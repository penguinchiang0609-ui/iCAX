#include "TubeNesting.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <mutex>
#include <numbers>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace iCAX::TubeNesting
{
    namespace
    {
        constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

        [[nodiscard]] double Up(const double Value_)
        {
            return std::nextafter(Value_, (std::numeric_limits<double>::infinity)());
        }

        [[nodiscard]] double Down(const double Value_)
        {
            return std::nextafter(Value_, -(std::numeric_limits<double>::infinity)());
        }

        [[nodiscard]] double NormalizeAngle(double Value_)
        {
            Value_ = std::fmod(Value_, kTwoPi);
            return Value_ < 0.0 ? Value_ + kTwoPi : Value_;
        }

        struct PoseKey final
        {
            std::array<std::uint64_t, 6> Words{};
            bool operator==(const PoseKey&) const = default;
        };

        struct PoseKeyHash final
        {
            std::size_t operator()(const PoseKey& Key_) const noexcept
            {
                std::uint64_t _Hash = 1469598103934665603ULL;
                for (const auto _Word : Key_.Words)
                {
                    _Hash ^= _Word;
                    _Hash *= 1099511628211ULL;
                    _Hash ^= _Word >> 32;
                    _Hash *= 1099511628211ULL;
                }
                return static_cast<std::size_t>(_Hash);
            }
        };

        [[nodiscard]] PoseKey KeyOf(const PairPoseMetadata& Pose_)
        {
            return {{
                std::bit_cast<std::uint64_t>(Pose_.RelativeRotationRadians),
                static_cast<std::uint64_t>(Pose_.RequiredSeparation),
                std::bit_cast<std::uint64_t>(Pose_.ContinuousSeparationUpper),
                std::bit_cast<std::uint64_t>(Pose_.ContinuousSeparationLower),
                std::bit_cast<std::uint64_t>(Pose_.OptimalityGap),
                static_cast<std::uint64_t>(Pose_.PoseStatus)
                    | (static_cast<std::uint64_t>(Pose_.SourceQuality) << 8)
            }};
        }

        void CompactPoseMetadata(PairTableSnapshot& Table_)
        {
            if (Table_.Entries.size() < 4096 || Table_.Poses.size() <= 2) return;
            // Straight and other low-order production profiles often share a
            // tiny number of exact pose records across millions of directed
            // entries.  Intern them after the parallel build.  Abort early if
            // the geometry is genuinely diverse, bounding temporary memory
            // instead of constructing a second four-million-item hash table.
            const std::size_t _MaximumUnique = (std::min)(std::size_t{262144},
                (std::max)(std::size_t{65536}, Table_.Types.size() * 32));
            std::unordered_map<PoseKey, std::uint32_t, PoseKeyHash> _Indices;
            _Indices.reserve((std::min)(_MaximumUnique, Table_.Entries.size()));
            std::vector<PairPoseMetadata> _Compacted(1);
            _Compacted.reserve((std::min)(_MaximumUnique + 1, Table_.Poses.size()));
            std::vector<std::uint32_t> _Remapped(Table_.Entries.size(), 0);
            for (std::size_t _EntryIndex = 0;
                _EntryIndex < Table_.Entries.size(); ++_EntryIndex)
            {
                const auto& _Entry = Table_.Entries[_EntryIndex];
                if (_Entry.Status != PairEntryStatus::Feasible
                    && _Entry.Status != PairEntryStatus::Certified)
                    continue;
                if (_Entry.PoseMetadataIndex == 0
                    || _Entry.PoseMetadataIndex >= Table_.Poses.size())
                    return;
                const auto& _Pose = Table_.Poses[_Entry.PoseMetadataIndex];
                const auto _Key = KeyOf(_Pose);
                const auto _Found = _Indices.find(_Key);
                if (_Found != _Indices.end())
                {
                    _Remapped[_EntryIndex] = _Found->second;
                    continue;
                }
                if (_Compacted.size() > _MaximumUnique) return;
                const auto _NewIndex = static_cast<std::uint32_t>(_Compacted.size());
                _Compacted.push_back(_Pose);
                _Indices.emplace(_Key, _NewIndex);
                _Remapped[_EntryIndex] = _NewIndex;
            }
            for (std::size_t _EntryIndex = 0;
                _EntryIndex < Table_.Entries.size(); ++_EntryIndex)
                if (_Remapped[_EntryIndex] != 0)
                    Table_.Entries[_EntryIndex].PoseMetadataIndex = _Remapped[_EntryIndex];
            Table_.Poses.swap(_Compacted);
        }

        struct Series final
        {
            double Constant = 0.0;
            std::vector<double> Cosine;
            std::vector<double> Sine;
            double Residual = 0.0;
            double FirstDerivative = 0.0;
            double SecondDerivative = 0.0;
            bool Certified = false;
            ProfileSourceQuality SourceQuality = ProfileSourceQuality::Missing;
        };

        [[nodiscard]] bool IsUsable(const PeriodicEndProfile& Profile_)
        {
            if (!Profile_.CompleteSingleValued || Profile_.Levels.empty()
                || Profile_.SourceQuality == ProfileSourceQuality::Missing)
                return false;
            for (const auto& _Level : Profile_.Levels)
            {
                if (_Level.Order != _Level.Cosine.size()
                    || _Level.Order != _Level.Sine.size()
                    || !std::isfinite(_Level.Constant)
                    || !std::isfinite(_Level.ResidualUpper)
                    || !std::isfinite(_Level.FirstDerivativeUpper)
                    || !std::isfinite(_Level.SecondDerivativeUpper)
                    || _Level.ResidualUpper < 0.0
                    || _Level.FirstDerivativeUpper < 0.0
                    || _Level.SecondDerivativeUpper < 0.0
                    || std::ranges::any_of(_Level.Cosine,
                        [](const double Value_) { return !std::isfinite(Value_); })
                    || std::ranges::any_of(_Level.Sine,
                        [](const double Value_) { return !std::isfinite(Value_); }))
                    return false;
            }
            return true;
        }

        [[nodiscard]] Series MakeSeries(
            const FourierLevel& Level_, const ProfileSourceQuality Quality_,
            const bool Reverse_, const Length PartLength_)
        {
            Series _Result;
            _Result.Constant = Reverse_
                ? static_cast<double>(PartLength_) - Level_.Constant
                : Level_.Constant;
            _Result.Cosine.resize(Level_.Order);
            _Result.Sine.resize(Level_.Order);
            for (std::size_t _Index = 0; _Index < Level_.Order; ++_Index)
            {
                _Result.Cosine[_Index] = Reverse_
                    ? -Level_.Cosine[_Index] : Level_.Cosine[_Index];
                // L-f(-theta): cosine changes sign, sine keeps its sign.
                _Result.Sine[_Index] = Reverse_
                    ? Level_.Sine[_Index] : Level_.Sine[_Index];
            }
            _Result.Residual = Level_.ResidualUpper;
            _Result.FirstDerivative = Level_.FirstDerivativeUpper;
            _Result.SecondDerivative = Level_.SecondDerivativeUpper;
            _Result.Certified = Level_.Certified;
            _Result.SourceQuality = Quality_;
            return _Result;
        }

        [[nodiscard]] double Evaluate(
            const Series& Value_, const double Theta_, const int Derivative_ = 0)
        {
            double _Result = Derivative_ == 0 ? Value_.Constant : 0.0;
            for (std::size_t _Index = 0; _Index < Value_.Cosine.size(); ++_Index)
            {
                const double _K = static_cast<double>(_Index + 1);
                const double _Angle = _K * Theta_;
                const double _Cosine = std::cos(_Angle);
                const double _Sine = std::sin(_Angle);
                if (Derivative_ == 0)
                    _Result += Value_.Cosine[_Index] * _Cosine
                        + Value_.Sine[_Index] * _Sine;
                else if (Derivative_ == 1)
                    _Result += _K * (-Value_.Cosine[_Index] * _Sine
                        + Value_.Sine[_Index] * _Cosine);
                else
                    _Result -= _K * _K * (Value_.Cosine[_Index] * _Cosine
                        + Value_.Sine[_Index] * _Sine);
            }
            return _Result;
        }

        [[nodiscard]] double Difference(
            const Series& Tail_, const Series& Head_,
            const double Theta_, const double Phase_, const int Derivative_ = 0)
        {
            return Evaluate(Tail_, Theta_, Derivative_)
                - Evaluate(Head_, Theta_ - Phase_, Derivative_);
        }

        [[nodiscard]] double DerivativeBound(const Series& Value_, const int Derivative_)
        {
            double _Result = 0.0;
            for (std::size_t _Index = 0; _Index < Value_.Cosine.size(); ++_Index)
            {
                const double _K = static_cast<double>(_Index + 1);
                const double _Amplitude = std::hypot(
                    Value_.Cosine[_Index], Value_.Sine[_Index]);
                _Result += (Derivative_ == 1 ? _K : _K * _K) * _Amplitude;
            }
            return Up(_Result);
        }

        struct InnerInterval final
        {
            double Minimum = 0.0;
            double Maximum = 0.0;
            double Upper = 0.0;
        };

        struct InnerIntervalLess final
        {
            bool operator()(const InnerInterval& Left_, const InnerInterval& Right_) const
            {
                return Left_.Upper < Right_.Upper;
            }
        };

        struct InnerResult final
        {
            double Lower = -(std::numeric_limits<double>::infinity)();
            double Upper = (std::numeric_limits<double>::infinity)();
            bool Complete = false;
            std::size_t Evaluations = 0;
            std::vector<double> Contacts;
        };

        [[nodiscard]] double InnerUpper(
            const Series& Tail_, const Series& Head_, const double Phase_,
            const double Minimum_, const double Maximum_, const double SecondBound_)
        {
            const double _Middle = (Minimum_ + Maximum_) * 0.5;
            const double _Radius = (Maximum_ - Minimum_) * 0.5;
            const double _Value = Difference(Tail_, Head_, _Middle, Phase_);
            const double _Derivative = Difference(Tail_, Head_, _Middle, Phase_, 1);
            return Up(_Value + std::abs(_Derivative) * _Radius
                + 0.5 * SecondBound_ * _Radius * _Radius);
        }

        [[nodiscard]] InnerResult MaximizeAtPhase(
            const Series& Tail_, const Series& Head_, const double Phase_,
            const double Tolerance_, const std::size_t MaximumIntervals_,
            const std::size_t ContactCount_)
        {
            InnerResult _Result;
            const double _SecondBound = Up(
                DerivativeBound(Tail_, 2) + DerivativeBound(Head_, 2));
            std::priority_queue<InnerInterval, std::vector<InnerInterval>, InnerIntervalLess> _Queue;
            constexpr std::size_t _InitialIntervals = 16;
            std::vector<std::pair<double, double>> _Samples;
            _Samples.reserve(_InitialIntervals * 3 + MaximumIntervals_ / 8);
            auto _Observe = [&](const double Theta_)
            {
                const double _Value = Difference(Tail_, Head_, Theta_, Phase_);
                _Result.Lower = (std::max)(_Result.Lower, Down(_Value));
                _Samples.emplace_back(_Value, NormalizeAngle(Theta_));
                ++_Result.Evaluations;
            };
            for (std::size_t _Index = 0; _Index < _InitialIntervals; ++_Index)
            {
                const double _Minimum = kTwoPi * static_cast<double>(_Index)
                    / static_cast<double>(_InitialIntervals);
                const double _Maximum = kTwoPi * static_cast<double>(_Index + 1)
                    / static_cast<double>(_InitialIntervals);
                _Observe((_Minimum + _Maximum) * 0.5);
                _Queue.push({_Minimum, _Maximum,
                    InnerUpper(Tail_, Head_, Phase_, _Minimum, _Maximum, _SecondBound)});
            }

            std::size_t _Intervals = _InitialIntervals;
            while (!_Queue.empty())
            {
                _Result.Upper = (std::max)(_Result.Lower, _Queue.top().Upper);
                if (_Result.Upper - _Result.Lower <= Tolerance_)
                {
                    _Result.Complete = true;
                    break;
                }
                if (_Intervals >= MaximumIntervals_) break;
                const auto _Current = _Queue.top();
                _Queue.pop();
                if (_Current.Upper <= _Result.Lower) continue;
                const double _Middle = (_Current.Minimum + _Current.Maximum) * 0.5;
                const std::array<std::pair<double, double>, 2> _Children{{
                    {_Current.Minimum, _Middle}, {_Middle, _Current.Maximum}
                }};
                for (const auto& [_Minimum, _Maximum] : _Children)
                {
                    _Observe((_Minimum + _Maximum) * 0.5);
                    const double _Upper = InnerUpper(
                        Tail_, Head_, Phase_, _Minimum, _Maximum, _SecondBound);
                    if (_Upper > _Result.Lower)
                        _Queue.push({_Minimum, _Maximum, _Upper});
                    ++_Intervals;
                }
            }
            if (_Queue.empty())
            {
                _Result.Upper = _Result.Lower;
                _Result.Complete = true;
            }
            else
            {
                _Result.Upper = (std::max)(_Result.Lower, _Queue.top().Upper);
            }

            std::sort(_Samples.begin(), _Samples.end(), [](const auto& Left_, const auto& Right_)
            {
                return Left_.first > Right_.first;
            });
            for (const auto& [_Value, _Theta] : _Samples)
            {
                (void)_Value;
                if (std::ranges::any_of(_Result.Contacts, [&](const double Existing_)
                    { return std::abs(Existing_ - _Theta) < 1.0e-10; }))
                    continue;
                _Result.Contacts.push_back(_Theta);
                if (_Result.Contacts.size() >= (std::max)(std::size_t{1}, ContactCount_)) break;
            }
            return _Result;
        }

        [[nodiscard]] double ContactLower(
            const Series& Tail_, const Series& Head_, const double Phase_,
            const double Radius_, const std::vector<double>& Contacts_,
            const double Residual_)
        {
            if (Contacts_.empty())
                return -(std::numeric_limits<double>::infinity)();
            std::vector<double> _Values, _Slopes;
            _Values.reserve(Contacts_.size());
            _Slopes.reserve(Contacts_.size());
            for (const double _Theta : Contacts_)
            {
                _Values.push_back(Difference(Tail_, Head_, _Theta, Phase_));
                // d/dphi [-g(theta-phi)] = +g'(theta-phi).
                _Slopes.push_back(Evaluate(Head_, _Theta - Phase_, 1));
            }
            std::vector<double> _Candidates{-Radius_, Radius_};
            for (std::size_t _Left = 0; _Left < _Values.size(); ++_Left)
                for (std::size_t _Right = _Left + 1; _Right < _Values.size(); ++_Right)
                {
                    const double _Denominator = _Slopes[_Left] - _Slopes[_Right];
                    if (std::abs(_Denominator) <= 1.0e-15) continue;
                    const double _T = (_Values[_Right] - _Values[_Left]) / _Denominator;
                    if (_T >= -Radius_ && _T <= Radius_) _Candidates.push_back(_T);
                }
            const double _SecondBound = DerivativeBound(Head_, 2);
            double _Minimum = (std::numeric_limits<double>::infinity)();
            for (const double _T : _Candidates)
            {
                double _Envelope = -(std::numeric_limits<double>::infinity)();
                for (std::size_t _Index = 0; _Index < _Values.size(); ++_Index)
                    _Envelope = (std::max)(_Envelope,
                        Down(_Values[_Index] + _Slopes[_Index] * _T));
                _Minimum = (std::min)(_Minimum,
                    Down(_Envelope - 0.5 * _SecondBound * _T * _T));
            }
            return Down(_Minimum - Residual_);
        }

        struct PhaseInterval final
        {
            double Minimum = 0.0;
            double Maximum = 0.0;
            double Lower = 0.0;
        };

        struct PhaseIntervalGreater final
        {
            bool operator()(const PhaseInterval& Left_, const PhaseInterval& Right_) const
            {
                return Left_.Lower > Right_.Lower;
            }
        };

        [[nodiscard]] std::vector<AngleInterval> ContinuousIntervals(const AngleDomain& Domain_)
        {
            if (Domain_.Intervals.empty() && Domain_.DiscreteRadians.empty())
                return {{0.0, kTwoPi}};
            std::vector<AngleInterval> _Result;
            for (const auto& _Input : Domain_.Intervals)
            {
                if (!std::isfinite(_Input.MinimumRadians)
                    || !std::isfinite(_Input.MaximumRadians)) continue;
                const double _Span = _Input.MaximumRadians - _Input.MinimumRadians;
                if (std::abs(_Span) >= kTwoPi)
                {
                    _Result = {{0.0, kTwoPi}};
                    break;
                }
                double _Minimum = NormalizeAngle(_Input.MinimumRadians);
                double _Maximum = NormalizeAngle(_Input.MaximumRadians);
                if (std::abs(_Span) <= 1.0e-15)
                {
                    _Result.push_back({_Minimum, _Minimum});
                }
                else if (_Minimum < _Maximum)
                {
                    _Result.push_back({_Minimum, _Maximum});
                }
                else
                {
                    _Result.push_back({_Minimum, kTwoPi});
                    _Result.push_back({0.0, _Maximum});
                }
            }
            return _Result;
        }

        [[nodiscard]] ProfileSourceQuality CombineQuality(
            const ProfileSourceQuality Left_, const ProfileSourceQuality Right_)
        {
            // Missing is encoded as zero for stable serialization, not because
            // it is better than a native source.  It contaminates the pair and
            // must never be hidden by max(NativeCertified, Missing).
            if (Left_ == ProfileSourceQuality::Missing
                || Right_ == ProfileSourceQuality::Missing)
                return ProfileSourceQuality::Missing;
            return static_cast<ProfileSourceQuality>((std::max)(
                static_cast<unsigned>(Left_), static_cast<unsigned>(Right_)));
        }

        [[nodiscard]] PairPoseResult SolveAtLevels(
            const PairPartType& PartA_, const Series& Tail_,
            const PairPartType& PartB_, const Series& Head_,
            const AngleDomain& Domain_, const PairPoseSolverSettings& Settings_)
        {
            PairPoseResult _Result;
            _Result.SourceQuality = CombineQuality(Tail_.SourceQuality, Head_.SourceQuality);
            const double _Residual = Tail_.Residual + Head_.Residual;
            const double _PhysicalMinimum = static_cast<double>((std::max)(
                Length{0}, PartA_.MaximumLength - PartB_.MaximumLength));

            // Straight cuts and conservative AABB end planes are by far the
            // most common production case.  Their spectra have no harmonics,
            // so a phase branch-and-bound would do exactly the same work for
            // every angle.  Resolve that case in O(1); this is what keeps a
            // 1000-type (four million directed pose) table practical.
            if (Tail_.Cosine.empty() && Head_.Cosine.empty())
            {
                std::optional<double> _Phase;
                for (const auto _Value : Domain_.DiscreteRadians)
                    if (std::isfinite(_Value))
                    {
                        _Phase = NormalizeAngle(_Value);
                        break;
                    }
                if (!_Phase)
                {
                    const auto _Intervals = ContinuousIntervals(Domain_);
                    if (!_Intervals.empty()) _Phase = _Intervals.front().MinimumRadians;
                }
                if (!_Phase)
                {
                    _Result.Status = PairPoseStatus::Forbidden;
                    _Result.Diagnostic = "allowed rotation domain is empty";
                    return _Result;
                }
                const double _Difference = Tail_.Constant - Head_.Constant;
                _Result.RelativeRotationRadians = *_Phase;
                _Result.RequiredSeparationUpper = (std::max)(_PhysicalMinimum,
                    _Residual == 0.0 ? _Difference : Up(_Difference + _Residual));
                _Result.RequiredSeparationLower = (std::max)(_PhysicalMinimum,
                    _Residual == 0.0 ? _Difference : Down(_Difference - _Residual));
                _Result.OptimalityGap = (std::max)(0.0,
                    _Result.RequiredSeparationUpper - _Result.RequiredSeparationLower);
                _Result.PhaseIntervalsEvaluated = 1;
                _Result.AxialIntervalsEvaluated = 1;
                const bool _CertifiedSource = Tail_.Certified && Head_.Certified
                    && _Result.SourceQuality != ProfileSourceQuality::NumericOnly
                    && _Result.SourceQuality != ProfileSourceQuality::Missing;
                if (_CertifiedSource && _Result.OptimalityGap <= Settings_.Tolerance)
                {
                    _Result.Status = PairPoseStatus::CertifiedEpsilonOptimal;
                    _Result.Diagnostic = "constant end profiles solved analytically";
                }
                else
                {
                    _Result.Status = PairPoseStatus::BestFeasible;
                    _Result.Diagnostic = "constant end profiles retain source residual uncertainty";
                }
                return _Result;
            }
            double _BestUpper = static_cast<double>(PartA_.MaximumLength);
            double _BestPhase = 0.0;
            double _DiscreteLower = (std::numeric_limits<double>::infinity)();
            bool _HasFeasible = false;
            bool _AllInnerComplete = true;
            std::priority_queue<PhaseInterval,
                std::vector<PhaseInterval>, PhaseIntervalGreater> _Queue;

            auto _EvaluatePhase = [&](const double Phase_)
            {
                const auto _Inner = MaximizeAtPhase(
                    Tail_, Head_, NormalizeAngle(Phase_), Settings_.FixedPhaseTolerance,
                    Settings_.MaximumAxialIntervals, Settings_.InitialContactCount);
                _Result.AxialIntervalsEvaluated += _Inner.Evaluations;
                _AllInnerComplete = _AllInnerComplete && _Inner.Complete;
                const double _Upper = (std::max)(_PhysicalMinimum, Up(_Inner.Upper + _Residual));
                if (!_HasFeasible || _Upper < _BestUpper)
                {
                    _BestUpper = _Upper;
                    _BestPhase = NormalizeAngle(Phase_);
                    _HasFeasible = true;
                }
                return _Inner;
            };

            auto _AddInterval = [&](const double Minimum_, const double Maximum_)
            {
                if (Maximum_ < Minimum_) return;
                const double _Middle = (Minimum_ + Maximum_) * 0.5;
                const auto _Inner = _EvaluatePhase(_Middle);
                const double _Radius = (Maximum_ - Minimum_) * 0.5;
                double _Lower = ContactLower(
                    Tail_, Head_, _Middle, _Radius, _Inner.Contacts, _Residual);
                if (!std::isfinite(_Lower))
                    _Lower = Down(_Inner.Lower
                        - DerivativeBound(Head_, 1) * _Radius - _Residual);
                _Lower = (std::max)(_PhysicalMinimum, _Lower);
                _Queue.push({Minimum_, Maximum_, _Lower});
                ++_Result.PhaseIntervalsEvaluated;
            };

            for (const double _Phase : Domain_.DiscreteRadians)
                if (std::isfinite(_Phase))
                {
                    const auto _Inner = _EvaluatePhase(_Phase);
                    _DiscreteLower = (std::min)(_DiscreteLower,
                        (std::max)(_PhysicalMinimum, Down(_Inner.Lower - _Residual)));
                }
            for (const auto& _Interval : ContinuousIntervals(Domain_))
            {
                if (_Interval.MaximumRadians - _Interval.MinimumRadians <= 1.0e-15)
                {
                    (void)_EvaluatePhase(_Interval.MinimumRadians);
                    continue;
                }
                constexpr std::size_t _InitialPieces = 8;
                for (std::size_t _Index = 0; _Index < _InitialPieces; ++_Index)
                {
                    const double _Minimum = _Interval.MinimumRadians
                        + (_Interval.MaximumRadians - _Interval.MinimumRadians)
                            * static_cast<double>(_Index) / _InitialPieces;
                    const double _Maximum = _Interval.MinimumRadians
                        + (_Interval.MaximumRadians - _Interval.MinimumRadians)
                            * static_cast<double>(_Index + 1) / _InitialPieces;
                    _AddInterval(_Minimum, _Maximum);
                }
            }
            if (!_HasFeasible)
            {
                _Result.Status = PairPoseStatus::Forbidden;
                _Result.Diagnostic = "allowed rotation domain is empty";
                return _Result;
            }

            const auto _QueueLower = [&]
            {
                const double _Continuous = _Queue.empty()
                    ? (std::numeric_limits<double>::infinity)() : _Queue.top().Lower;
                const double _Combined = (std::min)(_Continuous, _DiscreteLower);
                return std::isfinite(_Combined) ? (std::min)(_BestUpper, _Combined) : _BestUpper;
            };
            double _GlobalLower = _QueueLower();
            while (!_Queue.empty()
                && _BestUpper - _GlobalLower > Settings_.Tolerance
                && _Result.PhaseIntervalsEvaluated < Settings_.MaximumPhaseIntervals)
            {
                const auto _Current = _Queue.top();
                _Queue.pop();
                if (_Current.Lower >= _BestUpper)
                {
                    _GlobalLower = _QueueLower();
                    continue;
                }
                const double _Middle = (_Current.Minimum + _Current.Maximum) * 0.5;
                _AddInterval(_Current.Minimum, _Middle);
                _AddInterval(_Middle, _Current.Maximum);
                _GlobalLower = _QueueLower();
            }
            _GlobalLower = _QueueLower();

            _Result.RelativeRotationRadians = _BestPhase;
            _Result.RequiredSeparationUpper = _BestUpper;
            _Result.RequiredSeparationLower = _GlobalLower;
            _Result.OptimalityGap = (std::max)(0.0, _BestUpper - _GlobalLower);
            const bool _CertifiedSource = Tail_.Certified && Head_.Certified
                && _Result.SourceQuality != ProfileSourceQuality::NumericOnly
                && _Result.SourceQuality != ProfileSourceQuality::Missing;
            if (_CertifiedSource && _AllInnerComplete
                && _Result.OptimalityGap <= Settings_.Tolerance)
            {
                _Result.Status = PairPoseStatus::CertifiedEpsilonOptimal;
                _Result.Diagnostic = "certified within requested axial tolerance";
            }
            else if (_Result.PhaseIntervalsEvaluated >= Settings_.MaximumPhaseIntervals
                || !_AllInnerComplete)
            {
                _Result.Status = PairPoseStatus::BudgetExhausted;
                _Result.Diagnostic = "best feasible pose returned with a remaining bound gap";
            }
            else
            {
                _Result.Status = PairPoseStatus::BestFeasible;
                _Result.Diagnostic = "source profile is not natively certified";
            }
            return _Result;
        }
    }

    PeriodicEndProfile BuildPeriodicProfileFromSamples(
        const std::vector<Length>& Samples_, const double AxialOffset_,
        const std::size_t MaximumOrder_, const double SourceResidualUpper_)
    {
        PeriodicEndProfile _Result;
        _Result.SourceQuality = ProfileSourceQuality::ApproximateCertified;
        if (Samples_.size() < 2 || !std::isfinite(AxialOffset_)
            || !std::isfinite(SourceResidualUpper_) || SourceResidualUpper_ < 0.0)
            return _Result;
        std::uint64_t _SourceHash = 1469598103934665603ULL;
        const auto _HashWord = [&](const std::uint64_t Word_)
        {
            constexpr std::uint64_t _Prime = 1099511628211ULL;
            for (unsigned _Byte = 0; _Byte < 8; ++_Byte)
            {
                _SourceHash ^= (Word_ >> (_Byte * 8)) & 0xffU;
                _SourceHash *= _Prime;
            }
        };
        _HashWord(Samples_.size());
        _HashWord(MaximumOrder_);
        _HashWord(std::bit_cast<std::uint64_t>(AxialOffset_));
        _HashWord(std::bit_cast<std::uint64_t>(SourceResidualUpper_));
        for (const auto _Sample : Samples_)
            _HashWord(static_cast<std::uint64_t>(_Sample));
        std::ostringstream _HashText;
        _HashText << "sampled-fnv1a64:" << std::hex << _SourceHash;
        _Result.SourceHash = _HashText.str();
        _Result.CompleteSingleValued = true;
        const std::size_t _MaximumOrder = (std::min)(
            MaximumOrder_, Samples_.size() > 2 ? (Samples_.size() - 1) / 2 : std::size_t{0});
        std::vector<std::size_t> _Orders{0};
        for (std::size_t _Order = 1; _Order <= _MaximumOrder; _Order *= 2)
        {
            _Orders.push_back(_Order);
            if (_Order > _MaximumOrder / 2) break;
        }
        if (_Orders.back() != _MaximumOrder) _Orders.push_back(_MaximumOrder);
        const double _Delta = kTwoPi / static_cast<double>(Samples_.size());
        for (const std::size_t _Order : _Orders)
        {
            FourierLevel _Level;
            _Level.Order = _Order;
            _Level.Constant = AxialOffset_;
            for (const Length _Value : Samples_)
                _Level.Constant += static_cast<double>(_Value) / Samples_.size();
            _Level.Cosine.assign(_Order, 0.0);
            _Level.Sine.assign(_Order, 0.0);
            for (std::size_t _K = 1; _K <= _Order; ++_K)
            {
                for (std::size_t _Index = 0; _Index < Samples_.size(); ++_Index)
                {
                    const double _Angle = kTwoPi * static_cast<double>(_Index)
                        / static_cast<double>(Samples_.size());
                    _Level.Cosine[_K - 1] += 2.0 * static_cast<double>(Samples_[_Index])
                        * std::cos(static_cast<double>(_K) * _Angle) / Samples_.size();
                    _Level.Sine[_K - 1] += 2.0 * static_cast<double>(Samples_[_Index])
                        * std::sin(static_cast<double>(_K) * _Angle) / Samples_.size();
                }
            }
            Series _Series;
            _Series.Constant = _Level.Constant;
            _Series.Cosine = _Level.Cosine;
            _Series.Sine = _Level.Sine;
            _Level.FirstDerivativeUpper = DerivativeBound(_Series, 1);
            _Level.SecondDerivativeUpper = DerivativeBound(_Series, 2);
            double _Residual = SourceResidualUpper_;
            constexpr std::size_t _SubIntervals = 8;
            for (std::size_t _Index = 0; _Index < Samples_.size(); ++_Index)
            {
                const double _Left = static_cast<double>(Samples_[_Index]) + AxialOffset_;
                const double _Right = static_cast<double>(Samples_[(_Index + 1) % Samples_.size()])
                    + AxialOffset_;
                const double _Slope = (_Right - _Left) / _Delta;
                for (std::size_t _Sub = 0; _Sub < _SubIntervals; ++_Sub)
                {
                    const double _Fraction = (static_cast<double>(_Sub) + 0.5) / _SubIntervals;
                    const double _Theta = (static_cast<double>(_Index) + _Fraction) * _Delta;
                    const double _Native = _Left + (_Right - _Left) * _Fraction;
                    const double _Radius = _Delta / (2.0 * _SubIntervals);
                    const double _Bound = std::abs(_Native - Evaluate(_Series, _Theta))
                        + _Radius * (std::abs(_Slope) + _Level.FirstDerivativeUpper)
                        + SourceResidualUpper_;
                    _Residual = (std::max)(_Residual, Up(_Bound));
                }
            }
            _Level.ResidualUpper = _Residual;
            _Level.Certified = true;
            _Result.Levels.push_back(std::move(_Level));
        }
        return _Result;
    }

    PairPoseResult SolveOrientedPairPose(
        const PairPartType& PartA_, const PairTypeGeometry& GeometryA_, const Direction DirectionA_,
        const PairPartType& PartB_, const PairTypeGeometry& GeometryB_, const Direction DirectionB_,
        const AngleDomain& Domain_, const PairPoseSolverSettings& Settings_)
    {
        PairPoseResult _Invalid;
        if (PartA_.MaximumLength <= 0 || PartB_.MaximumLength <= 0
            || !std::isfinite(Settings_.Tolerance)
            || !std::isfinite(Settings_.FixedPhaseTolerance)
            || Settings_.Tolerance < 0.0 || Settings_.FixedPhaseTolerance <= 0.0
            || Settings_.MaximumPhaseIntervals < 8
            || Settings_.MaximumAxialIntervals < 16
            || Settings_.InitialContactCount == 0
            || !PartA_.DirectionAllowed[static_cast<std::size_t>(DirectionA_)]
            || !PartB_.DirectionAllowed[static_cast<std::size_t>(DirectionB_)])
        {
            _Invalid.Status = PairPoseStatus::InvalidInput;
            _Invalid.Diagnostic = "invalid part, direction, or tolerance";
            return _Invalid;
        }
        const auto& _TailProfile = DirectionA_ == Direction::Forward
            ? GeometryA_.Right : GeometryA_.Left;
        const auto& _HeadProfile = DirectionB_ == Direction::Forward
            ? GeometryB_.Left : GeometryB_.Right;
        if (!IsUsable(_TailProfile) || !IsUsable(_HeadProfile))
        {
            _Invalid.Status = PairPoseStatus::Unsupported;
            _Invalid.Diagnostic = "missing complete single-valued end profile";
            return _Invalid;
        }
        const std::size_t _LevelCount = (std::max)(
            _TailProfile.Levels.size(), _HeadProfile.Levels.size());
        PairPoseResult _Best;
        for (std::size_t _Step = 0; _Step < _LevelCount; ++_Step)
        {
            const auto& _TailLevel = _TailProfile.Levels[(std::min)(
                _Step, _TailProfile.Levels.size() - 1)];
            const auto& _HeadLevel = _HeadProfile.Levels[(std::min)(
                _Step, _HeadProfile.Levels.size() - 1)];
            const auto _Tail = MakeSeries(_TailLevel, _TailProfile.SourceQuality,
                DirectionA_ == Direction::Reverse, PartA_.MaximumLength);
            const auto _Head = MakeSeries(_HeadLevel, _HeadProfile.SourceQuality,
                DirectionB_ == Direction::Reverse, PartB_.MaximumLength);
            auto _Current = SolveAtLevels(PartA_, _Tail, PartB_, _Head, Domain_, Settings_);
            _Current.FourierOrder = (std::max)(_TailLevel.Order, _HeadLevel.Order);
            _Best = std::move(_Current);
            if (_Best.Status == PairPoseStatus::CertifiedEpsilonOptimal) break;
        }
        return _Best;
    }

    PairTableSnapshot BuildPairTable(
        const PairTableBuildInput& Input_, const PairTableBuildSettings& Settings_)
    {
        PairTableSnapshot _Table;
        _Table.Version = Input_.Version;
        _Table.TypeDefinitionHash = Input_.TypeDefinitionHash;
        _Table.ProcessConfigurationHash = Input_.ProcessConfigurationHash;
        _Table.Types = Input_.Types;
        _Table.Poses.resize(1); // metadata index zero is permanently reserved
        const std::size_t _TypeCount = Input_.Types.size();
        if (_TypeCount > (std::numeric_limits<std::size_t>::max)() / 2)
            return _Table;
        const std::size_t _StateCount = _TypeCount * 2;
        if (_StateCount != 0
            && _StateCount > (std::numeric_limits<std::size_t>::max)() / _StateCount)
            return _Table;
        const std::size_t _EntryCount = _StateCount * _StateCount;
        if (_TypeCount == 0 || Input_.Geometry.size() != _TypeCount)
            return _Table;
        std::vector<std::string> _TypeIDs;
        _TypeIDs.reserve(_TypeCount);
        for (const auto& _Type : Input_.Types)
        {
            if (_Type.ID.empty() || _Type.CompatibilityKey.empty()
                || _Type.MaximumLength <= 0 || _Type.MaterialLength < 0
                || _Type.MaterialLength > _Type.MaximumLength
                || (!_Type.DirectionAllowed[0] && !_Type.DirectionAllowed[1]))
                return _Table;
            _TypeIDs.push_back(_Type.ID);
        }
        std::sort(_TypeIDs.begin(), _TypeIDs.end());
        if (std::adjacent_find(_TypeIDs.begin(), _TypeIDs.end()) != _TypeIDs.end())
            return _Table;
        if (_TypeCount > (std::numeric_limits<std::size_t>::max)() / _TypeCount)
            return _Table;
        const std::size_t _DirectedPairCount = _TypeCount * _TypeCount;
        if (!Input_.DirectedPairDomains.empty()
            && Input_.DirectedPairDomains.size() != _DirectedPairCount)
            return _Table;
        if (!Input_.OrientedPairDomains.empty()
            && Input_.OrientedPairDomains.size() != _EntryCount)
            return _Table;
        if (!Input_.PairProcessLosses.empty()
            && Input_.PairProcessLosses.size() != _EntryCount)
            return _Table;
        if (std::ranges::any_of(Input_.PairProcessLosses,
            [](const Length Value_) { return Value_ < 0; }))
            return _Table;
        if (_EntryCount >= static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)()))
            return _Table;
        if (!std::isfinite(Settings_.Pose.Tolerance)
            || !std::isfinite(Settings_.Pose.FixedPhaseTolerance)
            || Settings_.Pose.Tolerance < 0.0
            || Settings_.Pose.FixedPhaseTolerance <= 0.0
            || Settings_.Pose.MaximumPhaseIntervals < 8
            || Settings_.Pose.MaximumAxialIntervals < 16
            || Settings_.Pose.InitialContactCount == 0)
            return _Table;

        _Table.Entries.resize(_EntryCount);

        // Pre-size final metadata and let each worker own one entry/pose slot.
        // A PairPoseResult contains a diagnostic string; retaining four
        // million temporary results for 1000 types would otherwise add several
        // hundred MB to the already necessary table storage.
        _Table.Poses.resize(_EntryCount + 1);
        const auto _ExactConstant = [](const PeriodicEndProfile& Profile_, double& Value_)
        {
            if (!Profile_.CompleteSingleValued || Profile_.Levels.empty()
                || Profile_.SourceQuality == ProfileSourceQuality::Missing
                || Profile_.SourceQuality == ProfileSourceQuality::NumericOnly)
                return false;
            const double _Value = Profile_.Levels.back().Constant;
            for (const auto& _Level : Profile_.Levels)
                if (_Level.Order != 0 || !_Level.Certified || _Level.ResidualUpper != 0.0
                    || _Level.Constant != _Value)
                    return false;
            Value_ = _Value;
            return true;
        };
        const auto _AllowedConstantPhase = [](const AngleDomain& Domain_, double& Phase_)
        {
            if (Domain_.Intervals.empty() && Domain_.DiscreteRadians.empty())
            {
                Phase_ = 0.0;
                return true;
            }
            for (const auto _Value : Domain_.DiscreteRadians)
                if (std::isfinite(_Value))
                {
                    Phase_ = NormalizeAngle(_Value);
                    return true;
                }
            for (const auto& _Interval : Domain_.Intervals)
                if (std::isfinite(_Interval.MinimumRadians)
                    && std::isfinite(_Interval.MaximumRadians))
                {
                    Phase_ = NormalizeAngle(_Interval.MinimumRadians);
                    return true;
                }
            return false;
        };
        const auto _CommitPose = [&](const std::size_t Index_, const PairPartType& PartA_,
            const PairPartType& PartB_, const double Phase_, const double Upper_,
            const double Lower_, const PairPoseStatus Status_,
            const ProfileSourceQuality Quality_)
        {
            if (Settings_.RequireCertifiedProfiles
                && Status_ != PairPoseStatus::CertifiedEpsilonOptimal)
                return;
            const double _SeparationValue = (std::max)(0.0, Upper_);
            if (_SeparationValue > static_cast<double>(
                (std::numeric_limits<Length>::max)()))
                return;
            const Length _Separation = static_cast<Length>(std::ceil(_SeparationValue));
            const Length _ProcessLoss = Input_.PairProcessLosses.empty()
                ? 0 : Input_.PairProcessLosses[Index_];
            const long double _SavingValue = static_cast<long double>(PartA_.MaximumLength)
                - static_cast<long double>(_Separation)
                - static_cast<long double>(_ProcessLoss);
            if (_SavingValue < static_cast<long double>(
                    (std::numeric_limits<Length>::min)())
                || _SavingValue > static_cast<long double>(
                    (std::numeric_limits<Length>::max)()))
                return;
            const Length _Saving = static_cast<Length>(std::floor(_SavingValue));
            if (_Saving > (std::min)(PartA_.MaximumLength, PartB_.MaximumLength))
                return;
            auto& _Entry = _Table.Entries[Index_];
            _Entry.Status = Status_ == PairPoseStatus::CertifiedEpsilonOptimal
                ? PairEntryStatus::Certified : PairEntryStatus::Feasible;
            _Entry.NetSaving = _Saving;
            _Entry.PoseMetadataIndex = static_cast<std::uint32_t>(Index_ + 1);
            _Table.Poses[Index_ + 1] = {
                Phase_, _Separation, Upper_, Lower_,
                (std::max)(0.0, Upper_ - Lower_), Status_, Quality_
            };
        };
        std::atomic_size_t _Next = 0;
        const auto _Worker = [&]
        {
            while (true)
            {
                constexpr std::size_t _ChunkSize = 256;
                const std::size_t _Begin = _Next.fetch_add(
                    _ChunkSize, std::memory_order_relaxed);
                if (_Begin >= _EntryCount) break;
                const std::size_t _End = (std::min)(_EntryCount, _Begin + _ChunkSize);
                for (std::size_t _Index = _Begin; _Index < _End; ++_Index)
                {
                const std::size_t _StateA = _Index / _StateCount;
                const std::size_t _StateB = _Index % _StateCount;
                const std::size_t _TypeA = _StateA / 2;
                const std::size_t _TypeB = _StateB / 2;
                const auto _DirectionA = static_cast<Direction>(_StateA % 2);
                const auto _DirectionB = static_cast<Direction>(_StateB % 2);
                const auto& _PartA = Input_.Types[_TypeA];
                const auto& _PartB = Input_.Types[_TypeB];
                if (_PartA.CompatibilityKey != _PartB.CompatibilityKey
                    || !_PartA.DirectionAllowed[static_cast<std::size_t>(_DirectionA)]
                    || !_PartB.DirectionAllowed[static_cast<std::size_t>(_DirectionB)])
                {
                    _Table.Entries[_Index].Status = PairEntryStatus::Forbidden;
                    continue;
                }
                const auto& _Domain = !Input_.OrientedPairDomains.empty()
                    ? Input_.OrientedPairDomains[_Index]
                    : (!Input_.DirectedPairDomains.empty()
                        ? Input_.DirectedPairDomains[_TypeA * _TypeCount + _TypeB]
                        : Input_.Geometry[_TypeB].RelativeRotationDomain);

                const auto& _TailProfile = _DirectionA == Direction::Forward
                    ? Input_.Geometry[_TypeA].Right : Input_.Geometry[_TypeA].Left;
                const auto& _HeadProfile = _DirectionB == Direction::Forward
                    ? Input_.Geometry[_TypeB].Left : Input_.Geometry[_TypeB].Right;
                double _TailConstant = 0.0;
                double _HeadConstant = 0.0;
                double _ConstantPhase = 0.0;
                if (_ExactConstant(_TailProfile, _TailConstant)
                    && _ExactConstant(_HeadProfile, _HeadConstant))
                {
                    if (!_AllowedConstantPhase(_Domain, _ConstantPhase))
                    {
                        _Table.Entries[_Index].Status = PairEntryStatus::Forbidden;
                        continue;
                    }
                    if (_DirectionA == Direction::Reverse)
                        _TailConstant = static_cast<double>(_PartA.MaximumLength) - _TailConstant;
                    if (_DirectionB == Direction::Reverse)
                        _HeadConstant = static_cast<double>(_PartB.MaximumLength) - _HeadConstant;
                    const double _Separation = (std::max)({0.0,
                        static_cast<double>((std::max)(Length{0},
                            _PartA.MaximumLength - _PartB.MaximumLength)),
                        _TailConstant - _HeadConstant});
                    const auto _Quality = CombineQuality(
                        _TailProfile.SourceQuality, _HeadProfile.SourceQuality);
                    _CommitPose(_Index, _PartA, _PartB, _ConstantPhase,
                        _Separation, _Separation,
                        PairPoseStatus::CertifiedEpsilonOptimal, _Quality);
                    continue;
                }
                const auto _Pose = SolveOrientedPairPose(
                    _PartA, Input_.Geometry[_TypeA], _DirectionA,
                    _PartB, Input_.Geometry[_TypeB], _DirectionB,
                    _Domain, Settings_.Pose);
                auto& _Entry = _Table.Entries[_Index];
                if (_Pose.Status == PairPoseStatus::Forbidden)
                {
                    _Entry.Status = PairEntryStatus::Forbidden;
                    continue;
                }
                if (_Pose.Status == PairPoseStatus::Unsupported
                    || _Pose.Status == PairPoseStatus::InvalidInput
                    || (Settings_.RequireCertifiedProfiles
                        && _Pose.Status != PairPoseStatus::CertifiedEpsilonOptimal))
                    continue;
                _CommitPose(_Index, _PartA, _PartB,
                    _Pose.RelativeRotationRadians, _Pose.RequiredSeparationUpper,
                    _Pose.RequiredSeparationLower, _Pose.Status, _Pose.SourceQuality);
                }
            }
        };
        const auto _Hardware = (std::max)(1u, std::thread::hardware_concurrency());
        const std::size_t _Concurrency = Settings_.MaximumConcurrency == 0
            ? (std::min)(static_cast<std::size_t>(_Hardware), std::size_t{16})
            : (std::max)(std::size_t{1}, Settings_.MaximumConcurrency);
        std::vector<std::thread> _Threads;
        _Threads.reserve(_Concurrency > 0 ? _Concurrency - 1 : 0);
        for (std::size_t _Index = 1; _Index < _Concurrency; ++_Index)
            _Threads.emplace_back(_Worker);
        _Worker();
        for (auto& _Thread : _Threads) _Thread.join();
        CompactPoseMetadata(_Table);
        return _Table;
    }

    bool ValidatePairTable(
        const PairTableSnapshot& Table_, std::vector<std::string>* Diagnostics_)
    {
        std::vector<std::string> _Local;
        auto& _Diagnostics = Diagnostics_ ? *Diagnostics_ : _Local;
        const auto _AddDiagnostic = [&](const char* Message_)
        {
            if (std::ranges::find(_Diagnostics, Message_) == _Diagnostics.end())
                _Diagnostics.emplace_back(Message_);
        };
        if (Table_.Types.size() > (std::numeric_limits<std::size_t>::max)() / 2)
        {
            _AddDiagnostic("pair-table type count overflows its state index");
            return false;
        }
        const std::size_t _StateCount = Table_.Types.size() * 2;
        if (_StateCount != 0
            && _StateCount > (std::numeric_limits<std::size_t>::max)() / _StateCount)
        {
            _AddDiagnostic("pair-table state count overflows its entry index");
            return false;
        }
        const std::size_t _ExpectedEntries = _StateCount * _StateCount;
        if (Table_.Schema != "icax.tube-pair-table.v1")
            _AddDiagnostic("unsupported pair-table schema");
        if (Table_.Entries.size() != _ExpectedEntries)
            _AddDiagnostic("pair-table entry count does not match its type count");
        if (Table_.Poses.empty())
            _AddDiagnostic("pair-table pose metadata array must reserve index zero");
        std::vector<std::string> _IDs;
        for (const auto& _Type : Table_.Types)
        {
            if (_Type.ID.empty() || _Type.CompatibilityKey.empty() || _Type.MaximumLength <= 0
                || _Type.MaterialLength < 0 || _Type.MaterialLength > _Type.MaximumLength
                || (!_Type.DirectionAllowed[0] && !_Type.DirectionAllowed[1]))
                _AddDiagnostic("pair-table contains an invalid part type");
            _IDs.push_back(_Type.ID);
        }
        std::sort(_IDs.begin(), _IDs.end());
        if (std::adjacent_find(_IDs.begin(), _IDs.end()) != _IDs.end())
            _AddDiagnostic("pair-table part type IDs are not unique");
        if (Table_.Entries.size() == _ExpectedEntries)
        {
            for (std::size_t _Index = 0; _Index < Table_.Entries.size(); ++_Index)
            {
                const auto& _Entry = Table_.Entries[_Index];
                if (static_cast<unsigned>(_Entry.Status)
                    > static_cast<unsigned>(PairEntryStatus::Certified))
                {
                    _AddDiagnostic("pair-table contains an invalid entry status");
                    continue;
                }
                if (_Entry.Status == PairEntryStatus::Missing)
                {
                    _AddDiagnostic("pair-table contains an unresolved entry");
                    continue;
                }
                if (_Entry.Status != PairEntryStatus::Feasible
                    && _Entry.Status != PairEntryStatus::Certified) continue;
                const auto _TypeA = (_Index / _StateCount) / 2;
                const auto _TypeB = (_Index % _StateCount) / 2;
                if (_Entry.NetSaving > (std::min)(
                    Table_.Types[_TypeA].MaximumLength, Table_.Types[_TypeB].MaximumLength))
                    _AddDiagnostic("pair-table saving exceeds a part length");
                if (_Entry.PoseMetadataIndex == 0
                    || _Entry.PoseMetadataIndex >= Table_.Poses.size())
                    _AddDiagnostic("pair-table entry references invalid pose metadata");
                else
                {
                    const auto& _Pose = Table_.Poses[_Entry.PoseMetadataIndex];
                    if (!std::isfinite(_Pose.RelativeRotationRadians)
                        || !std::isfinite(_Pose.ContinuousSeparationUpper)
                        || !std::isfinite(_Pose.ContinuousSeparationLower)
                        || !std::isfinite(_Pose.OptimalityGap)
                        || _Pose.RequiredSeparation < 0
                        || _Pose.ContinuousSeparationUpper
                            < _Pose.ContinuousSeparationLower
                        || _Pose.OptimalityGap < 0.0
                        || static_cast<unsigned>(_Pose.PoseStatus)
                            > static_cast<unsigned>(PairPoseStatus::BudgetExhausted)
                        || static_cast<unsigned>(_Pose.SourceQuality)
                            > static_cast<unsigned>(ProfileSourceQuality::NumericOnly))
                        _AddDiagnostic("pair-table contains invalid pose metadata");
                    if (_Entry.Status == PairEntryStatus::Certified
                        && (_Pose.PoseStatus
                                != PairPoseStatus::CertifiedEpsilonOptimal
                            || _Pose.SourceQuality == ProfileSourceQuality::Missing
                            || _Pose.SourceQuality == ProfileSourceQuality::NumericOnly))
                        _AddDiagnostic(
                            "pair-table certified entry lacks a certified geometry source");
                }
            }
        }
        return _Diagnostics.empty();
    }
}
