#include "pch.h"
#include "NestingAdapter.h"
#include "FinalGeometryMeasurement.h"

#include "TemplateRuntime/StandardJsonCodec.h"
#include "TubeNesting/TubeNesting.h"
#include "Task/Task.h"
#include <bit>
#include <cctype>
#include <iomanip>
#include <iterator>
#include <string_view>
#include <thread>

namespace iCAX::TubeDesigner
{
std::vector<SNestingVariant> BuildKnifePlaneNestingVariants(
    const SLinearNestingGeometry& Geometry_, const double EnvelopeLength_, const bool AllowHalfTurn_)
{
    constexpr double _Tolerance = 0.021;
    if (!Geometry_.IsReliable || !std::isfinite(EnvelopeLength_)
        || std::abs(Geometry_.EnvelopeLength - EnvelopeLength_) > _Tolerance
        || (Geometry_.Left.Projection <= _Tolerance && Geometry_.Right.Projection <= _Tolerance))
        return {};
    std::vector<SNestingVariant> _Variants;
    for (const bool _HalfTurn : { false, true })
    {
        if (_HalfTurn && !AllowHalfTurn_) continue;
        const auto _End = [&](const SPlanarNestingEnd& Source_) {
            const double _Sign = _HalfTurn ? -1.0 : 1.0;
            const auto _A = Source_.GradientY * _Sign, _B = Source_.GradientZ * _Sign;
            SNestingEnd _Result;
            if (!Source_.IsPlanar || !std::isfinite(_A) || !std::isfinite(_B)
                || !std::isfinite(Source_.Projection) || std::hypot(_A, _B) > 8.0) return _Result;
            _Result.Projection = std::max(0.0, Source_.Projection - _Tolerance);
            _Result.NestingPlane = std::to_string(std::llround(_A * 100000.0))
                + ":" + std::to_string(std::llround(_B * 100000.0));
            _Result.AllowTrapezoidNesting = _Result.Projection > 0.0;
            return _Result;
        };
        SNestingVariant _Variant;
        _Variant.ID = _HalfTurn ? "forward-180" : "forward-0";
        _Variant.EnvelopeLength = EnvelopeLength_;
        _Variant.MaterialLength = std::clamp(Geometry_.MaterialEquivalentLength, 0.01, EnvelopeLength_);
        _Variant.LeftEnd = _End(Geometry_.Left); _Variant.RightEnd = _End(Geometry_.Right);
        _Variant.RotationRadians = _HalfTurn ? 3.14159265358979323846 : 0.0;
        _Variants.push_back(std::move(_Variant));
    }
    return _Variants;
}

iCAX::Data::VariantArray MakeNestingPlacementTransform(
    const std::array<double, 3>& LocalCenter_, const double Start_, const double End_,
    const bool Reversed_, const double RotationRadians_)
{
    if (!std::isfinite(Start_) || !std::isfinite(End_) || Start_ < 0.0 || End_ <= Start_
        || !std::isfinite(RotationRadians_)
        || !std::ranges::all_of(LocalCenter_, [](const double Value_) { return std::isfinite(Value_); }))
        throw std::invalid_argument("排样零件变换参数无效");
    const double _XDirection = Reversed_ ? -1.0 : 1.0;
    // Reverse about local Y before applying the solved roll about X.  This is
    // the rigid transform corresponding to the pair-table convention
    // x' = L - x and theta' = -theta.  Reversing about Z instead would add an
    // unrecorded pi phase to every mixed forward/reverse adjacency.
    const double _ZDirection = Reversed_ ? -1.0 : 1.0;
    const double _Cosine = std::cos(RotationRadians_);
    const double _Sine = std::sin(RotationRadians_);
    const double _M11 = _Cosine;
    const double _M12 = -_Sine * _ZDirection;
    const double _M21 = _Sine;
    const double _M22 = _Cosine * _ZDirection;
    return {
        _XDirection, 0.0, 0.0, (Start_ + End_) * 0.5 - _XDirection * LocalCenter_[0],
        0.0, _M11, _M12, -(_M11 * LocalCenter_[1] + _M12 * LocalCenter_[2]),
        0.0, _M21, _M22, -(_M21 * LocalCenter_[1] + _M22 * LocalCenter_[2]),
        0.0, 0.0, 0.0, 1.0
    };
}

namespace
{
    using namespace iCAX::TubeNesting;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    // The foundation solver expands demand quantities into placement instances
    // and already guards that expansion at one million.  Keep the adapter in
    // sync so ordinary high-volume straight and miter cutting is not rejected
    // by an unrelated 2000-piece UI-era limit.
    constexpr std::size_t kMaximumPartInstances = 1'000'000;
    constexpr double kMaximumLength = 1000000.0;
    constexpr double kUnitsPerMm = 100.0;

    void HashWord(std::uint64_t& Hash_, std::uint64_t Value_)
    {
        constexpr std::uint64_t _Prime = 1099511628211ULL;
        for (unsigned _Byte = 0; _Byte < 8; ++_Byte)
        {
            Hash_ ^= (Value_ >> (_Byte * 8)) & 0xffU;
            Hash_ *= _Prime;
        }
    }

    void HashText(std::uint64_t& Hash_, const std::string_view Value_)
    {
        constexpr std::uint64_t _Prime = 1099511628211ULL;
        HashWord(Hash_, Value_.size());
        for (const unsigned char _Character : Value_)
        {
            Hash_ ^= _Character;
            Hash_ *= _Prime;
        }
    }

    void HashProfile(std::uint64_t& Hash_, const PeriodicEndProfile& Profile_)
    {
        HashText(Hash_, Profile_.Schema);
        HashText(Hash_, Profile_.SourceHash);
        HashText(Hash_, Profile_.CoordinateConvention);
        HashWord(Hash_, static_cast<std::uint64_t>(Profile_.SourceQuality));
        HashWord(Hash_, Profile_.CompleteSingleValued ? 1U : 0U);
        HashWord(Hash_, Profile_.Levels.size());
        for (const auto& _Level : Profile_.Levels)
        {
            HashWord(Hash_, _Level.Order);
            HashWord(Hash_, std::bit_cast<std::uint64_t>(_Level.Constant));
            HashWord(Hash_, std::bit_cast<std::uint64_t>(_Level.ResidualUpper));
            HashWord(Hash_, std::bit_cast<std::uint64_t>(_Level.FirstDerivativeUpper));
            HashWord(Hash_, std::bit_cast<std::uint64_t>(_Level.SecondDerivativeUpper));
            HashWord(Hash_, _Level.Certified ? 1U : 0U);
            for (const auto _Value : _Level.Cosine)
                HashWord(Hash_, std::bit_cast<std::uint64_t>(_Value));
            for (const auto _Value : _Level.Sine)
                HashWord(Hash_, std::bit_cast<std::uint64_t>(_Value));
        }
    }

    std::string TypeDefinitionFingerprint(const PairTableBuildInput& Input_)
    {
        std::uint64_t _Hash = 1469598103934665603ULL;
        HashWord(_Hash, Input_.Types.size());
        for (std::size_t _Index = 0; _Index < Input_.Types.size(); ++_Index)
        {
            const auto& _Type = Input_.Types[_Index];
            HashText(_Hash, _Type.ID);
            HashText(_Hash, _Type.CompatibilityKey);
            HashText(_Hash, _Type.OrderKey);
            HashWord(_Hash, static_cast<std::uint64_t>(_Type.MaximumLength));
            HashWord(_Hash, static_cast<std::uint64_t>(_Type.MaterialLength));
            HashWord(_Hash, _Type.DirectionAllowed[0] ? 1U : 0U);
            HashWord(_Hash, _Type.DirectionAllowed[1] ? 1U : 0U);
            const auto& _Geometry = Input_.Geometry[_Index];
            HashProfile(_Hash, _Geometry.Left);
            HashProfile(_Hash, _Geometry.Right);
            HashWord(_Hash, _Geometry.RelativeRotationDomain.Intervals.size());
            for (const auto& _Interval : _Geometry.RelativeRotationDomain.Intervals)
            {
                HashWord(_Hash, std::bit_cast<std::uint64_t>(_Interval.MinimumRadians));
                HashWord(_Hash, std::bit_cast<std::uint64_t>(_Interval.MaximumRadians));
            }
            HashWord(_Hash, _Geometry.RelativeRotationDomain.DiscreteRadians.size());
            for (const auto _Angle : _Geometry.RelativeRotationDomain.DiscreteRadians)
                HashWord(_Hash, std::bit_cast<std::uint64_t>(_Angle));
        }
        const auto _HashDomains = [&](const std::vector<AngleDomain>& Domains_)
        {
            HashWord(_Hash, Domains_.size());
            for (const auto& _Domain : Domains_)
            {
                HashWord(_Hash, _Domain.Intervals.size());
                for (const auto& _Interval : _Domain.Intervals)
                {
                    HashWord(_Hash,
                        std::bit_cast<std::uint64_t>(_Interval.MinimumRadians));
                    HashWord(_Hash,
                        std::bit_cast<std::uint64_t>(_Interval.MaximumRadians));
                }
                HashWord(_Hash, _Domain.DiscreteRadians.size());
                for (const auto _Angle : _Domain.DiscreteRadians)
                    HashWord(_Hash, std::bit_cast<std::uint64_t>(_Angle));
            }
        };
        _HashDomains(Input_.DirectedPairDomains);
        _HashDomains(Input_.OrientedPairDomains);
        std::ostringstream _Text;
        _Text << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << _Hash;
        return _Text.str();
    }

