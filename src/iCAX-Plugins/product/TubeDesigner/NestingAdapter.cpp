#include "pch.h"
#include "NestingAdapter.h"
#include "FinalGeometryMeasurement.h"

#include "TemplateRuntime/StandardJsonCodec.h"
#include "TubeNesting/TubeNesting.h"
#include "Task/Task.h"
#include <cctype>
#include <thread>
#include <iterator>

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
    const double _YDirection = Reversed_ ? -1.0 : 1.0;
    const double _Cosine = std::cos(RotationRadians_);
    const double _Sine = std::sin(RotationRadians_);
    const double _M11 = _Cosine * _YDirection;
    const double _M12 = -_Sine;
    const double _M21 = _Sine * _YDirection;
    const double _M22 = _Cosine;
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

    SolverSettings Settings(const std::size_t Count_)
    {
        SolverSettings _Settings;
        _Settings.Kerf = 0;
        _Settings.ConstructionRuns = Count_ > 200 ? 2 : 6;
        _Settings.LocalSearchPasses = Count_ > 200 ? 0 : 3;
        _Settings.LargeNeighborhoodIterations = Count_ > 200 ? 0 : 32;
        _Settings.ExactPartLimit = 10;
        _Settings.ExactNodeLimit = 50000;
        return _Settings;
    }

    bool Complete(const SolveResult& Result_)
    {
        return Result_.Status == SolveStatus::Optimal || Result_.Status == SolveStatus::Feasible;
    }

    // Stock-shortage fallback: ask the SAME solver for bounded, progressively sized
    // subsets. Shortest first favors fulfilled piece count. This is not a claim of
    // optimal partial fulfillment; every omitted piece is reported explicitly.
    SolveResult SolveAvailableSubset(
        const std::vector<PartDemand>& Demands_, const std::vector<StockType>& Stocks_,
        const SolverSettings& Settings_)
    {
        std::vector<std::size_t> _Instances;
        for (std::size_t _Index = 0; _Index < Demands_.size(); ++_Index)
            _Instances.insert(_Instances.end(), Demands_[_Index].Quantity, _Index);
        std::stable_sort(_Instances.begin(), _Instances.end(), [&](const auto Left_, const auto Right_) {
            return Demands_[Left_].NominalLength < Demands_[Right_].NominalLength;
        });
        auto _Quick = Settings_;
        _Quick.ConstructionRuns = 1;
        _Quick.LocalSearchPasses = 0;
        _Quick.LargeNeighborhoodIterations = 0;
        _Quick.ExactPartLimit = 0;
        const auto _Subset = [&](const std::size_t Size_) {
            auto _Parts = Demands_;
            for (auto& _Part : _Parts) _Part.Quantity = 0;
            for (std::size_t _Index = 0; _Index < Size_; ++_Index)
                ++_Parts[_Instances[_Index]].Quantity;
            std::erase_if(_Parts, [](const auto& Part_) { return Part_.Quantity == 0; });
            return _Parts;
        };
        std::size_t _Low = 0;
        std::size_t _High = _Instances.size();
        SolveResult _Best;
        while (_Low < _High)
        {
            const auto _Middle = _Low + (_High - _Low + 1) / 2;
            auto _Candidate = Solver().Solve(_Subset(_Middle), Stocks_, _Quick);
            if (Complete(_Candidate))
            {
                _Low = _Middle;
                _Best = std::move(_Candidate);
            }
            else _High = _Middle - 1;
        }
        if (_Low != 0)
        {
            auto _Optimized = Solver().Solve(_Subset(_Low), Stocks_, Settings_);
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

ObjectMap SolveManufacturingNesting(
    const std::vector<SNestingPart>& Parts_, const std::vector<SNestingStock>& Stocks_,
    const double PartGap_, const std::size_t MaximumConcurrency_)
{
    if (Parts_.empty()) throw std::invalid_argument("请先勾选需要排样的零件");
    if (Stocks_.size() > 1000) throw std::invalid_argument("母材规格行不能超过 1000 行");
    const auto _Gap = Quantize(PartGap_, true);
    std::size_t _TotalParts = 0;
    std::set<std::string> _IDs;
    std::map<std::string, std::array<double, 3>> _LocalCenters;
    std::map<std::string, std::vector<PartDemand>> _Groups;
    for (const auto& _Part : Parts_)
    {
        if (_Part.ID.empty() || _Part.ProfileKey.empty() || !_IDs.insert(_Part.ID).second)
            throw std::invalid_argument("零件编号、截面分组为空或零件重复");
        if (_Part.Quantity == 0 || _Part.Quantity > kMaximumPartInstances - _TotalParts)
            throw std::invalid_argument("单次排样需为 1 至 1000000 件，请分批排样");
        _TotalParts += _Part.Quantity;
        _LocalCenters.emplace(_Part.ID, _Part.LocalCenter);
        const auto _Length = Quantize(_Part.Length, true);
        if (_Length <= 0) throw std::invalid_argument("零件长度必须大于 0 mm");
        std::vector<PartVariant> _Variants;
        if (_Part.Variants.empty())
        {
            PartVariant _Variant;
            _Variant.AxialLength = _Length;
            _Variant.MaterialLength = _Length;
            _Variant.LeftEnd.AllowCommonCut = false;
            _Variant.RightEnd.AllowCommonCut = false;
            _Variants.push_back(std::move(_Variant));
        }
        else
        {
            if (_Part.Variants.size() > 16)
                throw std::invalid_argument("单个零件的排样姿态不能超过 16 种");
            std::set<std::string> _VariantIDs;
            for (const auto& _Source : _Part.Variants)
            {
                if (_Source.ID.empty() || !_VariantIDs.insert(_Source.ID).second
                    || !std::isfinite(_Source.RotationRadians)
                    || !std::isfinite(_Source.PhaseOffset))
                    throw std::invalid_argument("零件排样姿态编号、旋转角或相位偏移无效");
                PartVariant _Variant;
                _Variant.ID = _Source.ID;
                _Variant.AxialLength = Quantize(_Source.EnvelopeLength, true);
                if (_Variant.AxialLength != _Length)
                    throw std::invalid_argument("零件排样姿态的轴向包络与制造几何不一致");
                _Variant.MaterialLength = std::min(
                    _Variant.AxialLength, Quantize(_Source.MaterialLength, true));
                if (_Variant.MaterialLength <= 0 || _Variant.MaterialLength > _Variant.AxialLength)
                    throw std::invalid_argument("零件排样姿态的净材料长度无效");
                const auto _CopyEnd = [](const SNestingEnd& Source_, EndDescriptor& Target_) {
                    Target_.AllowCommonCut = false;
                    Target_.NestingProjection = QuantizeProjection(Source_.Projection);
                    Target_.NestingPlane = Source_.NestingPlane;
                    Target_.AllowTrapezoidNesting = Source_.AllowTrapezoidNesting
                        && Target_.NestingProjection > 0 && !Target_.NestingPlane.empty();
                    if (Source_.Feature.Kind != CutLineFeatureKind::Invalid)
                    {
                        if (!IsValidCutLineFeature(Source_.Feature))
                            throw std::invalid_argument("端曲线特征编码无效");
                        Target_.Feature = Source_.Feature;
                    }
                    if (Target_.NestingPlane.size() > 160)
                        throw std::invalid_argument("斜切端面方向编号过长");
                };
                _CopyEnd(_Source.LeftEnd, _Variant.LeftEnd);
                _CopyEnd(_Source.RightEnd, _Variant.RightEnd);
                _Variant.Reversed = _Source.Reversed;
                _Variant.RotationRadians = _Source.RotationRadians;
                if (!std::isfinite(_Source.PhaseOffset) || _Source.PhaseOffset < 0.0
                    || _Source.PhaseOffset > kMaximumLength)
                    throw std::invalid_argument("零件展开相位偏移无效");
                _Variant.PhaseOffset = Quantize(_Source.PhaseOffset, false);
                _Variants.push_back(std::move(_Variant));
            }
        }
        _Groups[_Part.ProfileKey].push_back({
            _Part.ID, _Part.ProfileKey, {}, _Length, _Part.Quantity, std::move(_Variants) });
    }
    _IDs.clear();
    std::map<std::string, std::vector<StockType>> _StocksByProfile;
    for (const auto& _Stock : Stocks_)
    {
        if (_Stock.ID.empty() || _Stock.ProfileKey.empty() || !_IDs.insert(_Stock.ID).second)
            throw std::invalid_argument("母材编号、截面分组为空或母材编号重复");
        if (_Stock.Quantity < -1 || _Stock.Quantity > 1000000)
            throw std::invalid_argument("母材数量必须为 -1（不限量）、0（停用）或正整数");
        if (_Stock.Quantity == 0) continue;
        const auto _Length = Quantize(_Stock.Length, false);
        if (_Length <= 0) throw std::invalid_argument("母材长度必须至少为 0.01 mm");
        StockType _Native;
        _Native.ID = _Stock.ID;
        _Native.CompatibilityKey = _Stock.ProfileKey;
        _Native.TotalLength = _Length;
        _Native.Quantity = _Stock.Quantity == -1 ? 0 : static_cast<std::size_t>(_Stock.Quantity);
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
    };
    const auto _SolveGroup = [_Gap, &_LocalCenters](const std::vector<PartDemand>& _Demands,
        const std::vector<StockType>& _Stocks) {
        SGroupResult _GroupResult;
        auto& [_Plans, _PlacedQuantities, _MissingReasons, _TotalStock,
            _PartLength, _UsedLength, _PlacedCount, _AllExact, _SearchNodes] = _GroupResult;
        Length _MaximumStock = 0;
        for (const auto& _Stock : _Stocks) _MaximumStock = std::max(_MaximumStock, _Stock.TotalLength);
        std::vector<PartDemand> _FitDemands;
        for (const auto& _Demand : _Demands)
        {
            if (_MaximumStock < _Demand.NominalLength)
            {
                _MissingReasons[_Demand.ID] = _Stocks.empty() ? "该截面未设置可用母材" : "零件长于该截面的所有可用母材";
                _AllExact = false;
            }
            else _FitDemands.push_back(_Demand);
        }
        if (_FitDemands.empty()) return _GroupResult;
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
        std::vector<PartDemand> _PhaseDemands;
        std::size_t _PhaseCount = 0;
        for (auto _Demand : _FitDemands)
        {
            _Demand.Quantity -= _PlacedQuantities[_Demand.ID];
            if (_Demand.Quantity && _Demand.NominalLength <= _PhaseMaximum)
            {
                _PhaseCount += _Demand.Quantity;
                _PhaseDemands.push_back(std::move(_Demand));
            }
        }
        if (_PhaseDemands.empty()) continue;
        auto _Settings = Settings(_PhaseCount);
        _Settings.PartGap = _Gap;
        auto _Result = Solver().Solve(_PhaseDemands, _PhaseStocks, _Settings);
        if (_Result.Status == SolveStatus::InvalidInput)
            throw std::runtime_error("原生排样求解器拒绝了输入数据");
        if (!Complete(_Result))
        {
            _Result = SolveAvailableSubset(_PhaseDemands, _PhaseStocks, _Settings);
            _AllExact = false;
        }
        _AllExact = _AllExact && _Result.ExactSearchCompleted;
        _SearchNodes += _Result.Metrics.SearchNodes;
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
                    { "start", Mm(_Placement.Start) }, { "end", Mm(_Placement.End) },
                    { "length", Mm(_Placement.End - _Placement.Start) },
                    { "gapBefore", Mm(_Placement.GapBefore) },
                    // The first placement has no preceding part to nest with.
                    { "nestedWithPrevious", !_Placements.empty() && _Placement.GapBefore < _Gap },
                    { "variantId", _Placement.VariantID },
                    { "reversed", _Placement.Reversed },
                    { "phaseOffset", Mm(_Placement.PhaseOffset) },
                    { "rotationRadians", _Placement.RotationRadians },
                    { "trsf", MakeNestingPlacementTransform(_LocalCenters.at(_Placement.DemandID),
                        Mm(_Placement.Start), Mm(_Placement.End), _Placement.Reversed,
                        _Placement.RotationRadians) }
                });
            }
            _Plans.emplace_back(ObjectMap{
                { "id", _Stock.StockInstanceID }, { "stockTypeId", _Stock.StockTypeID },
                { "profileKey", _Stock.CompatibilityKey }, { "stockLength", Mm(_Stock.TotalLength) },
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
        const std::vector<PartDemand>* Demands;
        const std::vector<StockType>* Stocks;
    };
    std::vector<SGroupInput> _Inputs;
    for (const auto& [_Profile, _Demands] : _Groups)
        _Inputs.push_back({ &_Demands, &_StocksByProfile[_Profile] });
    std::vector<SGroupResult> _Results(_Inputs.size());
    const auto _Hardware = std::thread::hardware_concurrency();
    const std::size_t _Concurrency = MaximumConcurrency_ == 0
        ? std::min(8u, std::max(1u, _Hardware > 1 ? _Hardware - 1 : 1u))
        : std::clamp<std::size_t>(MaximumConcurrency_, 1, 8);
    const auto _RunGroup = [&](std::size_t Index_) {
        _Results[Index_] = _SolveGroup(*_Inputs[Index_].Demands, *_Inputs[Index_].Stocks);
    };
    for (std::size_t _Begin = 0; _Begin < _Inputs.size(); _Begin += _Concurrency)
    {
        const auto _Count = std::min(_Concurrency, _Inputs.size() - _Begin);
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
        _PartLength, _UsedLength, _PlacedCount, _AllExact, _SearchNodes] = _Aggregate;
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
    const auto _UsesTrapezoidNesting = std::ranges::any_of(Parts_, [](const auto& Part_) {
        return std::ranges::any_of(Part_.Variants, [](const auto& Variant_) {
            return Variant_.LeftEnd.AllowTrapezoidNesting || Variant_.RightEnd.AllowTrapezoidNesting;
        });
    });
    _Diagnostics.emplace_back(_UsesTrapezoidNesting
        ? std::string("按直切或保守斜切毛坯排样；斜切刀平面完整包住最终零件后参与梯形套切，不要求真实端口为平面。")
        : std::string("按直切包络排样；未找到可靠斜切近似的端口使用直切包络，间距仅计在相邻零件之间。"));
    return {
        { "status", std::string(_Status) }, { "plans", std::move(_Plans) },
        { "unplaced", std::move(_Unplaced) }, { "diagnostics", std::move(_Diagnostics) },
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