    std::optional<double> Number(const Variant& Value_)
    {
        return std::visit([](const auto& Item_) -> std::optional<double> {
            using T = std::decay_t<decltype(Item_)>;
            if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
                return static_cast<double>(Item_);
            return {};
        }, Value_.m_Value);
    }

    bool SameJson(const Variant& Left_, const Variant& Right_)
    {
        const auto _LeftNumber = Number(Left_);
        const auto _RightNumber = Number(Right_);
        if (_LeftNumber || _RightNumber)
            return _LeftNumber && _RightNumber && std::isfinite(*_LeftNumber)
                && std::isfinite(*_RightNumber) && *_LeftNumber == *_RightNumber;
        if (Left_.Is<ObjectMap>() && Right_.Is<ObjectMap>())
        {
            const auto _Left = Left_.To<ObjectMap>();
            const auto _Right = Right_.To<ObjectMap>();
            if (_Left.size() != _Right.size()) return false;
            for (const auto& [_Key, _Value] : _Left)
            {
                const auto _Found = _Right.find(_Key);
                if (_Found == _Right.end() || !SameJson(_Value, _Found->second)) return false;
            }
            return true;
        }
        if (Left_.Is<VariantArray>() && Right_.Is<VariantArray>())
        {
            const auto _Left = Left_.To<VariantArray>();
            const auto _Right = Right_.To<VariantArray>();
            if (_Left.size() != _Right.size()) return false;
            for (std::size_t _Index = 0; _Index < _Left.size(); ++_Index)
                if (!SameJson(_Left[_Index], _Right[_Index])) return false;
            return true;
        }
        return iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Left_)
            == iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Right_);
    }

    Length Quantize(const double Mm_, const bool RoundUp_)
    {
        if (!std::isfinite(Mm_) || Mm_ < 0.0 || Mm_ > kMaximumLength)
            throw std::invalid_argument("排样长度必须是 0 至 1000000 mm 范围内的有限数值");
        // Remove only floating-point noise at exact 0.01 mm boundaries.
        return static_cast<Length>(RoundUp_
            ? std::ceil(Mm_ * kUnitsPerMm - 1e-6)
            : std::floor(Mm_ * kUnitsPerMm + 1e-6));
    }

    Length QuantizeProjection(const double Mm_)
    {
        // Never round an overlap allowance up: a one-unit optimistic projection
        // could make the generated STEP solids cross each other.
        return Quantize(Mm_, false);
    }

    double Mm(const Length Value_) { return static_cast<double>(Value_) / kUnitsPerMm; }
    unsigned long long Count(const std::size_t Value_) { return static_cast<unsigned long long>(Value_); }

    PairTableSolverSettings Settings(const std::size_t Count_)
    {
        PairTableSolverSettings _Settings;
        _Settings.Constraint = ConstraintMode::HybridLargeNeighborhood;
        _Settings.ConstructionRuns = Count_ > 200 ? 2 : 6;
        _Settings.LocalSearchPasses = Count_ > 200 ? 1 : 3;
        _Settings.CandidateWidth = Count_ > 200 ? 16 : 32;
        _Settings.LargeNeighborhoodIterations = Count_ > 200 ? 8 : 32;
        _Settings.ExactPartLimit = 9;
        _Settings.ExactNodeLimit = 50000;
        _Settings.FiniteInventoryLabelLimit = 200000;
        _Settings.EvaluationLimit = Count_ > 200 ? 500000 : 2000000;
        _Settings.ConstraintTimeBudgetMilliseconds = 2000;
        _Settings.ConstraintNeighborhoodPartLimit = 9;
        _Settings.ConstraintNeighborhoodTimeBudgetMilliseconds = Count_ > 200 ? 75 : 150;
        _Settings.TimeBudgetMilliseconds = Count_ > 200 ? 15000 : 0;
        return _Settings;
    }

    bool Complete(const SolveResult& Result_)
    {
        return Result_.Status == SolveStatus::Optimal || Result_.Status == SolveStatus::Feasible;
    }

    PeriodicEndProfile FlatProfile(const double Position_)
    {
        FourierLevel _Level;
        _Level.Constant = Position_;
        _Level.Certified = true;
        PeriodicEndProfile _Profile;
        _Profile.SourceHash = "conservative-aabb-plane";
        _Profile.SourceQuality = ProfileSourceQuality::ApproximateCertified;
        _Profile.CompleteSingleValued = true;
        _Profile.Levels.push_back(std::move(_Level));
        return _Profile;
    }

    PairTypeGeometry FlatGeometry(const Length Length_)
    {
        PairTypeGeometry _Result;
        _Result.Left = FlatProfile(0.0);
        _Result.Right = FlatProfile(static_cast<double>(Length_));
        return _Result;
    }

    bool CertifiedForProductionSavings(const PairTypeGeometry& Geometry_)
    {
        const auto _Certified = [](const PeriodicEndProfile& Profile_)
        {
            return Profile_.CompleteSingleValued && !Profile_.Levels.empty()
                && Profile_.SourceQuality != ProfileSourceQuality::Missing
                && Profile_.SourceQuality != ProfileSourceQuality::NumericOnly;
        };
        return _Certified(Geometry_.Left) && _Certified(Geometry_.Right);
    }

    bool HasLegacyFeatureGeometry(const SNestingPart& Part_)
    {
        for (const auto& _Variant : Part_.Variants)
            if (IsValidCutLineFeature(_Variant.LeftEnd.Feature)
                && IsValidCutLineFeature(_Variant.RightEnd.Feature))
                return true;
        return false;
    }

    // Stock-shortage fallback asks the same table-only solver for progressively
    // larger shortest-first subsets.  Geometry is never re-evaluated here.
    SolveResult SolveAvailableSubset(
        PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
        const PairTableSolverSettings& Settings_)
    {
        std::vector<std::size_t> _Instances;
        for (std::size_t _Type = 0; _Type < Table_.Types.size(); ++_Type)
            _Instances.insert(_Instances.end(), Table_.Types[_Type].Quantity, _Type);
        std::stable_sort(_Instances.begin(), _Instances.end(), [&](const auto Left_, const auto Right_)
        {
            return Table_.Types[Left_].MaximumLength < Table_.Types[Right_].MaximumLength;
        });
        auto _Quick = Settings_;
        _Quick.Constraint = ConstraintMode::Disabled;
        _Quick.ConstructionRuns = 1;
        _Quick.LocalSearchPasses = 0;
        _Quick.LargeNeighborhoodIterations = 0;
        const auto _Select = [&](const std::size_t Size_)
        {
            for (auto& _Type : Table_.Types) _Type.Quantity = 0;
            for (std::size_t _Index = 0; _Index < Size_; ++_Index)
                ++Table_.Types[_Instances[_Index]].Quantity;
        };
        std::size_t _Low = 0;
        std::size_t _High = _Instances.size();
        SolveResult _Best;
        while (_Low < _High)
        {
            const auto _Middle = _Low + (_High - _Low + 1) / 2;
            _Select(_Middle);
            auto _Candidate = PairTableSolver().Solve(Table_, Stocks_, _Quick);
            if (Complete(_Candidate))
            {
                _Low = _Middle;
                _Best = std::move(_Candidate);
            }
            else _High = _Middle - 1;
        }
        if (_Low != 0)
        {
            _Select(_Low);
            auto _Optimized = PairTableSolver().Solve(Table_, Stocks_, Settings_);
            if (Complete(_Optimized)) _Best = std::move(_Optimized);
        }
        return _Best;
    }
}

bool IsTubeManufacturingPart(const ObjectMap& Properties_)
{
    if (Properties_.contains("manufacturing.plate")) return false;
    for (const auto* _Name : { "manufacturing.sourcing", "manufacturing.process" })
    {
        const auto _It = Properties_.find(_Name);
        if (_It == Properties_.end()) continue;
        if (!_It->second.Is<std::string>()) return false;
        auto _Value = _It->second.To<std::string>();
        const auto _First = _Value.find_first_not_of(" \t\r\n");
        if (_First == std::string::npos) continue;
        _Value = _Value.substr(_First, _Value.find_last_not_of(" \t\r\n") - _First + 1);
        std::transform(_Value.begin(), _Value.end(), _Value.begin(), [](unsigned char C_) {
            return static_cast<char>(std::tolower(C_));
        });
        if (_Value == "purchased" || _Value == "bent" || _Value == "curved"
            || _Value == "bending" || _Value == "tube-bending" || _Value == "bent-tube"
            || _Value == "curved-tube") return false;
    }
    if (const auto _It = Properties_.find("manufacturing.requiresBending"); _It != Properties_.end())
        if (!_It->second.Is<bool>() || _It->second.To<bool>()) return false;
    for (const auto* _Name : { "manufacturing.partKind", "manufacturing.materialCategory" })
    {
        const auto _Found = Properties_.find(_Name);
        if (_Found == Properties_.end()) continue;
        if (!_Found->second.Is<std::string>()) return false;
        auto _Kind = _Found->second.To<std::string>();
        const auto _First = _Kind.find_first_not_of(" \t\r\n");
        if (_First == std::string::npos) return false;
        _Kind = _Kind.substr(_First, _Kind.find_last_not_of(" \t\r\n") - _First + 1);
        std::transform(_Kind.begin(), _Kind.end(), _Kind.begin(), [](unsigned char Character_) {
            return static_cast<char>(std::tolower(Character_));
        });
        if (_Kind != "tube" && _Kind != "linear" && _Kind != "profile") return false;
    }
    return true;
}

bool MatchesNestingProfileKey(
    const std::string& Key_, const ObjectMap& Profile_, const std::string& PartID_)
{
    ObjectMap _Section;
    for (const auto* _Field : {
        "id", "kind", "packageVersion", "width", "depth", "diameter", "wallThickness",
        "cornerRadius", "hollow", "contentDigest", "parameters", "contours", "specification" })
    {
        const auto _Found = Profile_.find(_Field);
        if (_Found == Profile_.end() || _Found->second.Is<std::monostate>()) continue;
        if (_Found->second.Is<std::string>() && _Found->second.To<std::string>().empty()) continue;
        _Section[_Field] = _Found->second;
    }
    if (!_Section.contains("id") && !_Section.contains("kind"))
    {
        const auto _Name = Profile_.find("displayName");
        if (_Name != Profile_.end() && _Name->second.Is<std::string>())
        {
            auto _Text = _Name->second.To<std::string>();
            const auto _First = _Text.find_first_not_of(" \t\r\n");
            if (_First != std::string::npos)
                _Section["displayName"] = _Text.substr(_First, _Text.find_last_not_of(" \t\r\n") - _First + 1);
        }
        // buildProfileGroups adds an empty displayName even alongside unnamed dimensions.
        if (!_Section.empty() && !_Section.contains("displayName")) _Section["displayName"] = std::string();
    }
    if (_Section.empty()) return Key_ == "unknown:" + PartID_;
    try { return SameJson(Variant(_Section), iCAX::TemplateRuntime::CStandardJsonCodec::Parse(Key_)); }
    catch (const std::exception&) { return false; }
}

ObjectMap NormalizeNestingSettings(const ObjectMap& Settings_)
{
    const auto _Version = Settings_.find("version");
    if (_Version == Settings_.end() || Number(_Version->second) != 2.0)
        throw std::invalid_argument("不支持的排样设置版本");
    const auto _Stocks = Settings_.find("stocks");
    const auto _Parameters = Settings_.find("parameters");
    if (_Stocks == Settings_.end() || !_Stocks->second.Is<VariantArray>()
        || _Parameters == Settings_.end() || !_Parameters->second.Is<ObjectMap>())
        throw std::invalid_argument("排样设置必须包含母材列表和参数对象");
    const auto _ParameterMap = _Parameters->second.To<ObjectMap>();
    const auto _GapField = _ParameterMap.find("partGap");
    const auto _Gap = _GapField == _ParameterMap.end() ? std::optional<double>(0.0) : Number(_GapField->second);
    if (!_Gap) throw std::invalid_argument("零件间距必须是数值");
    Quantize(*_Gap, true);
    const auto _Groups = _Stocks->second.To<VariantArray>();
    if (_Groups.size() > 1000) throw std::invalid_argument("母材截面组不能超过 1000 组");
    VariantArray _NormalizedGroups;
    std::set<std::string> _ProfileKeys, _RowIDs;
    std::size_t _RowCount = 0;
    const auto _Text = [](const ObjectMap& Object_, const std::string& Field_, const std::size_t Limit_) {
        const auto _Found = Object_.find(Field_);
        if (_Found == Object_.end() || !_Found->second.Is<std::string>())
            throw std::invalid_argument("母材设置缺少文本字段: " + Field_);
        const auto _Value = _Found->second.To<std::string>();
        if (_Value.empty() || _Value.size() > Limit_)
            throw std::invalid_argument("母材设置文本为空或过长: " + Field_);
        return _Value;
    };
    for (const auto& _GroupValue : _Groups)
    {
        if (!_GroupValue.Is<ObjectMap>()) throw std::invalid_argument("母材截面组必须是对象");
        const auto _Group = _GroupValue.To<ObjectMap>();
        const auto _ProfileKey = _Text(_Group, "profileKey", 65536);
        if (!_ProfileKeys.insert(_ProfileKey).second) throw std::invalid_argument("母材截面组重复");
        const auto _RowsField = _Group.find("rows");
        if (_RowsField == _Group.end() || !_RowsField->second.Is<VariantArray>())
            throw std::invalid_argument("母材截面组必须包含行列表");
        VariantArray _Rows;
        for (const auto& _RowValue : _RowsField->second.To<VariantArray>())
        {
            if (++_RowCount > 1000) throw std::invalid_argument("母材设置不能超过 1000 行");
            if (!_RowValue.Is<ObjectMap>()) throw std::invalid_argument("母材行必须是对象");
            const auto _Row = _RowValue.To<ObjectMap>();
            const auto _ID = _Text(_Row, "id", 160);
            if (!_RowIDs.insert(_ID).second) throw std::invalid_argument("母材行编号重复");
            const auto _QuantityField = _Row.find("quantity");
            const auto _Quantity = _QuantityField == _Row.end() ? std::optional<double>() : Number(_QuantityField->second);
            if (!_Quantity || !std::isfinite(*_Quantity) || std::floor(*_Quantity) != *_Quantity
                || *_Quantity < -1.0 || *_Quantity > 1000000.0)
                throw std::invalid_argument("母材数量必须为 -1（不限量）、0（停用）或正整数，最多 1000000");
            const auto _LengthField = _Row.find("length");
            if (_LengthField == _Row.end()) throw std::invalid_argument("母材长度不能为空");
            if (*_Quantity == 0 && _LengthField->second.Is<std::string>()
                && _LengthField->second.To<std::string>().empty())
            {
                _Rows.emplace_back(ObjectMap{ { "id", _ID }, { "length", std::string() }, { "quantity", 0ll } });
                continue;
            }
            const auto _Length = Number(_LengthField->second);
            if (!_Length || Quantize(*_Length, false) <= 0)
                throw std::invalid_argument("母材长度必须为 0.01 至 1000000 mm 的数值");
            _Rows.emplace_back(ObjectMap{ { "id", _ID }, { "length", *_Length },
                { "quantity", static_cast<long long>(*_Quantity) } });
        }
        _NormalizedGroups.emplace_back(ObjectMap{ { "profileKey", _ProfileKey }, { "rows", std::move(_Rows) } });
    }
    return { { "version", 2ll }, { "stocks", std::move(_NormalizedGroups) },
        { "parameters", ObjectMap{ { "partGap", *_Gap } } } };
}

ObjectMap SolveManufacturingNesting(
    const std::vector<SNestingPart>& Parts_, const std::vector<SNestingStock>& Stocks_, const double PartGap_)
{
    return SolveManufacturingNesting(Parts_, Stocks_, PartGap_, 0);
}

namespace
{
ObjectMap SolveManufacturingNestingSinglePriority(
    const std::vector<SNestingPart>& Parts_, const std::vector<SNestingStock>& Stocks_,
    const double PartGap_, const std::size_t MaximumConcurrency_)
{
    if (Parts_.empty()) throw std::invalid_argument("请先勾选需要排样的零件");
    const auto _OrdinaryStockCount = std::ranges::count_if(
        Stocks_, [](const auto& Stock_) { return Stock_.InstanceID.empty(); });
    if (_OrdinaryStockCount > 1000 || Stocks_.size() > 3000)
        throw std::invalid_argument("母材规格不能超过 1000 行，续排实例不能超过 2000 根");
    const auto _Gap = Quantize(PartGap_, true);
    std::set<std::string> _ContinuationPredecessors;
    for (const auto& _Stock : Stocks_)
        if (!_Stock.InstanceID.empty())
        {
            if (_Stock.InitialPredecessorPartID.empty())
                throw std::invalid_argument("异形续排母材缺少末件编号");
            _ContinuationPredecessors.insert(_Stock.InitialPredecessorPartID);
        }
    std::size_t _TotalParts = 0;
    std::set<std::string> _IDs;
    std::map<std::string, std::array<double, 3>> _LocalCenters;
    std::map<std::string, std::string> _PartProfiles;
    std::map<std::string, std::uint32_t> _PartPriorities;
    std::map<std::string, PairTableBuildInput> _Groups;
    std::size_t _RecognizedProfileCount = 0;
    std::size_t _RecognizedNumericProfileCount = 0;
    std::size_t _LegacyBridgeCount = 0;
    std::size_t _ConservativeProfileCount = 0;
    bool _AllProfilesNativeCertified = true;
    for (const auto& _Part : Parts_)
    {
        if (_Part.ID.empty() || _Part.ProfileKey.empty() || !_IDs.insert(_Part.ID).second)
            throw std::invalid_argument("零件编号、截面分组为空或零件重复");
        if ((_Part.Quantity == 0 && !_ContinuationPredecessors.contains(_Part.ID))
            || _Part.Quantity > kMaximumPartInstances - _TotalParts)
            throw std::invalid_argument(
                "单次排样零件数量无效；零数量类型只能作为异形续排的固定末件");
        _TotalParts += _Part.Quantity;
        if (!std::ranges::all_of(_Part.LocalCenter,
            [](const double Value_) { return std::isfinite(Value_); }))
            throw std::invalid_argument("零件局部坐标中心无效");
        _LocalCenters.emplace(_Part.ID, _Part.LocalCenter);
        _PartProfiles.emplace(_Part.ID, _Part.ProfileKey);
        _PartPriorities.emplace(_Part.ID, _Part.Priority);
        const auto _Length = Quantize(_Part.Length, true);
        if (_Length <= 0) throw std::invalid_argument("零件长度必须大于 0 mm");
        if (!_Part.DirectionAllowed[0] && !_Part.DirectionAllowed[1])
            throw std::invalid_argument("零件正向和掉头方向不能同时禁用");
        Length _MaterialLength = _Length;
        if (!_Part.Variants.empty())
        {
            if (_Part.Variants.size() > 16)
                throw std::invalid_argument("单个零件的排样姿态不能超过 16 种");
            std::set<std::string> _VariantIDs;
            bool _HasMaterialLength = false;
            for (const auto& _Source : _Part.Variants)
            {
                if (_Source.ID.empty() || !_VariantIDs.insert(_Source.ID).second
                    || !std::isfinite(_Source.RotationRadians)
                    || !std::isfinite(_Source.PhaseOffset))
                    throw std::invalid_argument("零件排样姿态编号、旋转角或相位偏移无效");
                const auto _VariantLength = Quantize(_Source.EnvelopeLength, true);
                if (_VariantLength != _Length)
                    throw std::invalid_argument("零件排样姿态的轴向包络与制造几何不一致");
                const auto _VariantMaterial = (std::min)(
                    _VariantLength, Quantize(_Source.MaterialLength, true));
                if (_VariantMaterial <= 0 || _VariantMaterial > _VariantLength)
                    throw std::invalid_argument("零件排样姿态的净材料长度无效");
                if (!_HasMaterialLength)
                {
                    _MaterialLength = _VariantMaterial;
                    _HasMaterialLength = true;
                }
                else
                    _MaterialLength = (std::max)(_MaterialLength, _VariantMaterial);
                const auto _ValidateEnd = [](const SNestingEnd& Source_) {
                    (void)QuantizeProjection(Source_.Projection);
                    if (Source_.Feature.Kind != CutLineFeatureKind::Invalid)
                    {
                        if (!IsValidCutLineFeature(Source_.Feature))
                            throw std::invalid_argument("端曲线特征编码无效");
                    }
                    if (Source_.NestingPlane.size() > 160)
                        throw std::invalid_argument("斜切端面方向编号过长");
                };
                _ValidateEnd(_Source.LeftEnd);
                _ValidateEnd(_Source.RightEnd);
                if (!std::isfinite(_Source.PhaseOffset) || _Source.PhaseOffset < 0.0
                    || _Source.PhaseOffset > kMaximumLength)
                    throw std::invalid_argument("零件展开相位偏移无效");
            }
        }
        PairTypeGeometry _Geometry;
        if (_Part.PairGeometry)
        {
            ++_RecognizedProfileCount;
            if (!CertifiedForProductionSavings(*_Part.PairGeometry))
            {
                ++_RecognizedNumericProfileCount;
                _AllProfilesNativeCertified = false;
                _Geometry = FlatGeometry(_Length);
            }
            else
            {
                _Geometry = *_Part.PairGeometry;
                if (_Geometry.Left.SourceQuality != ProfileSourceQuality::NativeCertified
                    || _Geometry.Right.SourceQuality != ProfileSourceQuality::NativeCertified)
                    _AllProfilesNativeCertified = false;
            }
        }
        else if (HasLegacyFeatureGeometry(_Part))
        {
            _Geometry = FlatGeometry(_Length);
            ++_LegacyBridgeCount;
            _AllProfilesNativeCertified = false;
        }
        else
        {
            _Geometry = FlatGeometry(_Length);
            ++_ConservativeProfileCount;
            _AllProfilesNativeCertified = false;
        }
        auto& _Group = _Groups[_Part.ProfileKey];
        _Group.Version = "tube-nesting-pair-table-v1";
        _Group.Types.push_back(PairPartType{
            _Part.ID, _Part.ProfileKey, {}, _Length, _MaterialLength,
            _Part.Quantity, _Part.DirectionAllowed });
        _Group.Geometry.push_back(std::move(_Geometry));
    }
    if (_TotalParts == 0)
        throw std::invalid_argument("请至少提供一个尚未排样的零件");
    for (const auto& _Predecessor : _ContinuationPredecessors)
        if (!_PartProfiles.contains(_Predecessor))
            throw std::invalid_argument("异形续排母材的末件不在零件定义中");
    _IDs.clear();
    std::map<std::string, std::vector<StockType>> _StocksByProfile;
    for (const auto& _Stock : Stocks_)
    {
        if (_Stock.ID.empty() || _Stock.ProfileKey.empty() || !_IDs.insert(_Stock.ID).second)
            throw std::invalid_argument("母材编号、截面分组为空或母材编号重复");
        if (_Stock.Quantity < -1 || _Stock.Quantity > 1000000)
            throw std::invalid_argument("母材数量必须为 -1（不限量）、0（停用）或正整数");
        const bool _Continued = !_Stock.InstanceID.empty();
        if (_Continued)
        {
            if (_Stock.Quantity != 1 || _Stock.SourceStockTypeID.empty()
                || !_ContinuationPredecessors.contains(
                    _Stock.InitialPredecessorPartID)
                || _PartProfiles.at(_Stock.InitialPredecessorPartID)
                    != _Stock.ProfileKey
                || !std::isfinite(_Stock.InitialProcessedEnd)
                || !std::isfinite(_Stock.InitialPredecessorRotationRadians))
                throw std::invalid_argument("异形续排母材的实例、末件或姿态无效");
        }
        else if (!_Stock.SourceStockTypeID.empty()
            || _Stock.InitialProcessedEnd != 0.0
            || !_Stock.InitialPredecessorPartID.empty()
            || _Stock.InitialPredecessorReversed
            || _Stock.InitialPredecessorRotationRadians != 0.0)
            throw std::invalid_argument("普通母材不能携带固定前缀信息");
        if (_Stock.Quantity == 0) continue;
        const auto _Length = Quantize(_Stock.Length, false);
        if (_Length <= 0) throw std::invalid_argument("母材长度必须至少为 0.01 mm");
        StockType _Native;
        _Native.ID = _Stock.ID;
        _Native.SourceTypeID = _Stock.SourceStockTypeID;
        _Native.FixedInstanceID = _Stock.InstanceID;
        _Native.CompatibilityKey = _Stock.ProfileKey;
        _Native.TotalLength = _Length;
        _Native.Quantity = _Stock.Quantity == -1 ? 0 : static_cast<std::size_t>(_Stock.Quantity);
        if (_Continued)
        {
            _Native.Kind = StockKind::Remnant;
            _Native.CostUnits = 0;
            _Native.Quantity = 1;
            _Native.InitialProcessedEnd = Quantize(
                _Stock.InitialProcessedEnd, true);
            if (_Native.InitialProcessedEnd < 0
                || _Native.InitialProcessedEnd >= _Native.TotalLength)
                throw std::invalid_argument("异形续排母材没有可用的剩余长度");
            _Native.InitialPredecessorTypeID =
                _Stock.InitialPredecessorPartID;
            _Native.InitialPredecessorDirection = _Stock.InitialPredecessorReversed
                ? Direction::Reverse : Direction::Forward;
            _Native.InitialPredecessorRotationRadians =
                _Stock.InitialPredecessorRotationRadians;
        }
        _StocksByProfile[_Stock.ProfileKey].push_back(std::move(_Native));
    }

    struct SGroupResult
    {
        VariantArray Plans;
        std::map<std::string, std::size_t> PlacedQuantities;
        std::map<std::string, std::string> MissingReasons;
        Length TotalStock = 0, PartLength = 0, UsedLength = 0;
        std::size_t PlacedCount = 0;
        bool AllExact = true;
        unsigned long long SearchNodes = 0;
        std::vector<std::string> SolverDiagnostics;
    };
    const auto _Hardware = std::thread::hardware_concurrency();
    const std::size_t _GroupConcurrency = MaximumConcurrency_ == 0
        ? std::min(8u, std::max(1u, _Hardware > 1 ? _Hardware - 1 : 1u))
        : std::clamp<std::size_t>(MaximumConcurrency_, 1, 8);
    const bool _MultipleProfileGroups = _Groups.size() > 1;
    const auto _SolveGroup = [_Gap, &_LocalCenters, &_PartPriorities, _MultipleProfileGroups](const PairTableBuildInput& _SourceInput,
        const std::vector<StockType>& _Stocks) {
        SGroupResult _GroupResult;
        auto& [_Plans, _PlacedQuantities, _MissingReasons, _TotalStock,
            _PartLength, _UsedLength, _PlacedCount, _AllExact, _SearchNodes,
            _SolverDiagnostics] = _GroupResult;
        Length _MaximumStock = 0;
        for (const auto& _Stock : _Stocks) _MaximumStock = std::max(_MaximumStock, _Stock.TotalLength);
        PairTableBuildInput _Input;
        _Input.Version = _SourceInput.Version;
        _Input.TypeDefinitionHash = _SourceInput.TypeDefinitionHash;
        _Input.ProcessConfigurationHash = _SourceInput.ProcessConfigurationHash;
        for (std::size_t _Type = 0; _Type < _SourceInput.Types.size(); ++_Type)
        {
            const auto& _Definition = _SourceInput.Types[_Type];
            if (_MaximumStock < _Definition.MaximumLength)
            {
                _MissingReasons[_Definition.ID] = _Stocks.empty()
                    ? "该截面未设置可用母材" : "零件长于该截面的所有可用母材";
                _AllExact = false;
            }
            else
            {
                _Input.Types.push_back(_Definition);
                _Input.Geometry.push_back(_SourceInput.Geometry[_Type]);
            }
        }
        if (_Input.Types.empty()) return _GroupResult;

        const auto _StateCount = _Input.Types.size() * 2;
        _Input.PairProcessLosses.assign(_StateCount * _StateCount, _Gap);
        _Input.TypeDefinitionHash = TypeDefinitionFingerprint(_Input);
        _Input.ProcessConfigurationHash = "pair-gap-v1:"
            + std::to_string(_Gap);
        PairTableBuildSettings _BuildSettings;
        _BuildSettings.Pose.Tolerance = 1.0; // one 0.01 mm integer quantum
        _BuildSettings.Pose.FixedPhaseTolerance = 0.25;
        // Profile groups are already parallelized outside.  For one profile,
        // use the pair-table's bounded internal worker pool.
        _BuildSettings.MaximumConcurrency = _MultipleProfileGroups ? 1 : 0;
        auto _Table = BuildPairTable(_Input, _BuildSettings);
        std::vector<std::string> _TableDiagnostics;
        if (!ValidatePairTable(_Table, &_TableDiagnostics))
            throw std::runtime_error("端口姿态表无效，无法执行排样");
        const bool _TableCertified = std::ranges::all_of(
            _Table.Entries, [](const auto& Entry_)
            {
                return Entry_.Status == PairEntryStatus::Forbidden
                    || Entry_.Status == PairEntryStatus::Certified;
            });
        _AllExact = _AllExact && _TableCertified;
        std::vector<std::size_t> _RequestedQuantities;
        _RequestedQuantities.reserve(_Table.Types.size());
        for (const auto& _Type : _Table.Types)
            _RequestedQuantities.push_back(_Type.Quantity);

        // Inventory has hard priority: solve finite stock first, then only send its
        // unfulfilled demands to unlimited stock. Cost/count tie-breakers cannot
        // bypass this ordering.
        std::vector<StockType> _FiniteStocks, _UnlimitedStocks;
        for (const auto& _Stock : _Stocks)
            (_Stock.Quantity == 0 ? _UnlimitedStocks : _FiniteStocks).push_back(_Stock);
        for (const auto& _PhaseStocks : { _FiniteStocks, _UnlimitedStocks })
        {
        if (_PhaseStocks.empty()) continue;
        Length _PhaseMaximum = 0;
        for (const auto& _Stock : _PhaseStocks) _PhaseMaximum = std::max(_PhaseMaximum, _Stock.TotalLength);
        std::vector<std::size_t> _PhaseQuantities(_Table.Types.size(), 0);
        std::size_t _PhaseCount = 0;
        for (std::size_t _Type = 0; _Type < _Table.Types.size(); ++_Type)
        {
            const auto& _Definition = _Table.Types[_Type];
            const auto _Placed = _PlacedQuantities[_Definition.ID];
            const auto _Requested = _RequestedQuantities[_Type];
            const auto _Remaining = _Requested - (std::min)(_Requested, _Placed);
            if (_Remaining && _Definition.MaximumLength <= _PhaseMaximum)
            {
                _PhaseQuantities[_Type] = _Remaining;
                _PhaseCount += _Remaining;
            }
        }
        if (_PhaseCount == 0) continue;
        for (std::size_t _Type = 0; _Type < _Table.Types.size(); ++_Type)
            _Table.Types[_Type].Quantity = _PhaseQuantities[_Type];
        auto _Settings = Settings(_PhaseCount);
        _Settings.ConstraintWorkers = _MultipleProfileGroups ? 1 : 0;
        auto _Result = PairTableSolver().Solve(_Table, _PhaseStocks, _Settings);
        if (_Result.Status == SolveStatus::InvalidInput)
            throw std::runtime_error("原生排样求解器拒绝了输入数据");
        if (!Complete(_Result))
        {
            _Result = SolveAvailableSubset(_Table, _PhaseStocks, _Settings);
            _AllExact = false;
        }
        _AllExact = _AllExact && _Result.ExactSearchCompleted;
        _SearchNodes += _Result.Metrics.SearchNodes;
        _SolverDiagnostics.insert(_SolverDiagnostics.end(),
            _Result.Diagnostics.begin(), _Result.Diagnostics.end());
        for (const auto& _Stock : _Result.Stocks)
        {
            VariantArray _Placements;
            for (const auto& _Placement : _Stock.Placements)
            {
                ++_PlacedQuantities[_Placement.DemandID];
                ++_PlacedCount;
                std::ostringstream _InstanceID;
                _InstanceID << _Placement.DemandID << '#' << std::setw(4) << std::setfill('0')
                    << _PlacedQuantities[_Placement.DemandID];
                _Placements.emplace_back(ObjectMap{
                    { "partId", _Placement.DemandID }, { "instanceId", _InstanceID.str() },
                    { "priority", static_cast<unsigned long long>(
                        _PartPriorities.at(_Placement.DemandID)) },
                    { "start", Mm(_Placement.Start) }, { "end", Mm(_Placement.End) },
                    { "length", Mm(_Placement.End - _Placement.Start) },
                    { "gapBefore", Mm(_Placement.GapBefore) },
                    // Continued stock has a fixed predecessor before the first
                    // newly returned placement.
                    { "nestedWithPrevious", (!_Placements.empty() || _Stock.Continued)
                        && _Placement.GapBefore < _Gap },
                    { "variantId", _Placement.VariantID },
                    { "reversed", _Placement.Reversed },
                    { "phaseOffset", 0.0 },
                    { "rotationRadians", _Placement.RotationRadians },
                    { "savingFromPrevious", Mm(_Placement.SavingFromPrevious) },
                    { "pairPoseIndex", static_cast<unsigned long long>(
                        _Placement.PairPoseMetadataIndex) },
                    { "trsf", MakeNestingPlacementTransform(_LocalCenters.at(_Placement.DemandID),
                        Mm(_Placement.Start), Mm(_Placement.End), _Placement.Reversed,
                        _Placement.RotationRadians) }
                });
            }
            _Plans.emplace_back(ObjectMap{
                { "id", _Stock.StockInstanceID }, { "stockTypeId", _Stock.StockTypeID },
                { "profileKey", _Stock.CompatibilityKey }, { "stockLength", Mm(_Stock.TotalLength) },
                { "continued", _Stock.Continued },
                { "initialProcessedEnd", Mm(_Stock.InitialProcessedEnd) },
                { "usedLength", Mm(_Stock.ProcessedEnd) }, { "remainingLength", Mm(_Stock.RemainingLength) },
                { "partLength", Mm(_Stock.PartLength) }, { "partCount", Count(_Stock.Placements.size()) },
                { "utilization", static_cast<double>(_Stock.PartLength) / _Stock.TotalLength },
                { "placements", std::move(_Placements) }
            });
            _TotalStock += _Stock.TotalLength;
            _PartLength += _Stock.PartLength;
            _UsedLength += _Stock.ProcessedEnd;
        }
        }
        if (!_FiniteStocks.empty() && !_UnlimitedStocks.empty()) _AllExact = false;
        return _GroupResult;
    };

    // Prepare read-only group inputs before launching workers. A group's finite
    // inventory, fallback search and unlimited-stock phase stay on one task.
    struct SGroupInput
    {
        const PairTableBuildInput* Input;
        const std::vector<StockType>* Stocks;
    };
    std::vector<SGroupInput> _Inputs;
    for (const auto& [_Profile, _Input] : _Groups)
        _Inputs.push_back({ &_Input, &_StocksByProfile[_Profile] });
    std::vector<SGroupResult> _Results(_Inputs.size());
    const auto _RunGroup = [&](std::size_t Index_) {
        _Results[Index_] = _SolveGroup(*_Inputs[Index_].Input, *_Inputs[Index_].Stocks);
    };
    for (std::size_t _Begin = 0; _Begin < _Inputs.size(); _Begin += _GroupConcurrency)
    {
        const auto _Count = std::min(_GroupConcurrency, _Inputs.size() - _Begin);
        struct SBatch
        {
            std::vector<iCAX::Tasks::Task<void>> Tasks;
            ~SBatch() { for (const auto& _Task : Tasks) _Task.Wait(); }
        } _Batch;
        _Batch.Tasks.reserve(_Count - 1);
        if (_Count > 1)
        {
            // A separate reusable Task pool avoids waiting on the default pool
            // when nesting itself was invoked by one of its workers.
            static auto _Scheduler = std::make_shared<iCAX::Tasks::ThreadPoolTaskScheduler>(8);
            for (std::size_t _Offset = 1; _Offset < _Count; ++_Offset)
            {
                const auto _Index = _Begin + _Offset;
                _Batch.Tasks.push_back(iCAX::Tasks::Run([&, _Index] { _RunGroup(_Index); }, _Scheduler));
            }
        }
        _RunGroup(_Begin);
        for (const auto& _Task : _Batch.Tasks) _Task.Wait();
        for (const auto& _Task : _Batch.Tasks) _Task.Result();
    }

    // Merge in the original profile order, never completion order. Workers do
    // not touch aggregate counters, resource libraries, inventory or the scene.
    SGroupResult _Aggregate;
    auto& [_Plans, _PlacedQuantities, _MissingReasons, _TotalStock,
        _PartLength, _UsedLength, _PlacedCount, _AllExact, _SearchNodes,
        _SolverDiagnostics] = _Aggregate;
    VariantArray _Unplaced, _Diagnostics;
    for (auto& _Result : _Results)
    {
        _Plans.insert(_Plans.end(), std::make_move_iterator(_Result.Plans.begin()),
            std::make_move_iterator(_Result.Plans.end()));
        _PlacedQuantities.merge(_Result.PlacedQuantities);
        _MissingReasons.merge(_Result.MissingReasons);
        _TotalStock += _Result.TotalStock;
        _PartLength += _Result.PartLength;
        _UsedLength += _Result.UsedLength;
        _PlacedCount += _Result.PlacedCount;
        _AllExact = _AllExact && _Result.AllExact;
        _SearchNodes += _Result.SearchNodes;
        _SolverDiagnostics.insert(_SolverDiagnostics.end(),
            std::make_move_iterator(_Result.SolverDiagnostics.begin()),
            std::make_move_iterator(_Result.SolverDiagnostics.end()));
    }
    for (const auto& _Part : Parts_)
    {
        const auto _Missing = _Part.Quantity - _PlacedQuantities[_Part.ID];
        if (!_Missing) continue;
        const auto _Reason = _MissingReasons.find(_Part.ID);
        _Unplaced.emplace_back(ObjectMap{
            { "partId", _Part.ID }, { "quantity", Count(_Missing) },
            { "reason", _Reason != _MissingReasons.end() ? _Reason->second : std::string("现有母材未能容纳全部零件，请增加母材数量或长度后重算") }
        });
    }
    const auto _Status = !_Unplaced.empty()
        ? (_Plans.empty() ? "infeasible" : "partial") : (_AllExact ? "optimal" : "feasible");
    const auto _StockCount = Count(_Plans.size());
    if (!_Unplaced.empty() && !_Plans.empty())
        _Diagnostics.emplace_back(std::string("已保留可行的部分方案；未排零件需补充母材或调整间距后重算。部分方案不保证全局最优。"));
    std::sort(_SolverDiagnostics.begin(), _SolverDiagnostics.end());
    _SolverDiagnostics.erase(std::unique(
        _SolverDiagnostics.begin(), _SolverDiagnostics.end()), _SolverDiagnostics.end());
    for (auto& _Diagnostic : _SolverDiagnostics)
        _Diagnostics.emplace_back(std::move(_Diagnostic));
    _Diagnostics.emplace_back(std::string(
        "排样已使用四姿态邻接表；顺序求解阶段不再访问 BRep、梯形投影或在线端线匹配。"));
    if (_RecognizedProfileCount)
        _Diagnostics.emplace_back(std::string("已使用管型识别服务提供的周期端口谱：")
            + std::to_string(_RecognizedProfileCount) + " 种零件。");
    if (_RecognizedNumericProfileCount)
        _Diagnostics.emplace_back(std::string("其中 ")
            + std::to_string(_RecognizedNumericProfileCount)
            + " 种只有数值采样谱；为避免未经证明的互穿，本次按轴向包络排样，未使用其重叠节约量。");
    if (_LegacyBridgeCount)
        _Diagnostics.emplace_back(std::string("有 ") + std::to_string(_LegacyBridgeCount)
            + " 种零件只有旧端线采样；本次按轴向包络排样，旧在线相位匹配未进入顺序求解。");
    if (_ConservativeProfileCount)
        _Diagnostics.emplace_back(std::string("有 ") + std::to_string(_ConservativeProfileCount)
            + " 种零件缺少端口谱，已使用不重叠的轴向包络平面，结果安全但可能少节料。");
    return {
        { "status", std::string(_Status) }, { "plans", std::move(_Plans) },
        { "unplaced", std::move(_Unplaced) }, { "diagnostics", std::move(_Diagnostics) },
        { "proof", ObjectMap{
            { "integerModelOptimalityProven", _AllExact },
            { "nativePairBoundsCertified", _AllProfilesNativeCertified },
            { "scope", std::string(_AllExact ? "full-integer-model" : "feasible-incumbent") }
        } },
        { "metrics", ObjectMap{
            { "requestedPartCount", Count(_TotalParts) }, { "placedPartCount", Count(_PlacedCount) },
            { "unplacedPartCount", Count(_TotalParts - _PlacedCount) },
            { "stockCount", _StockCount }, { "totalStockLength", Mm(_TotalStock) },
            { "partLength", Mm(_PartLength) }, { "usedLength", Mm(_UsedLength) },
            { "remainingLength", Mm(_TotalStock - _UsedLength) }, { "gapLength", Mm(_UsedLength - _PartLength) },
            { "utilization", _TotalStock > 0 ? static_cast<double>(_PartLength) / _TotalStock : 0.0 },
            { "searchNodes", _SearchNodes }, { "partGap", Mm(_Gap) }
        } }
    };
}

}

ObjectMap SolveManufacturingNesting(
    const std::vector<SNestingPart>& Parts_, const std::vector<SNestingStock>& Stocks_,
    const double PartGap_, const std::size_t MaximumConcurrency_)
{
    std::set<std::uint32_t> _PriorityLevels;
    for (const auto& _Part : Parts_)
        if (_Part.Quantity) _PriorityLevels.insert(_Part.Priority);
    if (_PriorityLevels.size() <= 1)
        return SolveManufacturingNestingSinglePriority(
            Parts_, Stocks_, PartGap_, MaximumConcurrency_);

    std::set<std::string> _ContinuationPredecessors;
    std::set<std::string> _ReservedPlanIDs;
    for (const auto& _Stock : Stocks_)
    {
        if (_Stock.InstanceID.empty()) continue;
        _ContinuationPredecessors.insert(_Stock.InitialPredecessorPartID);
        if (!_ReservedPlanIDs.insert(_Stock.InstanceID).second)
            throw std::invalid_argument("异形续排母材实例编号重复");
    }
    std::set<std::string> _PartIDs;
    std::size_t _RequestedCount = 0;
    for (const auto& _Part : Parts_)
    {
        if (_Part.ID.empty() || _Part.ProfileKey.empty()
            || !_PartIDs.insert(_Part.ID).second)
            throw std::invalid_argument("零件编号、截面分组为空或零件重复");
        if ((_Part.Quantity == 0 && !_ContinuationPredecessors.contains(_Part.ID))
            || _Part.Quantity > kMaximumPartInstances - _RequestedCount)
            throw std::invalid_argument(
                "单次排样零件数量无效；零数量类型只能作为异形续排的固定末件");
        _RequestedCount += _Part.Quantity;
    }

    const auto _RequiredText = [](const ObjectMap& Object_, const char* Field_)
    {
        const auto _Found = Object_.find(Field_);
        if (_Found == Object_.end() || !_Found->second.Is<std::string>())
            throw std::logic_error(std::string("排样结果缺少文本字段: ") + Field_);
        return _Found->second.To<std::string>();
    };
    const auto _RequiredNumber = [](const ObjectMap& Object_, const char* Field_)
    {
        const auto _Found = Object_.find(Field_);
        if (_Found == Object_.end())
            throw std::logic_error(std::string("排样结果缺少数值字段: ") + Field_);
        const auto _Value = Number(_Found->second);
        if (!_Value || !std::isfinite(*_Value))
            throw std::logic_error(std::string("排样结果数值字段无效: ") + Field_);
        return *_Value;
    };
    const auto _RequiredBool = [](const ObjectMap& Object_, const char* Field_)
    {
        const auto _Found = Object_.find(Field_);
        if (_Found == Object_.end() || !_Found->second.Is<bool>())
            throw std::logic_error(std::string("排样结果缺少布尔字段: ") + Field_);
        return _Found->second.To<bool>();
    };

    std::vector<SNestingStock> _AvailableStocks = Stocks_;
    VariantArray _Plans;
    std::map<std::string, std::size_t> _PlanIndices;
    std::set<std::string> _PlacementIDs;
    std::map<std::string, std::size_t> _PlanOrdinals, _PartOrdinals;
    std::size_t _ContinuationOrdinal = 0;
    unsigned long long _SearchNodes = 0;
    bool _AllExact = true;
    bool _AllBoundsCertified = true;
    std::vector<std::string> _LayerDiagnostics;
    std::set<std::string> _SeenDiagnostics;
    VariantArray _Unplaced;
    std::optional<std::uint32_t> _StoppedAtPriority;

    const auto _RememberDiagnostics = [&](const ObjectMap& Result_)
    {
        const auto _Found = Result_.find("diagnostics");
        if (_Found == Result_.end() || !_Found->second.Is<VariantArray>()) return;
        for (const auto& _Value : _Found->second.To<VariantArray>())
            if (_Value.Is<std::string>())
            {
                auto _Text = _Value.To<std::string>();
                if (_SeenDiagnostics.insert(_Text).second)
                    _LayerDiagnostics.push_back(std::move(_Text));
            }
    };
    const auto _AccumulateProof = [&](const ObjectMap& Result_)
    {
        const auto _ProofField = Result_.find("proof");
        if (_ProofField == Result_.end() || !_ProofField->second.Is<ObjectMap>())
        {
            _AllExact = false;
            _AllBoundsCertified = false;
            return;
        }
        const auto _Proof = _ProofField->second.To<ObjectMap>();
        const auto _Exact = _Proof.find("integerModelOptimalityProven");
        const auto _Bounds = _Proof.find("nativePairBoundsCertified");
        _AllExact = _AllExact && _Exact != _Proof.end()
            && _Exact->second.Is<bool>() && _Exact->second.To<bool>();
        _AllBoundsCertified = _AllBoundsCertified && _Bounds != _Proof.end()
            && _Bounds->second.Is<bool>() && _Bounds->second.To<bool>();
    };

    for (const auto _Priority : _PriorityLevels)
    {
        std::set<std::string> _RequiredPredecessors;
        for (const auto& _Stock : _AvailableStocks)
            if (!_Stock.InstanceID.empty())
                _RequiredPredecessors.insert(_Stock.InitialPredecessorPartID);
        std::vector<SNestingPart> _LayerParts;
        for (const auto& _Part : Parts_)
        {
            if (_Part.Priority != _Priority
                && !_RequiredPredecessors.contains(_Part.ID)) continue;
            auto _LayerPart = _Part;
            if (_LayerPart.Priority != _Priority) _LayerPart.Quantity = 0;
            _LayerParts.push_back(std::move(_LayerPart));
        }

        auto _LayerResult = SolveManufacturingNestingSinglePriority(
            _LayerParts, _AvailableStocks, PartGap_, MaximumConcurrency_);
        _RememberDiagnostics(_LayerResult);
        _AccumulateProof(_LayerResult);
        if (const auto _Metrics = _LayerResult.find("metrics");
            _Metrics != _LayerResult.end() && _Metrics->second.Is<ObjectMap>())
        {
            const auto _Map = _Metrics->second.To<ObjectMap>();
            if (const auto _Nodes = _Map.find("searchNodes"); _Nodes != _Map.end())
                if (const auto _Value = Number(_Nodes->second); _Value && *_Value >= 0.0)
                    _SearchNodes += static_cast<unsigned long long>(*_Value);
        }

        auto _FreshPlans = _LayerResult.at("plans").To<VariantArray>();
        for (auto& _Value : _FreshPlans)
        {
            auto _Plan = _Value.To<ObjectMap>();
            auto _PlanID = _RequiredText(_Plan, "id");
            const auto _StockTypeID = _RequiredText(_Plan, "stockTypeId");
            const auto _Continued = _RequiredBool(_Plan, "continued");

            if (_Continued)
            {
                const auto _Consumed = std::ranges::find_if(
                    _AvailableStocks, [&](const auto& Stock_)
                    { return Stock_.InstanceID == _PlanID; });
                if (_Consumed == _AvailableStocks.end())
                    throw std::logic_error("分层排样返回了未知的续排母材实例");
                _AvailableStocks.erase(_Consumed);
            }
            else
            {
                const auto _Consumed = std::ranges::find_if(
                    _AvailableStocks, [&](const auto& Stock_)
                    { return Stock_.InstanceID.empty() && Stock_.ID == _StockTypeID; });
                if (_Consumed == _AvailableStocks.end())
                    throw std::logic_error("分层排样返回了未知的普通母材规格");
                if (_Consumed->Quantity > 0 && --_Consumed->Quantity == 0)
                    _AvailableStocks.erase(_Consumed);
            }

            if (!_Continued && (_PlanIndices.contains(_PlanID)
                || _ReservedPlanIDs.contains(_PlanID)))
            {
                do
                {
                    _PlanID = _StockTypeID + "#priority-"
                        + std::to_string(++_PlanOrdinals[_StockTypeID]);
                }
                while (_PlanIndices.contains(_PlanID)
                    || _ReservedPlanIDs.contains(_PlanID));
                _Plan["id"] = _PlanID;
            }

            auto _Placements = _Plan.at("placements").To<VariantArray>();
            for (auto& _PlacementValue : _Placements)
            {
                auto _Placement = _PlacementValue.To<ObjectMap>();
                auto _InstanceID = _RequiredText(_Placement, "instanceId");
                if (_PlacementIDs.contains(_InstanceID))
                {
                    const auto _PartID = _RequiredText(_Placement, "partId");
                    do
                    {
                        _InstanceID = _PartID + "#priority-"
                            + std::to_string(++_PartOrdinals[_PartID]);
                    }
                    while (_PlacementIDs.contains(_InstanceID));
                    _Placement["instanceId"] = _InstanceID;
                }
                _PlacementIDs.insert(_InstanceID);
                _PlacementValue = std::move(_Placement);
            }
            _Plan["placements"] = std::move(_Placements);

            std::size_t _PlanIndex = 0;
            const auto _Existing = _PlanIndices.find(_PlanID);
            if (_Continued && _Existing != _PlanIndices.end())
            {
                _PlanIndex = _Existing->second;
                auto _Merged = _Plans[_PlanIndex].To<ObjectMap>();
                auto _MergedPlacements = _Merged.at("placements").To<VariantArray>();
                auto _NewPlacements = _Plan.at("placements").To<VariantArray>();
                _MergedPlacements.insert(_MergedPlacements.end(),
                    std::make_move_iterator(_NewPlacements.begin()),
                    std::make_move_iterator(_NewPlacements.end()));
                _Merged["placements"] = std::move(_MergedPlacements);
                _Merged["usedLength"] = _RequiredNumber(_Plan, "usedLength");
                _Merged["remainingLength"] = _RequiredNumber(_Plan, "remainingLength");
                _Merged["partLength"] = _RequiredNumber(_Merged, "partLength")
                    + _RequiredNumber(_Plan, "partLength");
                _Merged["partCount"] = Count(
                    _Merged.at("placements").To<VariantArray>().size());
                const auto _StockLength = _RequiredNumber(_Merged, "stockLength");
                _Merged["utilization"] = _StockLength > 0.0
                    ? _RequiredNumber(_Merged, "partLength") / _StockLength : 0.0;
                _Plans[_PlanIndex] = std::move(_Merged);
            }
            else
            {
                if (_Continued && !_ReservedPlanIDs.contains(_PlanID))
                    throw std::logic_error("分层排样续排实例没有来源");
                _PlanIndex = _Plans.size();
                _PlanIndices.emplace(_PlanID, _PlanIndex);
                _Plans.emplace_back(std::move(_Plan));
            }

            const auto _Merged = _Plans[_PlanIndex].To<ObjectMap>();
            const auto _Remaining = _RequiredNumber(_Merged, "remainingLength");
            const auto _MergedPlacements = _Merged.at("placements").To<VariantArray>();
            if (_Remaining >= 0.01 && !_MergedPlacements.empty())
            {
                std::string _ContinuationID;
                do
                {
                    _ContinuationID = "__priority_continuation__:"
                        + std::to_string(++_ContinuationOrdinal);
                }
                while (std::ranges::any_of(_AvailableStocks, [&](const auto& Stock_)
                    { return Stock_.ID == _ContinuationID; }));
                const auto _Last = _MergedPlacements.back().To<ObjectMap>();
                SNestingStock _Continuation;
                _Continuation.ID = std::move(_ContinuationID);
                _Continuation.ProfileKey = _RequiredText(_Merged, "profileKey");
                _Continuation.Length = _RequiredNumber(_Merged, "stockLength");
                _Continuation.Quantity = 1;
                _Continuation.SourceStockTypeID = _RequiredText(_Merged, "stockTypeId");
                _Continuation.InstanceID = _PlanID;
                _Continuation.InitialProcessedEnd = _RequiredNumber(_Merged, "usedLength");
                _Continuation.InitialPredecessorPartID = _RequiredText(_Last, "partId");
                _Continuation.InitialPredecessorReversed = _RequiredBool(_Last, "reversed");
                _Continuation.InitialPredecessorRotationRadians =
                    _RequiredNumber(_Last, "rotationRadians");
                _AvailableStocks.push_back(std::move(_Continuation));
            }
        }

        auto _LayerUnplaced = _LayerResult.at("unplaced").To<VariantArray>();
        if (!_LayerUnplaced.empty())
        {
            _StoppedAtPriority = _Priority;
            _AllExact = false;
            _Unplaced = std::move(_LayerUnplaced);
            for (const auto& _Part : Parts_)
            {
                if (_Part.Priority <= _Priority || !_Part.Quantity) continue;
                _Unplaced.emplace_back(ObjectMap{
                    { "partId", _Part.ID }, { "quantity", Count(_Part.Quantity) },
                    { "reason", std::string("更高优先级零件尚未全部排入，严格优先级模式未启动本级排样") }
                });
            }
            break;
        }
    }

    std::size_t _PlacedCount = 0;
    double _TotalStock = 0.0, _PartLength = 0.0, _UsedLength = 0.0;
    for (const auto& _Value : _Plans)
    {
        const auto _Plan = _Value.To<ObjectMap>();
        _PlacedCount += _Plan.at("placements").To<VariantArray>().size();
        _TotalStock += _RequiredNumber(_Plan, "stockLength");
        _PartLength += _RequiredNumber(_Plan, "partLength");
        _UsedLength += _RequiredNumber(_Plan, "usedLength");
    }
    std::size_t _UnplacedCount = 0;
    for (const auto& _Value : _Unplaced)
        _UnplacedCount += static_cast<std::size_t>(
            _RequiredNumber(_Value.To<ObjectMap>(), "quantity"));

    VariantArray _Diagnostics;
    _Diagnostics.emplace_back(std::string("已按数字从小到大执行 ")
        + std::to_string(_PriorityLevels.size())
        + " 个严格优先级；每一级完成后固定顺序和姿态，再从开放异形端续排下一级。");
    if (_StoppedAtPriority)
        _Diagnostics.emplace_back(std::string("优先级 ")
            + std::to_string(*_StoppedAtPriority)
            + " 未能全部排入，所有更低优先级均未启动。");
    for (auto& _Diagnostic : _LayerDiagnostics)
        _Diagnostics.emplace_back(std::move(_Diagnostic));

    const auto _Status = !_Unplaced.empty()
        ? (_Plans.empty() ? "infeasible" : "partial")
        : (_AllExact ? "optimal" : "feasible");
    return {
        { "status", std::string(_Status) }, { "plans", std::move(_Plans) },
        { "unplaced", std::move(_Unplaced) }, { "diagnostics", std::move(_Diagnostics) },
        { "proof", ObjectMap{
            { "integerModelOptimalityProven", _AllExact && !_StoppedAtPriority },
            { "nativePairBoundsCertified", _AllBoundsCertified },
            { "scope", std::string("strict-priority-fixed-prefix-layers") },
            { "priorityLevelCount", Count(_PriorityLevels.size()) }
        } },
        { "metrics", ObjectMap{
            { "requestedPartCount", Count(_RequestedCount) },
            { "placedPartCount", Count(_PlacedCount) },
            { "unplacedPartCount", Count(_UnplacedCount) },
            { "stockCount", Count(_PlanIndices.size()) },
            { "totalStockLength", _TotalStock }, { "partLength", _PartLength },
            { "usedLength", _UsedLength }, { "remainingLength", _TotalStock - _UsedLength },
            { "gapLength", _UsedLength - _PartLength },
            { "utilization", _TotalStock > 0.0 ? _PartLength / _TotalStock : 0.0 },
            { "searchNodes", _SearchNodes },
            { "partGap", Mm(Quantize(PartGap_, true)) }
        } }
    };
}
}
