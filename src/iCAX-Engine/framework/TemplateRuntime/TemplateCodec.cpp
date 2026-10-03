#include "TemplateCodec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
    using namespace iCAX::TemplateRuntime;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;

    const ObjectMap& RequireObject(const Variant& Value_, const std::string& strPath_)
    {
        if (!Value_.Is<ObjectMap>())
            throw std::invalid_argument(strPath_ + " must be an object");
        return std::get<ObjectMap>(Value_.m_Value);
    }

    const VariantArray& RequireArray(const Variant& Value_, const std::string& strPath_)
    {
        if (!Value_.Is<VariantArray>())
            throw std::invalid_argument(strPath_ + " must be an array");
        return std::get<VariantArray>(Value_.m_Value);
    }

    const Variant* Find(const ObjectMap& Object_, const std::string& strName_)
    {
        const auto _Iterator = Object_.find(strName_);
        return _Iterator == Object_.end() ? nullptr : &_Iterator->second;
    }

    std::string RequireString(const ObjectMap& Object_, const std::string& strName_, const std::string& strPath_)
    {
        const auto _Value = Find(Object_, strName_);
        if (!_Value || !_Value->Is<std::string>() || _Value->To<std::string>().empty())
            throw std::invalid_argument(strPath_ + "." + strName_ + " must be a non-empty string");
        return _Value->To<std::string>();
    }

    void ValidateStableKey(const std::string& strValue_, const std::string& strPath_)
    {
        const auto _IsFirst = [](unsigned char Character_) {
            return std::isalnum(Character_) != 0 || Character_ == '_';
        };
        const auto _IsRest = [&_IsFirst](unsigned char Character_) {
            return _IsFirst(Character_) || Character_ == '.' || Character_ == '-'
                || Character_ == ':';
        };
        if (strValue_.empty() || !_IsFirst(static_cast<unsigned char>(strValue_.front()))
            || !std::all_of(strValue_.begin() + 1, strValue_.end(),
                [&_IsRest](char Character_) {
                    return _IsRest(static_cast<unsigned char>(Character_));
                }))
        {
            throw std::invalid_argument(strPath_
                + " must use only letters, digits, '.', '_', '-' or ':' and cannot be a path");
        }
    }

    std::string OptionalString(const ObjectMap& Object_, const std::string& strName_, const std::string& strDefault_ = {})
    {
        const auto _Value = Find(Object_, strName_);
        if (!_Value || _Value->Is<std::monostate>()) return strDefault_;
        if (!_Value->Is<std::string>())
            throw std::invalid_argument(strName_ + " must be a string");
        return _Value->To<std::string>();
    }

    bool OptionalBool(const ObjectMap& Object_, const std::string& strName_, bool bDefault_)
    {
        const auto _Value = Find(Object_, strName_);
        if (!_Value || _Value->Is<std::monostate>()) return bDefault_;
        if (!_Value->Is<bool>())
            throw std::invalid_argument(strName_ + " must be a boolean");
        return _Value->To<bool>();
    }

    double ToDouble(const Variant& Value_, const std::string& strPath_)
    {
        if (Value_.Is<double>()) return Value_.To<double>();
        if (Value_.Is<float>()) return Value_.To<float>();
        if (Value_.Is<int>()) return Value_.To<int>();
        if (Value_.Is<unsigned int>()) return Value_.To<unsigned int>();
        if (Value_.Is<long long>()) return static_cast<double>(Value_.To<long long>());
        if (Value_.Is<unsigned long long>()) return static_cast<double>(Value_.To<unsigned long long>());
        throw std::invalid_argument(strPath_ + " must be numeric");
    }

    std::uint64_t ToUInt64(const Variant& Value_, const std::string& strPath_)
    {
        const auto _Number = ToDouble(Value_, strPath_);
        if (!std::isfinite(_Number) || _Number < 0.0 || std::floor(_Number) != _Number)
            throw std::invalid_argument(strPath_ + " must be a non-negative integer");
        return static_cast<std::uint64_t>(_Number);
    }

    std::int64_t ToInt64(const Variant& Value_, const std::string& strPath_)
    {
        const auto _Number = ToDouble(Value_, strPath_);
        if (!std::isfinite(_Number) || std::floor(_Number) != _Number
            || _Number < static_cast<double>(std::numeric_limits<std::int64_t>::min())
            || _Number > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::invalid_argument(strPath_ + " must be an integer");
        }
        return static_cast<std::int64_t>(_Number);
    }

    ObjectMap ParseRigidItemPlacement(const Variant& Value_, const std::string& Path_)
    {
        const auto& _Placement = RequireObject(Value_, Path_);
        for (const auto& [_Key, _Value] : _Placement)
            if (_Key != "origin" && _Key != "xAxis" && _Key != "yAxis" && _Key != "zAxis")
                throw std::invalid_argument(Path_ + " contains unsupported field " + _Key);
        ObjectMap _Result;
        const auto _Vector = [&](const char* Key_) {
            const auto _Value = Find(_Placement, Key_);
            if (!_Value) throw std::invalid_argument(Path_ + "." + Key_ + " is required");
            const auto& _Coordinates = RequireArray(*_Value, Path_ + "." + Key_);
            if (_Coordinates.size() != 3)
                throw std::invalid_argument(Path_ + "." + Key_ + " must have three coordinates");
            std::array<double, 3> _Numbers;
            VariantArray _Canonical;
            for (std::size_t _Index = 0; _Index < 3; ++_Index)
            {
                _Numbers[_Index] = ToDouble(_Coordinates[_Index], Path_ + "." + Key_);
                if (!std::isfinite(_Numbers[_Index]))
                    throw std::invalid_argument(Path_ + "." + Key_ + " must be finite");
                _Canonical.emplace_back(_Numbers[_Index]);
            }
            _Result[Key_] = std::move(_Canonical);
            return _Numbers;
        };
        (void)_Vector("origin");
        const auto _X = _Vector("xAxis"), _Y = _Vector("yAxis"), _Z = _Vector("zAxis");
        const auto _Dot = [](const auto& A_, const auto& B_) {
            return A_[0] * B_[0] + A_[1] * B_[1] + A_[2] * B_[2];
        };
        constexpr double _AxisTolerance = 1.0e-7;
        if (std::abs(_Dot(_X, _X) - 1.0) > _AxisTolerance
            || std::abs(_Dot(_Y, _Y) - 1.0) > _AxisTolerance
            || std::abs(_Dot(_Z, _Z) - 1.0) > _AxisTolerance)
            throw std::invalid_argument(Path_ + " axes must be unit vectors; scaling is not supported");
        if (std::abs(_Dot(_X, _Y)) > _AxisTolerance || std::abs(_Dot(_X, _Z)) > _AxisTolerance
            || std::abs(_Dot(_Y, _Z)) > _AxisTolerance)
            throw std::invalid_argument(Path_ + " axes must be orthogonal");
        const std::array<double, 3> _Cross{
            _X[1] * _Y[2] - _X[2] * _Y[1], _X[2] * _Y[0] - _X[0] * _Y[2],
            _X[0] * _Y[1] - _X[1] * _Y[0]};
        if (std::abs(_Dot(_Cross, _Z) - 1.0) > _AxisTolerance)
            throw std::invalid_argument(Path_ + " axes must be right-handed; mirroring is not supported");
        return _Result;
    }

    void ValidatePlateComponent(const Variant& Value_, const std::string& Path_)
    {
        const auto& _Plate = RequireObject(Value_, Path_);
        const std::set<std::string> _Fields{"width", "height", "thickness", "areaMm2", "center", "xAxis", "yAxis", "outline"};
        for (const auto& [_Key, _Value] : _Plate)
            if (!_Fields.contains(_Key)) throw std::invalid_argument(Path_ + " contains unsupported field " + _Key);
        const auto _Dimension = [&](const char* Key_) {
            const auto _Value = Find(_Plate, Key_);
            if (!_Value) throw std::invalid_argument(Path_ + "." + Key_ + " is required");
            const auto _Number = ToDouble(*_Value, Path_ + "." + Key_);
            if (!std::isfinite(_Number) || _Number <= 0.0)
                throw std::invalid_argument(Path_ + "." + Key_ + " must be positive and finite");
            return _Number;
        };
        const auto _Width = _Dimension("width"), _Height = _Dimension("height");
        (void)_Dimension("thickness");
        auto _Area = _Width * _Height;
        if (!std::isfinite(_Area)) throw std::invalid_argument(Path_ + " area must be finite");
        if (const auto _Value = Find(_Plate, "outline"))
        {
            const auto& _Values = RequireArray(*_Value, Path_ + ".outline");
            if (_Values.size() < 3) throw std::invalid_argument(Path_ + " outline requires at least three distinct finite 2D points");
            std::vector<std::array<double, 2>> _Points;
            std::set<std::array<double, 2>> _DistinctPoints;
            for (const auto& _PointValue : _Values)
            {
                const auto& _Coordinates = RequireArray(_PointValue, Path_ + ".outline point");
                if (_Coordinates.size() != 2) throw std::invalid_argument(Path_ + " outline requires finite 2D points");
                const std::array<double, 2> _Point{
                    ToDouble(_Coordinates[0], Path_ + ".outline point.x"), ToDouble(_Coordinates[1], Path_ + ".outline point.y")};
                if (!std::isfinite(_Point[0]) || !std::isfinite(_Point[1]) || !_DistinctPoints.insert(_Point).second)
                    throw std::invalid_argument(Path_ + " outline requires distinct finite 2D points");
                _Points.push_back(_Point);
            }
            double _TwiceSignedArea = 0.0;
            for (std::size_t _Index = 0; _Index < _Points.size(); ++_Index)
            {
                const auto& _A = _Points[_Index];
                const auto& _B = _Points[(_Index + 1) % _Points.size()];
                _TwiceSignedArea += _A[0] * _B[1] - _B[0] * _A[1];
            }
            _Area = std::abs(_TwiceSignedArea) / 2.0;
            if (!std::isfinite(_Area) || _Area <= 0.0)
                throw std::invalid_argument(Path_ + " outline must have finite positive area");
        }
        if (const auto _Value = Find(_Plate, "areaMm2"))
        {
            const auto _DeclaredArea = ToDouble(*_Value, Path_ + ".areaMm2");
            if (!std::isfinite(_DeclaredArea) || std::abs(_DeclaredArea - _Area) > std::max(1e-8, std::max(std::abs(_DeclaredArea), _Area) * 1e-8))
                throw std::invalid_argument(Path_ + " area must match dimensions or outline");
        }
        const auto _FrameCount = unsigned(Find(_Plate, "center") != nullptr) + unsigned(Find(_Plate, "xAxis") != nullptr)
            + unsigned(Find(_Plate, "yAxis") != nullptr);
        if (!_FrameCount) return;
        if (_FrameCount != 3) throw std::invalid_argument(Path_ + " intrinsic frame requires center, xAxis and yAxis");
        const auto _Vector = [&](const char* Key_) {
            const auto& _Values = RequireArray(_Plate.at(Key_), Path_ + "." + Key_);
            if (_Values.size() != 3) throw std::invalid_argument(Path_ + "." + Key_ + " requires three coordinates");
            std::array<double, 3> _Numbers;
            for (std::size_t _Index = 0; _Index < 3; ++_Index)
            {
                _Numbers[_Index] = ToDouble(_Values[_Index], Path_ + "." + Key_);
                if (!std::isfinite(_Numbers[_Index])) throw std::invalid_argument(Path_ + " intrinsic frame must be finite");
            }
            return _Numbers;
        };
        (void)_Vector("center");
        const auto _X = _Vector("xAxis"), _Y = _Vector("yAxis");
        const auto _Dot = [](const auto& A_, const auto& B_) { return A_[0] * B_[0] + A_[1] * B_[1] + A_[2] * B_[2]; };
        if (std::abs(_Dot(_X, _X) - 1.0) > 1e-7 || std::abs(_Dot(_Y, _Y) - 1.0) > 1e-7
            || std::abs(_Dot(_X, _Y)) > 1e-7)
            throw std::invalid_argument(Path_ + " intrinsic axes must be unit and orthogonal");
    }

    std::optional<double> OptionalNumber(const ObjectMap& Object_, const std::string& strName_, const std::string& strPath_)
    {
        const auto _Value = Find(Object_, strName_);
        if (!_Value || _Value->Is<std::monostate>()) return std::nullopt;
        return ToDouble(*_Value, strPath_ + "." + strName_);
    }

    SLocalizedText ParseLocalizedText(const Variant& Value_, const std::string& strPath_)
    {
        SLocalizedText _Result;
        if (Value_.Is<std::string>())
        {
            _Result.Default = Value_.To<std::string>();
            return _Result;
        }
        const auto& _Object = RequireObject(Value_, strPath_);
        for (const auto& [_Locale, _Text] : _Object)
        {
            if (!_Text.Is<std::string>())
                throw std::invalid_argument(strPath_ + "." + _Locale + " must be a string");
            _Result.Translations.emplace(_Locale, _Text.To<std::string>());
        }
        if (const auto _Iterator = _Result.Translations.find("zh-CN");
            _Iterator != _Result.Translations.end())
            _Result.Default = _Iterator->second;
        else if (!_Result.Translations.empty())
            _Result.Default = _Result.Translations.begin()->second;
        return _Result;
    }

    SLocalizedText RequiredLocalizedText(const ObjectMap& Object_, const std::string& strName_, const std::string& strPath_)
    {
        const auto _Value = Find(Object_, strName_);
        if (!_Value) throw std::invalid_argument(strPath_ + "." + strName_ + " is required");
        auto _Result = ParseLocalizedText(*_Value, strPath_ + "." + strName_);
        if (_Result.Resolve().empty())
            throw std::invalid_argument(strPath_ + "." + strName_ + " cannot be empty");
        return _Result;
    }

    EParameterValueType ParseValueType(const std::string& strValue_)
    {
        if (strValue_ == "number") return EParameterValueType::Number;
        if (strValue_ == "integer") return EParameterValueType::Integer;
        if (strValue_ == "boolean") return EParameterValueType::Boolean;
        if (strValue_ == "string") return EParameterValueType::String;
        if (strValue_ == "enum") return EParameterValueType::Enumeration;
        throw std::invalid_argument("unsupported parameter valueType: " + strValue_);
    }

    std::string ValueTypeName(EParameterValueType Type_)
    {
        switch (Type_)
        {
        case EParameterValueType::Number: return "number";
        case EParameterValueType::Integer: return "integer";
        case EParameterValueType::Boolean: return "boolean";
        case EParameterValueType::String: return "string";
        case EParameterValueType::Enumeration: return "enum";
        }
        throw std::logic_error("unknown parameter value type");
    }

    EParameterQuantity ParseQuantity(const std::string& strValue_)
    {
        if (strValue_.empty() || strValue_ == "none") return EParameterQuantity::None;
        if (strValue_ == "length") return EParameterQuantity::Length;
        if (strValue_ == "angle") return EParameterQuantity::Angle;
        if (strValue_ == "ratio") return EParameterQuantity::Ratio;
        if (strValue_ == "count") return EParameterQuantity::Count;
        throw std::invalid_argument("unsupported parameter quantity: " + strValue_);
    }

    std::string QuantityName(EParameterQuantity Quantity_)
    {
        switch (Quantity_)
        {
        case EParameterQuantity::None: return "none";
        case EParameterQuantity::Length: return "length";
        case EParameterQuantity::Angle: return "angle";
        case EParameterQuantity::Ratio: return "ratio";
        case EParameterQuantity::Count: return "count";
        }
        throw std::logic_error("unknown parameter quantity");
    }

    SParameterCondition ParseCondition(const Variant* pValue_, const std::string& strPath_)
    {
        if (!pValue_ || pValue_->Is<std::monostate>()) return {};
        const auto& _Object = RequireObject(*pValue_, strPath_);
        const auto _Operator = OptionalString(_Object, "op", "eq");
        SParameterCondition _Result;
        if (_Operator == "eq" || _Operator == "ne")
        {
            _Result.Kind = _Operator == "eq" ? EConditionKind::Equals : EConditionKind::NotEquals;
            _Result.ParameterKey = RequireString(_Object, "parameter", strPath_);
            const auto _Expected = Find(_Object, "value");
            if (!_Expected) throw std::invalid_argument(strPath_ + ".value is required");
            _Result.ExpectedValue = *_Expected;
            return _Result;
        }
        if (_Operator == "all" || _Operator == "any")
        {
            _Result.Kind = _Operator == "all" ? EConditionKind::All : EConditionKind::Any;
            const auto _Conditions = Find(_Object, "conditions");
            if (!_Conditions) throw std::invalid_argument(strPath_ + ".conditions is required");
            std::size_t _Index = 0;
            for (const auto& _Condition : RequireArray(*_Conditions, strPath_ + ".conditions"))
                _Result.Children.push_back(ParseCondition(&_Condition,
                    strPath_ + ".conditions[" + std::to_string(_Index++) + "]"));
            if (_Result.Children.empty())
                throw std::invalid_argument(strPath_ + ".conditions cannot be empty");
            return _Result;
        }
        if (_Operator == "not")
        {
            _Result.Kind = EConditionKind::Not;
            const auto _Condition = Find(_Object, "condition");
            if (!_Condition) throw std::invalid_argument(strPath_ + ".condition is required");
            _Result.Children.push_back(ParseCondition(_Condition, strPath_ + ".condition"));
            return _Result;
        }
        throw std::invalid_argument(strPath_ + ".op is unsupported: " + _Operator);
    }

    EGeometryOperator ParseGeometryOperator(const std::string& strValue_)
    {
        if (strValue_ == "profile2d") return EGeometryOperator::Profile2D;
        if (strValue_ == "extrude") return EGeometryOperator::Extrude;
        if (strValue_ == "sweep") return EGeometryOperator::Sweep;
        if (strValue_ == "revolve") return EGeometryOperator::Revolve;
        if (strValue_ == "loft") return EGeometryOperator::Loft;
        if (strValue_ == "boolean") return EGeometryOperator::Boolean;
        if (strValue_ == "transform") return EGeometryOperator::Transform;
        if (strValue_ == "pattern") return EGeometryOperator::Pattern;
        if (strValue_ == "fillet") return EGeometryOperator::Fillet;
        if (strValue_ == "chamfer") return EGeometryOperator::Chamfer;
        if (strValue_ == "compound") return EGeometryOperator::Compound;
        if (strValue_ == "resource") return EGeometryOperator::Resource;
        throw std::invalid_argument("unsupported geometry operator: " + strValue_);
    }

    std::vector<std::string> ToStringVector(const Variant* pValue_, const std::string& strPath_)
    {
        std::vector<std::string> _Result;
        if (!pValue_) return _Result;
        std::size_t _Index = 0;
        for (const auto& _Value : RequireArray(*pValue_, strPath_))
        {
            if (!_Value.Is<std::string>() || _Value.To<std::string>().empty())
                throw std::invalid_argument(strPath_ + "[" + std::to_string(_Index) + "] must be a non-empty string");
            _Result.push_back(_Value.To<std::string>());
            ++_Index;
        }
        return _Result;
    }

    struct SNumericEnumValue
    {
        enum class EKind { NotNumeric, Integer, Floating } Kind = EKind::NotNumeric;
        bool Negative = false;
        std::uint64_t Magnitude = 0;
        double Floating = 0.0;
    };

    SNumericEnumValue NumericEnumValue(const Variant& Value_)
    {
        return std::visit([](const auto& Item_) -> SNumericEnumValue {
            using T = std::decay_t<decltype(Item_)>;
            // bool is an enum value in its own right, not an alias for 0 or 1.
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    const auto _Signed = static_cast<std::int64_t>(Item_);
                    if (_Signed < 0)
                        return { SNumericEnumValue::EKind::Integer, true,
                            static_cast<std::uint64_t>(-(_Signed + 1)) + 1 };
                }
                return { SNumericEnumValue::EKind::Integer, false,
                    static_cast<std::uint64_t>(Item_) };
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                const auto _Number = static_cast<double>(Item_);
                if (!std::isfinite(_Number)) return {};
                const auto _Magnitude = std::abs(_Number);
                if (std::trunc(_Number) == _Number && _Magnitude < std::ldexp(1.0, 64))
                    return { SNumericEnumValue::EKind::Integer, _Number < 0.0,
                        static_cast<std::uint64_t>(_Magnitude) };
                return { SNumericEnumValue::EKind::Floating, false, 0, _Number };
            }
            else return {};
        }, Value_.m_Value);
    }

    bool EnumValuesEqual(const Variant& Left_, const Variant& Right_)
    {
        if (Left_ == Right_) return true;
        // Standard JSON produces int64, whereas the web bridge emits int32 for
        // small whole numbers. Compare their values without changing Variant's
        // global strict equality or rounding large integers through double.
        const auto _Left = NumericEnumValue(Left_);
        const auto _Right = NumericEnumValue(Right_);
        if (_Left.Kind != _Right.Kind) return false;
        if (_Left.Kind == SNumericEnumValue::EKind::Integer)
            return _Left.Negative == _Right.Negative && _Left.Magnitude == _Right.Magnitude;
        return _Left.Kind == SNumericEnumValue::EKind::Floating
            && _Left.Floating == _Right.Floating;
    }

    Variant NormalizeValue(const SParameterDefinition& Definition_, const Variant& Value_,
        bool ValidateBounds_ = true)
    {
        switch (Definition_.ValueType)
        {
        case EParameterValueType::Number:
        {
            const auto _Number = ToDouble(Value_, Definition_.Key);
            if (!std::isfinite(_Number))
                throw std::invalid_argument(Definition_.Key + " must be finite");
            if (ValidateBounds_ && Definition_.Constraints.Minimum && _Number < *Definition_.Constraints.Minimum)
                throw std::invalid_argument(Definition_.Key + " is below its minimum");
            if (ValidateBounds_ && Definition_.Constraints.Maximum && _Number > *Definition_.Constraints.Maximum)
                throw std::invalid_argument(Definition_.Key + " exceeds its maximum");
            return Variant(_Number);
        }
        case EParameterValueType::Integer:
        {
            const auto _Number = ToInt64(Value_, Definition_.Key);
            if (ValidateBounds_ && Definition_.Constraints.Minimum && static_cast<double>(_Number) < *Definition_.Constraints.Minimum)
                throw std::invalid_argument(Definition_.Key + " is below its minimum");
            if (ValidateBounds_ && Definition_.Constraints.Maximum && static_cast<double>(_Number) > *Definition_.Constraints.Maximum)
                throw std::invalid_argument(Definition_.Key + " exceeds its maximum");
            return Variant(static_cast<long long>(_Number));
        }
        case EParameterValueType::Boolean:
            if (!Value_.Is<bool>()) throw std::invalid_argument(Definition_.Key + " must be a boolean");
            return Value_;
        case EParameterValueType::String:
        {
            if (!Value_.Is<std::string>()) throw std::invalid_argument(Definition_.Key + " must be a string");
            const auto _Text = Value_.To<std::string>();
            if (ValidateBounds_ && Definition_.Constraints.MinimumLength && _Text.size() < *Definition_.Constraints.MinimumLength)
                throw std::invalid_argument(Definition_.Key + " is shorter than its minimum length");
            if (ValidateBounds_ && Definition_.Constraints.MaximumLength && _Text.size() > *Definition_.Constraints.MaximumLength)
                throw std::invalid_argument(Definition_.Key + " exceeds its maximum length");
            return Value_;
        }
        case EParameterValueType::Enumeration:
            for (const auto& _Choice : Definition_.Choices)
                if (EnumValuesEqual(_Choice.Value, Value_))
                    return _Choice.Value; // Keep the descriptor's canonical type.
            throw std::invalid_argument(Definition_.Key + " is not an allowed value");
        }
        throw std::logic_error("unknown parameter type");
    }

    bool MatchesCondition(const SParameterCondition& Condition_, const ObjectMap& Values_)
    {
        // Use the descriptor's already parsed condition tree. Normalize all
        // parent values first so field order cannot change applicability.
        switch (Condition_.Kind)
        {
        case EConditionKind::Always:
            return true;
        case EConditionKind::Equals:
        case EConditionKind::NotEquals:
        {
            const auto _Iterator = Values_.find(Condition_.ParameterKey);
            if (_Iterator == Values_.end()) return false;
            const bool _Equal = EnumValuesEqual(_Iterator->second, Condition_.ExpectedValue);
            return Condition_.Kind == EConditionKind::Equals ? _Equal : !_Equal;
        }
        case EConditionKind::All:
            return std::all_of(Condition_.Children.begin(), Condition_.Children.end(),
                [&Values_](const auto& Child_) { return MatchesCondition(Child_, Values_); });
        case EConditionKind::Any:
            return std::any_of(Condition_.Children.begin(), Condition_.Children.end(),
                [&Values_](const auto& Child_) { return MatchesCondition(Child_, Values_); });
        case EConditionKind::Not:
            return Condition_.Children.size() == 1 && !MatchesCondition(Condition_.Children.front(), Values_);
        }
        return false;
    }

    void ValidateConditionReferences(
        SParameterCondition& Condition_,
        const std::vector<SParameterDefinition>& Parameters_,
        const std::string& strPath_)
    {
        switch (Condition_.Kind)
        {
        case EConditionKind::Always:
            return;
        case EConditionKind::Equals:
        case EConditionKind::NotEquals:
        {
            const auto _Parameter = std::find_if(
                Parameters_.begin(), Parameters_.end(),
                [&Condition_](const auto& Definition_) {
                    return Definition_.Key == Condition_.ParameterKey;
                });
            if (_Parameter == Parameters_.end())
                throw std::invalid_argument(strPath_ + " references unknown parameter "
                    + Condition_.ParameterKey);
            Condition_.ExpectedValue = NormalizeValue(*_Parameter, Condition_.ExpectedValue);
            return;
        }
        case EConditionKind::All:
        case EConditionKind::Any:
        case EConditionKind::Not:
            for (std::size_t _Index = 0; _Index < Condition_.Children.size(); ++_Index)
                ValidateConditionReferences(
                    Condition_.Children[_Index], Parameters_,
                    strPath_ + ".conditions[" + std::to_string(_Index) + "]");
            return;
        }
    }

    ObjectMap ConditionToPresentation(const SParameterCondition& Condition_)
    {
        ObjectMap _Result;
        switch (Condition_.Kind)
        {
        case EConditionKind::Always:
            return _Result;
        case EConditionKind::Equals:
        case EConditionKind::NotEquals:
            _Result["op"] = Condition_.Kind == EConditionKind::Equals ? std::string("eq") : std::string("ne");
            _Result["parameter"] = Condition_.ParameterKey;
            _Result["value"] = Condition_.ExpectedValue;
            return _Result;
        case EConditionKind::All:
        case EConditionKind::Any:
        {
            VariantArray _Children;
            for (const auto& _Child : Condition_.Children)
                _Children.emplace_back(ConditionToPresentation(_Child));
            _Result[Condition_.Kind == EConditionKind::All ? "all" : "any"] = _Children;
            return _Result;
        }
        case EConditionKind::Not:
            if (!Condition_.Children.empty())
                _Result["not"] = ConditionToPresentation(Condition_.Children.front());
            return _Result;
        }
        return _Result;
    }

    void ValidateNeutralModelGraph(SNeutralModel& Model_)
    {
        std::unordered_map<std::string, std::size_t> _NodeByKey;
        for (std::size_t _Index = 0; _Index < Model_.Geometry.size(); ++_Index)
        {
            const auto& _Key = Model_.Geometry[_Index].Key;
            ValidateStableKey(_Key, "geometry key");
            if (!_NodeByKey.emplace(_Key, _Index).second)
                throw std::invalid_argument("duplicate geometry key: " + _Key);
        }
        for (const auto& _Node : Model_.Geometry)
        {
            for (const auto& _Input : _Node.Inputs)
                if (!_NodeByKey.contains(_Input))
                    throw std::invalid_argument(_Node.Key + " references missing geometry input " + _Input);
        }

        std::vector<std::uint8_t> _State(Model_.Geometry.size(), 0);
        std::function<void(std::size_t)> _Visit = [&](std::size_t Index_) {
            if (_State[Index_] == 2) return;
            if (_State[Index_] == 1)
                throw std::invalid_argument("geometry dependency cycle at " + Model_.Geometry[Index_].Key);
            _State[Index_] = 1;
            for (const auto& _Input : Model_.Geometry[Index_].Inputs)
                _Visit(_NodeByKey.at(_Input));
            _State[Index_] = 2;
        };
        for (std::size_t _Index = 0; _Index < Model_.Geometry.size(); ++_Index) _Visit(_Index);

        std::unordered_set<std::string> _ItemKeys;
        for (const auto& _Item : Model_.Items)
        {
            ValidateStableKey(_Item.Key, "model item key");
            if (!_ItemKeys.emplace(_Item.Key).second)
                throw std::invalid_argument("duplicate model item key: " + _Item.Key);
            for (const auto& [_Purpose, _GeometryKey] : _Item.Representations)
            {
                ValidateStableKey(_Purpose, _Item.Key + " representation purpose");
                if (!_NodeByKey.contains(_GeometryKey))
                    throw std::invalid_argument(_Item.Key + " representation " + _Purpose + " references missing geometry " + _GeometryKey);
            }
        }
        for (const auto& _Item : Model_.Items)
            for (const auto& _Child : _Item.Children)
                if (!_ItemKeys.contains(_Child))
                    throw std::invalid_argument(_Item.Key + " references missing child item " + _Child);
        std::unordered_set<std::string> _OutputKeys;
        for (const auto& _Output : Model_.Outputs)
        {
            ValidateStableKey(_Output.Key, "output key");
            ValidateStableKey(_Output.Purpose, _Output.Key + " purpose");
            if (!_OutputKeys.emplace(_Output.Key).second)
                throw std::invalid_argument("duplicate output key: " + _Output.Key);
            for (const auto& _ItemKey : _Output.ItemKeys)
                if (!_ItemKeys.contains(_ItemKey))
                    throw std::invalid_argument(_Output.Key + " references missing output item " + _ItemKey);
        }

        std::unordered_set<std::string> _RelationshipKeys;
        for (const auto& _Relationship : Model_.Relationships)
        {
            ValidateStableKey(_Relationship.Key, "relationship key");
            ValidateStableKey(_Relationship.Kind, _Relationship.Key + " kind");
            if (!_RelationshipKeys.emplace(_Relationship.Key).second)
                throw std::invalid_argument("duplicate relationship key: " + _Relationship.Key);
            if (_Relationship.ItemKeys.size() < 2)
                throw std::invalid_argument(_Relationship.Key + " requires at least two items");
            for (const auto& _ItemKey : _Relationship.ItemKeys)
                if (!_ItemKeys.contains(_ItemKey))
                    throw std::invalid_argument(_Relationship.Key + " references missing item " + _ItemKey);
        }

        std::unordered_set<std::string> _TableKeys;
        for (const auto& _Table : Model_.Tables)
        {
            ValidateStableKey(_Table.Key, "table key");
            if (!_TableKeys.emplace(_Table.Key).second)
                throw std::invalid_argument("duplicate table key: " + _Table.Key);
            std::unordered_set<std::string> _ColumnKeys;
            for (const auto& _Column : _Table.Columns)
            {
                ValidateStableKey(_Column.Key, _Table.Key + " column key");
                if (!_ColumnKeys.emplace(_Column.Key).second)
                    throw std::invalid_argument(_Table.Key + " has duplicate column key: " + _Column.Key);
            }
            std::unordered_set<std::string> _RowKeys;
            for (const auto& _Row : _Table.Rows)
            {
                ValidateStableKey(_Row.Key, _Table.Key + " row key");
                if (!_RowKeys.emplace(_Row.Key).second)
                    throw std::invalid_argument(_Table.Key + " has duplicate row key: " + _Row.Key);
                if (!_Row.ItemKey.empty() && !_ItemKeys.contains(_Row.ItemKey))
                    throw std::invalid_argument(_Row.Key + " references missing item " + _Row.ItemKey);
            }
        }
    }

    void ValidateDisplayProperties(const Variant& Value_, const std::string& Path_,
        bool Connection_ = false, std::size_t Depth_ = 0)
    {
        if (Depth_ > 64)
            throw std::invalid_argument(Path_ + " is nested too deeply");
        if (Value_.Is<ObjectMap>())
        {
            static const std::unordered_set<std::string> _Forbidden{
                "manufacturing", "manufacturingRoute", "provenance",
                "assemblyGeometryProcesses",
                "endProcess", "manufacturingAxis", "manufacturingStartToEnd",
                "frameManufacturing", "sourceSpans", "designSegment",
                "cornerProcess", "jointProcess", "connectionProcess", "assemblyPlanning",
                "stockState", "stockInterval", "cutPlanes", "machiningFeatures", "slotFeatures"};
            static const std::unordered_set<std::string> _ConnectionForbidden{
                "recipe", "halfHole", "manufacturingCut", "stockAllowance", "trim",
                "processTemplateId", "templateId", "toolRef", "toolParameters",
                "insertionDepth", "totalClearanceDepth", "totalClearanceHeight"};
            for (const auto& [_Key, _Child] : RequireObject(Value_, Path_))
            {
                const auto _Name = _Key.starts_with("tubeDesigner.") ? _Key.substr(13) : _Key;
                if (_Key.starts_with("manufacturing.")
                    || _Key.starts_with("tubeDesigner.assemblyProcess")
                    || _Key == "tubeDesigner.assemblyGeometryProcesses"
                    || _Forbidden.contains(_Name)
                    || (Connection_ && _ConnectionForbidden.contains(_Name))
                    || (Connection_ && _Key == "geometry" && _Child.Is<std::string>()
                        && _Child.To<std::string>() == "outer-envelope-cope")
                    || (Connection_ && _Key == "kind" && _Child.Is<std::string>()
                        && _Child.To<std::string>() == "weld"))
                    throw std::invalid_argument(Path_ + "." + _Key + " is not display data");
                ValidateDisplayProperties(_Child, Path_ + "." + _Key, Connection_, Depth_ + 1);
            }
        }
        else if (Value_.Is<VariantArray>())
        {
            std::size_t _Index = 0;
            for (const auto& _Child : RequireArray(Value_, Path_))
                ValidateDisplayProperties(_Child, Path_ + "[" + std::to_string(_Index++) + "]",
                    Connection_, Depth_ + 1);
        }
    }

    const ObjectMap* ProfileConstraints(const SParameterDefinition& Field_)
    {
        const auto _Value = Find(Field_.Presentation, "profileConstraints");
        return _Value ? &RequireObject(*_Value, Field_.Key + ".presentation.profileConstraints") : nullptr;
    }

    std::set<std::string> ProfileSectionKinds(const ObjectMap& Constraints_, const std::string& Path_)
    {
        std::set<std::string> _Kinds;
        const auto _Value = Find(Constraints_, "sectionKinds");
        if (!_Value) return _Kinds;
        for (const auto& _Kind : RequireArray(*_Value, Path_ + ".sectionKinds"))
        {
            if (!_Kind.Is<std::string>() || _Kind.To<std::string>().empty()
                || !_Kinds.insert(_Kind.To<std::string>()).second)
                throw std::invalid_argument(Path_ + ".sectionKinds requires distinct non-empty strings");
        }
        if (_Kinds.empty()) throw std::invalid_argument(Path_ + ".sectionKinds cannot be empty");
        return _Kinds;
    }

    void ValidateProfileConstraints(const SParameterDefinition& Field_)
    {
        const auto _Constraints = ProfileConstraints(Field_);
        if (!_Constraints) return;
        const auto _Path = Field_.Key + ".presentation.profileConstraints";
        const auto _Kinds = ProfileSectionKinds(*_Constraints, _Path);
        if (const auto _Hollow = Find(*_Constraints, "hollow"); _Hollow && !_Hollow->Is<bool>())
            throw std::invalid_argument(_Path + ".hollow must be a boolean");
        const auto _Minimum = Find(*_Constraints, "minimumContourCount");
        const auto _Maximum = Find(*_Constraints, "maximumContourCount");
        const auto _Count = [&](const Variant* Value_, const char* Key_) {
            if (Value_ && (!Value_->Is<int>() && !Value_->Is<unsigned int>()
                && !Value_->Is<long long>() && !Value_->Is<unsigned long long>()))
                throw std::invalid_argument(_Path + "." + Key_ + " must be an integer");
            if (Value_ && (ToDouble(*Value_, _Path) < 1 || ToDouble(*Value_, _Path) > 1000))
                throw std::invalid_argument(_Path + "." + Key_ + " must be between one and 1000");
            const auto _Value = Value_ ? ToUInt64(*Value_, _Path + "." + Key_) : 0;
            return _Value;
        };
        const auto _Min = _Count(_Minimum, "minimumContourCount");
        const auto _Max = _Count(_Maximum, "maximumContourCount");
        if (_Minimum && _Maximum && _Min > _Max)
            throw std::invalid_argument(_Path + " has an inverted contour count range");
        for (const auto& _Choice : Field_.Choices)
            if (!_Kinds.empty() && (!_Choice.Value.Is<std::string>()
                || !_Kinds.contains(_Choice.Value.To<std::string>())))
                throw std::invalid_argument(_Path + ".sectionKinds excludes a declared profile choice");
    }

    ObjectMap InternalDesignProperties(ObjectMap Properties_)
    {
        // Current display facts and internal manufacturing facts have distinct
        // names. Adapt only the private construction document, never the public
        // design document or host parameter map.
        for (const auto& [_Source, _Target] : std::map<std::string, std::string>{{"partKind", "manufacturing.partKind"},
            {"sourcing", "manufacturing.sourcing"}, {"material", "manufacturing.material"}, {"materialGrade", "manufacturing.materialGrade"},
            {"materialCategory", "manufacturing.materialCategory"}, {"categoryKey", "manufacturing.categoryKey"},
            {"categoryName", "manufacturing.categoryName"}, {"modelReference", "manufacturing.modelReference"},
            {"plate", "manufacturing.plate"}})
            if (const auto _Value = Find(Properties_, _Source); _Value && !Find(Properties_, _Target)) Properties_[_Target] = *_Value;
        return Properties_;
    }
}

iCAX::TemplateRuntime::STemplateDescriptor
iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(const Variant& Document_)
{
    const auto& _Root = RequireObject(Document_, "template descriptor");
    if (RequireString(_Root, "schema", "template descriptor") != kTemplateDescriptorSchema)
        throw std::invalid_argument("unsupported template descriptor schema");
    const auto _SchemaVersion = Find(_Root, "schemaVersion");
    if (!_SchemaVersion || ToUInt64(*_SchemaVersion, "schemaVersion") != kTemplateDescriptorSchemaVersion)
        throw std::invalid_argument("unsupported template descriptor schema version");

    STemplateDescriptor _Result;
    _Result.ID = RequireString(_Root, "id", "template descriptor");
    ValidateStableKey(_Result.ID, "template descriptor.id");
    _Result.Version = RequireString(_Root, "version", "template descriptor");
    _Result.PackageDigest = OptionalString(_Root, "packageDigest");
    _Result.DisplayName = RequiredLocalizedText(_Root, "displayName", "template descriptor");
    _Result.Description = OptionalString(_Root, "description");
    if (const auto _Extensions = Find(_Root, "extensions"))
        _Result.Extensions = RequireObject(*_Extensions, "template descriptor.extensions");

    std::unordered_set<std::string> _GroupKeys;
    if (const auto _Groups = Find(_Root, "groups"))
    {
        std::size_t _Index = 0;
        for (const auto& _Value : RequireArray(*_Groups, "template descriptor.groups"))
        {
            const auto _Path = "template descriptor.groups[" + std::to_string(_Index++) + "]";
            const auto& _Group = RequireObject(_Value, _Path);
            SParameterGroup _Definition;
            _Definition.Key = RequireString(_Group, "key", _Path);
            ValidateStableKey(_Definition.Key, _Path + ".key");
            _Definition.DisplayName = RequiredLocalizedText(_Group, "displayName", _Path);
            if (const auto _Order = Find(_Group, "order"))
                _Definition.Order = static_cast<std::int32_t>(ToDouble(*_Order, _Path + ".order"));
            if (!_GroupKeys.emplace(_Definition.Key).second)
                throw std::invalid_argument("duplicate parameter group: " + _Definition.Key);
            _Result.Groups.push_back(std::move(_Definition));
        }
    }

    const auto _Parameters = Find(_Root, "parameters");
    if (!_Parameters) throw std::invalid_argument("template descriptor.parameters is required");
    std::unordered_set<std::string> _ParameterKeys;
    std::size_t _Index = 0;
    for (const auto& _Value : RequireArray(*_Parameters, "template descriptor.parameters"))
    {
        const auto _Path = "template descriptor.parameters[" + std::to_string(_Index++) + "]";
        const auto& _Parameter = RequireObject(_Value, _Path);
        SParameterDefinition _Definition;
        _Definition.Key = RequireString(_Parameter, "key", _Path);
        ValidateStableKey(_Definition.Key, _Path + ".key");
        if (!_ParameterKeys.emplace(_Definition.Key).second)
            throw std::invalid_argument("duplicate parameter key: " + _Definition.Key);
        _Definition.DisplayName = RequiredLocalizedText(_Parameter, "displayName", _Path);
        _Definition.Description = OptionalString(_Parameter, "description");
        _Definition.ValueType = ParseValueType(RequireString(_Parameter, "valueType", _Path));
        _Definition.Quantity = ParseQuantity(OptionalString(_Parameter, "quantity", "none"));
        _Definition.Unit = OptionalString(_Parameter, "unit");
        _Definition.Required = OptionalBool(_Parameter, "required", true);
        _Definition.ReadOnly = OptionalBool(_Parameter, "readOnly", false);
        _Definition.GroupKey = OptionalString(_Parameter, "group");
        if (!_Definition.GroupKey.empty() && !_GroupKeys.contains(_Definition.GroupKey))
            throw std::invalid_argument(_Path + ".group references an unknown group");
        if (const auto _Order = Find(_Parameter, "order"))
            _Definition.Order = static_cast<std::int32_t>(ToDouble(*_Order, _Path + ".order"));
        if (const auto _Default = Find(_Parameter, "defaultValue")) _Definition.DefaultValue = *_Default;
        else if (_Definition.Required) throw std::invalid_argument(_Path + ".defaultValue is required");

        if (const auto _Constraints = Find(_Parameter, "constraints"))
        {
            const auto& _Object = RequireObject(*_Constraints, _Path + ".constraints");
            _Definition.Constraints.Minimum = OptionalNumber(_Object, "minimum", _Path + ".constraints");
            _Definition.Constraints.Maximum = OptionalNumber(_Object, "maximum", _Path + ".constraints");
            _Definition.Constraints.Step = OptionalNumber(_Object, "step", _Path + ".constraints");
            if (const auto _Min = Find(_Object, "minimumLength"))
                _Definition.Constraints.MinimumLength = ToUInt64(*_Min, _Path + ".constraints.minimumLength");
            if (const auto _Max = Find(_Object, "maximumLength"))
                _Definition.Constraints.MaximumLength = ToUInt64(*_Max, _Path + ".constraints.maximumLength");
            _Definition.Constraints.Pattern = OptionalString(_Object, "pattern");
        }
        if (_Definition.Constraints.Minimum && _Definition.Constraints.Maximum
            && *_Definition.Constraints.Minimum > *_Definition.Constraints.Maximum)
            throw std::invalid_argument(_Path + " has an inverted numeric range");

        if (const auto _Choices = Find(_Parameter, "choices"))
        {
            std::size_t _ChoiceIndex = 0;
            for (const auto& _ChoiceValue : RequireArray(*_Choices, _Path + ".choices"))
            {
                const auto _ChoicePath = _Path + ".choices[" + std::to_string(_ChoiceIndex++) + "]";
                const auto& _ChoiceObject = RequireObject(_ChoiceValue, _ChoicePath);
                const auto _Choice = Find(_ChoiceObject, "value");
                if (!_Choice) throw std::invalid_argument(_ChoicePath + ".value is required");
                SParameterChoice _DefinitionChoice;
                _DefinitionChoice.Value = *_Choice;
                _DefinitionChoice.DisplayName = RequiredLocalizedText(_ChoiceObject, "displayName", _ChoicePath);
                _DefinitionChoice.Description = OptionalString(_ChoiceObject, "description");
                if (std::any_of(
                    _Definition.Choices.begin(), _Definition.Choices.end(),
                    [&_DefinitionChoice](const auto& Existing_) {
                        return EnumValuesEqual(Existing_.Value, _DefinitionChoice.Value);
                    }))
                {
                    throw std::invalid_argument(_ChoicePath + ".value duplicates an earlier choice");
                }
                _Definition.Choices.push_back(std::move(_DefinitionChoice));
            }
        }
        if (_Definition.ValueType == EParameterValueType::Enumeration && _Definition.Choices.empty())
            throw std::invalid_argument(_Path + ".choices cannot be empty for an enum parameter");
        _Definition.VisibleWhen = ParseCondition(Find(_Parameter, "visibleWhen"), _Path + ".visibleWhen");
        _Definition.EnabledWhen = ParseCondition(Find(_Parameter, "enabledWhen"), _Path + ".enabledWhen");
        if (const auto _Presentation = Find(_Parameter, "presentation"))
            _Definition.Presentation = RequireObject(*_Presentation, _Path + ".presentation");
        ValidateProfileConstraints(_Definition);
        if (!_Definition.DefaultValue.Is<std::monostate>())
            _Definition.DefaultValue = NormalizeValue(_Definition, _Definition.DefaultValue);
        _Result.Parameters.push_back(std::move(_Definition));
    }
    for (auto& _Definition : _Result.Parameters)
    {
        ValidateConditionReferences(
            _Definition.VisibleWhen, _Result.Parameters,
            "template descriptor.parameters." + _Definition.Key + ".visibleWhen");
        ValidateConditionReferences(
            _Definition.EnabledWhen, _Result.Parameters,
            "template descriptor.parameters." + _Definition.Key + ".enabledWhen");
    }
    ValidateProductProfileOverrides(_Result, {});
    return _Result;
}

void iCAX::TemplateRuntime::CTemplateCodec::ValidateProductProfileOverrides(
    const STemplateDescriptor& Descriptor_, const ObjectMap& Overrides_)
{
    if (Overrides_.size() > 64) throw std::invalid_argument("tubeDesignerProfileOverrides has too many entries");
    const auto _Roles = Find(Descriptor_.Extensions, "resourceRoles");
    const auto _Profiles = _Roles ? Find(RequireObject(*_Roles, "resourceRoles"), "profiles") : nullptr;
    if (!_Profiles && !Overrides_.empty()) throw std::invalid_argument("product has no profile resource roles");
    if (!_Profiles) return;
    const auto& _Declared = RequireObject(*_Profiles, "resourceRoles.profiles");
    for (const auto& [_Role, _Declaration] : _Declared)
    {
        if (_Role.empty() || _Role.size() > 80) throw std::invalid_argument("profile resource role is invalid");
        const auto _Parameter = RequireString(RequireObject(_Declaration, "profile role"), "parameter", "profile role");
        const auto _Field = std::find_if(Descriptor_.Parameters.begin(), Descriptor_.Parameters.end(),
            [&](const auto& Field_) { return Field_.Key == _Parameter; });
        if (_Field == Descriptor_.Parameters.end() || _Field->ValueType != EParameterValueType::Enumeration)
            throw std::invalid_argument("profile resource role must reference an enum parameter");
        ValidateProfileConstraints(*_Field);
    }
    for (const auto& [_Role, _Value] : Overrides_)
    {
        const auto _Declaration = Find(_Declared, _Role);
        if (_Role.empty() || _Role.size() > 80 || !_Declaration)
            throw std::invalid_argument("product uses an undeclared profile resource role");
        const auto _Parameter = RequireString(RequireObject(*_Declaration, "profile role"), "parameter", "profile role");
        const auto _Field = std::find_if(Descriptor_.Parameters.begin(), Descriptor_.Parameters.end(),
            [&](const auto& Field_) { return Field_.Key == _Parameter; });
        if (_Field == Descriptor_.Parameters.end()) throw std::invalid_argument("profile role references a missing parameter");
        ValidateProfileConstraints(*_Field);
        const auto _Constraints = ProfileConstraints(*_Field);
        const auto _Kinds = _Constraints ? ProfileSectionKinds(*_Constraints, _Parameter + ".profileConstraints") : std::set<std::string>{};
        if (_Kinds.empty()) throw std::invalid_argument("profile resource role requires a declared sectionKinds whitelist");
        const auto& _Profile = RequireObject(_Value, "profile override " + _Role);
        const auto _Version = Find(_Profile, "schemaVersion");
        if (RequireString(_Profile, "schema", "profile override") != "icax.imported-tube-profile"
            || !_Version || (!_Version->Is<int>() && !_Version->Is<unsigned int>()
                && !_Version->Is<long long>() && !_Version->Is<unsigned long long>())
            || ToUInt64(*_Version, "profile override.schemaVersion") != 1)
            throw std::invalid_argument("unsupported product profile snapshot schema");
        const auto _ResourceKind = RequireString(_Profile, "kind", "profile override");
        if (_ResourceKind != "fixed-section" && _ResourceKind != "profile-package")
            throw std::invalid_argument("unsupported product profile resource kind");
        const auto _Kind = RequireString(_Profile, "sectionKind", "profile override");
        if (!_Kinds.contains(_Kind)) throw std::invalid_argument("profile section kind is not applicable to role " + _Role);
        const auto _Contours = Find(_Profile, "contours");
        if (!_Contours || RequireArray(*_Contours, "profile override.contours").empty()
            || RequireArray(*_Contours, "profile override.contours").size() > 1000)
            throw std::invalid_argument("profile override requires non-empty contours");
        const auto _Count = RequireArray(*_Contours, "profile override.contours").size();
        if (const auto _DeclaredCount = Find(_Profile, "contourCount"); _DeclaredCount)
            if ((!_DeclaredCount->Is<int>() && !_DeclaredCount->Is<unsigned int>()
                    && !_DeclaredCount->Is<long long>() && !_DeclaredCount->Is<unsigned long long>())
                || ToDouble(*_DeclaredCount, "profile override.contourCount") != static_cast<double>(_Count))
                throw std::invalid_argument("profile contourCount does not match its contours");
        if (const auto _Hollow = Find(_Profile, "hollow"); _Hollow
            && (!_Hollow->Is<bool>() || _Hollow->To<bool>() != (_Count > 1)))
            throw std::invalid_argument("profile hollow flag does not match its contours");
        if (const auto _Hollow = Find(*_Constraints, "hollow"); _Hollow && _Hollow->To<bool>() != (_Count > 1))
            throw std::invalid_argument("profile hollow section is not applicable to role " + _Role);
        if (const auto _Min = Find(*_Constraints, "minimumContourCount"); _Min && _Count < ToUInt64(*_Min, "minimumContourCount"))
            throw std::invalid_argument("profile has too few contours for role " + _Role);
        if (const auto _Max = Find(*_Constraints, "maximumContourCount"); _Max && _Count > ToUInt64(*_Max, "maximumContourCount"))
            throw std::invalid_argument("profile has too many contours for role " + _Role);
    }
}

void iCAX::TemplateRuntime::CTemplateCodec::ValidateConsumedProductProfileOverrides(
    const STemplateDescriptor& Descriptor_, const ObjectMap& Overrides_,
    const std::vector<std::string>& ProfileRolesConsumed_)
{
    if (ProfileRolesConsumed_.size() > 64)
        throw std::invalid_argument("product template consumed too many profile roles");
    std::set<std::string> _Seen;
    ObjectMap _Consumed;
    for (const auto& _Role : ProfileRolesConsumed_)
    {
        const auto _Value = Find(Overrides_, _Role);
        if (_Role.empty() || _Role.size() > 80 || !_Seen.insert(_Role).second || !_Value)
            throw std::invalid_argument("product template consumed role is duplicate or missing its input override");
        _Consumed.emplace(_Role, *_Value);
    }
    ValidateProductProfileOverrides(Descriptor_, _Consumed);
}

iCAX::Data::ObjectMap
iCAX::TemplateRuntime::CTemplateCodec::AdaptDisplayModel(const Variant& Document_)
{
    const auto& _Root = RequireObject(Document_, "display model");
    if (RequireString(_Root, "schema", "display model") != kDisplayModelSchema)
        throw std::invalid_argument("unsupported display model schema");
    const auto _Version = Find(_Root, "schemaVersion");
    if (!_Version || ToDouble(*_Version, "display model.schemaVersion") != kDisplayModelSchemaVersion)
        throw std::invalid_argument("unsupported display model schema version");
    const std::set<std::string> _Fields{"schema", "schemaVersion", "coordinateSystem", "lengthUnit", "resources", "items", "roots", "annotations"};
    for (const auto& [_Key, _Value] : _Root)
        if (!_Fields.contains(_Key)) throw std::invalid_argument("display model contains unsupported field " + _Key);
    for (const auto& _Key : _Fields)
        if (!Find(_Root, _Key)) throw std::invalid_argument("display model requires " + _Key);
    if (RequireString(_Root, "lengthUnit", "display model") != "mm")
        throw std::invalid_argument("display model length unit must be mm");
    const auto _Resources = Find(_Root, "resources"), _Items = Find(_Root, "items"), _Roots = Find(_Root, "roots");
    if (!_Resources || !_Items || !_Roots)
        throw std::invalid_argument("display model.resources, .items and .roots are required");
    const auto& _ResourceDefinitions = RequireArray(*_Resources, "display model.resources");
    const auto& _ItemDefinitions = RequireArray(*_Items, "display model.items");
    const auto _RootKeys = ToStringVector(_Roots, "display model.roots");
    VariantArray _AdaptedItems;
    std::unordered_map<std::string, std::size_t> _ItemIndices;
    std::vector<std::vector<std::string>> _Children;
    std::vector<bool> _HasGeometry;
    for (const auto& _Value : _ItemDefinitions)
    {
        const auto _Index = _AdaptedItems.size();
        const auto _Path = "display model.items[" + std::to_string(_Index) + "]";
        const auto& _Item = RequireObject(_Value, _Path);
        for (const auto& [_Key, _Entry] : _Item)
            if (_Key != "key" && _Key != "displayName" && _Key != "geometry"
                && _Key != "children" && _Key != "properties")
                throw std::invalid_argument(_Path + " contains unsupported field " + _Key);
        const auto _Key = RequireString(_Item, "key", _Path);
        ValidateStableKey(_Key, _Path + ".key");
        if (!_ItemIndices.emplace(_Key, _Index).second)
            throw std::invalid_argument("duplicate display item key: " + _Key);
        (void)RequiredLocalizedText(_Item, "displayName", _Path);
        auto _AdaptedItem = _Item;
        ObjectMap _Representations;
        if (const auto _Geometry = Find(_Item, "geometry"))
        {
            (void)RequireObject(*_Geometry, _Path + ".geometry");
            _Representations["result"] = *_Geometry;
        }
        _HasGeometry.push_back(!_Representations.empty());
        _AdaptedItem.erase("geometry");
        _AdaptedItem["representations"] = std::move(_Representations);
        _Children.push_back(ToStringVector(Find(_Item, "children"), _Path + ".children"));
        if (const auto _Properties = Find(_Item, "properties"))
        {
            const auto& _PropertyValues = RequireObject(*_Properties, _Path + ".properties");
            ValidateDisplayProperties(*_Properties, _Path + ".properties");
            if (const auto _Plate = Find(_PropertyValues, "plate"))
                ValidatePlateComponent(*_Plate, _Path + ".properties.plate");
            _AdaptedItem["properties"] = InternalDesignProperties(_PropertyValues);
        }
        _AdaptedItems.emplace_back(std::move(_AdaptedItem));
    }
    std::vector<std::size_t> _ParentCounts(_AdaptedItems.size(), 0);
    for (std::size_t _Index = 0; _Index < _Children.size(); ++_Index)
        for (const auto& _Child : _Children[_Index])
        {
            const auto _Found = _ItemIndices.find(_Child);
            if (_Found == _ItemIndices.end())
                throw std::invalid_argument("display item references missing child " + _Child);
            if (++_ParentCounts[_Found->second] > 1)
                throw std::invalid_argument("display item has multiple parents: " + _Child);
        }
    std::vector<std::uint8_t> _States(_AdaptedItems.size(), 0);
    std::function<void(std::size_t)> _ValidateTree = [&](std::size_t Index_) {
        if (_States[Index_] == 2) return;
        if (_States[Index_] == 1)
            throw std::invalid_argument("display item hierarchy contains a cycle");
        _States[Index_] = 1;
        for (const auto& _Child : _Children[Index_]) _ValidateTree(_ItemIndices.at(_Child));
        _States[Index_] = 2;
    };
    for (std::size_t _Index = 0; _Index < _AdaptedItems.size(); ++_Index) _ValidateTree(_Index);
    std::unordered_set<std::string> _SeenRoots;
    for (const auto& _Key : _RootKeys)
    {
        const auto _Found = _ItemIndices.find(_Key);
        if (_Found == _ItemIndices.end() || !_SeenRoots.emplace(_Key).second)
            throw std::invalid_argument("display model has a missing or duplicate root: " + _Key);
        if (_ParentCounts[_Found->second] != 0)
            throw std::invalid_argument("display root is a child item: " + _Key);
    }
    for (const auto& [_Key, _Index] : _ItemIndices)
        if (_ParentCounts[_Index] == 0 && !_SeenRoots.contains(_Key))
            throw std::invalid_argument("display model omits a root item: " + _Key);
    VariantArray _OutputItems;
    std::function<void(const std::string&)> _CollectVisible = [&](const std::string& Key_) {
        const auto _Index = _ItemIndices.at(Key_);
        if (_HasGeometry[_Index]) _OutputItems.emplace_back(Key_);
        for (const auto& _Child : _Children[_Index]) _CollectVisible(_Child);
    };
    for (const auto& _Key : _RootKeys) _CollectVisible(_Key);

    VariantArray _Relationships;
    ObjectMap _Extensions{{"tubeDesigner.geometryPurpose", std::string("display")}};
    if (const auto _Annotations = Find(_Root, "annotations"))
    {
        (void)RequireArray(*_Annotations, "display model.annotations");
        ValidateDisplayProperties(*_Annotations, "display model.annotations");
        _Extensions["tubeDesigner.specificationAnnotations"] = *_Annotations;
    }
    ObjectMap _Adapted{{"schema", std::string(kNeutralModelSchema)},
        {"schemaVersion", static_cast<unsigned long long>(kNeutralModelResourcesSchemaVersion)},
        {"template", ObjectMap{{"id", std::string("icax.internal.display-adapter")}, {"version", std::string("1.0.0")}}},
        {"resources", _ResourceDefinitions}, {"items", std::move(_AdaptedItems)},
        {"outputs", VariantArray{ObjectMap{{"key", std::string("result")}, {"purpose", std::string("result")},
            {"items", std::move(_OutputItems)}}}},
        {"relationships", std::move(_Relationships)}, {"extensions", std::move(_Extensions)}};
    for (const auto _Key : {"coordinateSystem", "lengthUnit"})
        if (Find(_Root, _Key)) _Adapted[_Key] = RequireString(_Root, _Key, "display model");
    // Reuse resource, placement, alias and connection validation before exposing
    // an internal adapter result. The script's display document remains intact.
    (void)ParseNeutralModel(Variant(_Adapted));
    return _Adapted;
}

iCAX::Data::ObjectMap
iCAX::TemplateRuntime::CTemplateCodec::AdaptManufacturingModel(const Variant& Document_)
{
    const auto& _Root = RequireObject(Document_, "manufacturing model");
    if (RequireString(_Root, "schema", "manufacturing model") != kManufacturingModelSchema)
        throw std::invalid_argument("unsupported manufacturing model schema");
    const auto _Version = Find(_Root, "schemaVersion");
    if (_Version && ToDouble(*_Version, "manufacturing model.schemaVersion") == kManufacturingModelSchemaVersion)
        throw std::invalid_argument("manufacturing declarations version four require a separate design model");
    return AdaptManufacturingInputModel(Document_);
}

iCAX::Data::ObjectMap
iCAX::TemplateRuntime::CTemplateCodec::AdaptManufacturingInputModel(const Variant& Document_)
{
    const auto& _Root = RequireObject(Document_, "manufacturing input model");
    const auto _Version = Find(_Root, "schemaVersion")
        ? ToDouble(_Root.at("schemaVersion"), "manufacturing input schemaVersion") : 0;
    if (RequireString(_Root, "schema", "manufacturing input model") != kManufacturingModelSchema
        || (_Version != kManufacturingDesignInputModelSchemaVersion && _Version != kManufacturingInputModelSchemaVersion))
        throw std::invalid_argument("unsupported manufacturing input model schema");
    const std::set<std::string> _Fields{"schema", "schemaVersion", "coordinateSystem", "lengthUnit",
        "resources", "items", "roots", "sourceMappings", "processes"};
    for (const auto& [_Key, _Value] : _Root)
        if (!_Fields.contains(_Key)) throw std::invalid_argument("manufacturing input contains unsupported field " + _Key);
    for (const auto& _Key : _Fields)
        if (!Find(_Root, _Key)) throw std::invalid_argument("manufacturing input." + _Key + " is required");
    if (RequireString(_Root, "lengthUnit", "manufacturing input") != "mm")
        throw std::invalid_argument("manufacturing input length unit must be mm");
    const std::function<void(const Variant&, const std::string&, unsigned)> _ValidateInput =
        [&](const Variant& Value_, const std::string& Path_, unsigned Depth_) {
        if (Depth_ > 64) throw std::invalid_argument(Path_ + " is nested too deeply");
        if (Value_.Is<ObjectMap>())
            for (const auto& [_Key, _Value] : RequireObject(Value_, Path_))
            {
                if (_Key == "result" || _Key == "applicable" || _Key == "functionDigest"
                    || _Key == "executionSignature" || _Key == "resolvedPlan" || _Key == "targetGeometry"
                    || _Key == "resultGeometry" || _Key == "assemblyProcessPlan" || _Key == "assemblyGeometryProcesses"
                    || _Key == "resolvedOperations" || _Key == "planDigest" || _Key == "nativeContactChecks"
                    || _Key == "materialRequirements" || _Key == "forming"
                    || _Key == "tubeDesigner.frameManufacturing" || _Key == "tubeDesigner.cornerProcess"
                    || _Key == "tubeDesigner.endProcess" || _Key == "tubeDesigner.sourceSpans"
                    || _Key == "tubeDesigner.assemblyOutputSets"
                    || _Key.starts_with("tubeDesigner.assemblyProcess") || _Key == "tubeDesigner.assemblyGeometryProcesses")
                    throw std::invalid_argument(Path_ + " contains executed manufacturing data " + _Key);
                _ValidateInput(_Value, Path_ + "." + _Key, Depth_ + 1);
            }
        else if (Value_.Is<VariantArray>())
            for (const auto& _Value : RequireArray(Value_, Path_)) _ValidateInput(_Value, Path_ + "[]", Depth_ + 1);
        else if ((Value_.Is<double>() && !std::isfinite(Value_.To<double>()))
            || (Value_.Is<float>() && !std::isfinite(Value_.To<float>())))
            throw std::invalid_argument(Path_ + " must contain finite values");
    };
    const auto _Vector = [](const Variant& Value_, const std::string& Path_) {
        const auto& _Values = RequireArray(Value_, Path_);
        if (_Values.size() != 3) throw std::invalid_argument(Path_ + " requires three coordinates");
        std::array<double, 3> _Result;
        for (std::size_t _Index = 0; _Index < 3; ++_Index)
        {
            _Result[_Index] = ToDouble(_Values[_Index], Path_);
            if (!std::isfinite(_Result[_Index])) throw std::invalid_argument(Path_ + " must be finite");
        }
        return _Result;
    };
    const auto& _Resources = RequireArray(_Root.at("resources"), "manufacturing resources");
    std::map<std::string, std::string> _ResourceOperators;
    for (const auto& _Value : _Resources)
    {
        const auto& _Node = RequireObject(_Value, "manufacturing resource");
        _ResourceOperators.emplace(RequireString(_Node, "key", "manufacturing resource"),
            RequireString(_Node, "operator", "manufacturing resource"));
    }
    VariantArray _TreeItems;
    std::map<std::string, ObjectMap> _Properties;
    std::set<std::string> _ManufacturingItems;
    std::set<std::string> _DesignItems, _AllInputItems;
    std::map<std::string, std::set<std::string>> _PathSegments;
    for (const auto& _Value : RequireArray(_Root.at("items"), "manufacturing items"))
    {
        const auto& _Item = RequireObject(_Value, "manufacturing item");
        for (const auto& [_Key, _Entry] : _Item)
            if (_Key != "key" && _Key != "displayName" && _Key != "geometry" && _Key != "manufacturingInput"
                && _Key != "componentReference" && _Key != "children" && _Key != "properties" && _Key != "designInput")
                throw std::invalid_argument("manufacturing item contains unsupported field " + _Key);
        const auto _Key = RequireString(_Item, "key", "manufacturing item");
        if (!_AllInputItems.emplace(_Key).second) throw std::invalid_argument("duplicate manufacturing item");
        const auto _Children = ToStringVector(Find(_Item, "children"), "manufacturing children");
        const auto _Geometry = Find(_Item, "geometry"), _Input = Find(_Item, "manufacturingInput"),
            _Component = Find(_Item, "componentReference"), _Design = Find(_Item, "designInput");
        const auto _Count = unsigned(_Geometry != nullptr) + unsigned(_Input != nullptr) + unsigned(_Component != nullptr) + unsigned(_Design != nullptr);
        if (_Count > 1 || (_Count && !_Children.empty()) || (!_Count && _Children.empty()))
            throw std::invalid_argument(_Key + " requires one initial input or non-empty group children");
        if (_Geometry || _Input || _Component) _ManufacturingItems.insert(_Key);
        ObjectMap _Values;
        if (const auto _PropertiesValue = Find(_Item, "properties"))
        {
            _Values = RequireObject(*_PropertiesValue, _Key + ".properties");
            if (Find(_Values, "manufacturing.sourceMembers"))
                throw std::invalid_argument("manufacturing sources belong in sourceMappings");
            _ValidateInput(*_PropertiesValue, _Key + ".properties", 0);
        }
        _Properties.emplace(_Key, _Values);
        if (_Design)
        {
            if (_Version != kManufacturingDesignInputModelSchemaVersion)
                throw std::invalid_argument("designInput requires manufacturing input version three");
            const auto& _Member = RequireObject(*_Design, _Key + ".designInput");
            const std::set<std::string> _MemberFields{"kind", "profileResource", "start", "end", "sectionFrame"};
            for (const auto& [_Field, _Entry] : _Member)
                if (!_MemberFields.contains(_Field)) throw std::invalid_argument("unsupported design input field " + _Field);
            if (_Member.size() != _MemberFields.size() || RequireString(_Member, "kind", _Key) != "tube-member")
                throw std::invalid_argument("design input requires a complete tube-member");
            const auto _Profile = RequireString(_Member, "profileResource", _Key);
            if (!_ResourceOperators.contains(_Profile) || _ResourceOperators.at(_Profile) != "profile2d")
                throw std::invalid_argument("design tube-member requires an existing profile2d resource");
            const auto _ProfileProperty = Find(_Values, "tubeDesigner.profile");
            if (!_ProfileProperty || RequireString(RequireObject(*_ProfileProperty, _Key + ".tubeDesigner.profile"),
                "sectionResource", _Key) != _Profile)
                throw std::invalid_argument("design member section differs from its own tube profile");
            for (const auto _Derived : {"length", "stockLength", "station", "stockStart", "startReserve", "endReserve",
                "bendAllowance", "stockState", "formingOrder"})
                if (Find(_Values, _Derived)) throw std::invalid_argument("design input contains manufacturing route or stock dimension");
            const auto _Start = _Vector(_Member.at("start"), "design member start"), _End = _Vector(_Member.at("end"), "design member end");
            const auto _Frame = ParseRigidItemPlacement(_Member.at("sectionFrame"), "design member sectionFrame");
            const auto _Origin = _Vector(_Frame.at("origin"), "design member origin"), _Axis = _Vector(_Frame.at("zAxis"), "design member zAxis");
            double _Length2 = 0;
            for (std::size_t _Index = 0; _Index < 3; ++_Index)
            {
                const auto _Delta = _End[_Index] - _Start[_Index]; _Length2 += _Delta * _Delta;
                if (std::abs(_Origin[_Index] - _Start[_Index]) > 1e-7)
                    throw std::invalid_argument("design member origin differs from its start");
            }
            if (_Length2 <= 1e-14) throw std::invalid_argument("design member has zero length");
            for (std::size_t _Index = 0; _Index < 3; ++_Index)
                if (std::abs(_Axis[_Index] - (_End[_Index] - _Start[_Index]) / std::sqrt(_Length2)) > 1e-7)
                    throw std::invalid_argument("design member frame zAxis must follow its endpoints");
            _DesignItems.insert(_Key);
        }
        if (_Component)
        {
            const auto& _Reference = RequireObject(*_Component, _Key + ".componentReference");
            if (_Reference.empty()) throw std::invalid_argument("component reference must contain actual inputs");
            if (const auto _Ref = Find(_Reference, "ref"))
            {
                const auto& _ResourceRef = RequireObject(*_Ref, "component ref");
                (void)RequireString(_ResourceRef, "scope", "component ref");
                (void)RequireString(_ResourceRef, "id", "component ref");
            }
            else if (Find(_Reference, "reference"))
                (void)RequireString(_Reference, "reference", "component reference");
            else
            {
                (void)RequireString(_Reference, "scope", "component reference");
                (void)RequireString(_Reference, "id", "component reference");
            }
            _ValidateInput(*_Component, _Key + ".componentReference", 0);
            if (const auto _Placement = Find(_Reference, "placement"))
                (void)ParseRigidItemPlacement(*_Placement, _Key + ".componentReference.placement");
        }
        if (_Input)
        {
            if (_Version == kManufacturingDesignInputModelSchemaVersion)
                throw std::invalid_argument("version three product input cannot pre-plan manufacturing paths");
            const auto& _Manufacturing = RequireObject(*_Input, _Key + ".manufacturingInput");
            for (const auto& [_Field, _Entry] : _Manufacturing)
                if (_Field != "kind" && _Field != "profileResource" && _Field != "path" && _Field != "formingOrder")
                    throw std::invalid_argument("unsupported manufacturing input field " + _Field);
            if (RequireString(_Manufacturing, "kind", _Key) != "tube-path")
                throw std::invalid_argument("unsupported manufacturing input kind");
            const auto _Profile = RequireString(_Manufacturing, "profileResource", _Key);
            if (!_ResourceOperators.contains(_Profile) || _ResourceOperators.at(_Profile) != "profile2d")
                throw std::invalid_argument("tube-path requires an existing profile2d resource");
            const auto _ProfileProperty = Find(_Values, "tubeDesigner.profile");
            if (!_ProfileProperty || RequireString(RequireObject(*_ProfileProperty, _Key + ".tubeDesigner.profile"),
                "sectionResource", _Key) != _Profile)
                throw std::invalid_argument("tube-path sectionResource differs from its own tube profile");
            for (const auto _Derived : {"length", "station", "stockStart", "startReserve", "endReserve", "bendAllowance"})
                if (Find(_Values, _Derived)) throw std::invalid_argument("tube-path properties contain unresolved manufacturing value " + std::string(_Derived));
            if (!Find(_Manufacturing, "path")) throw std::invalid_argument("tube-path requires path");
            const auto& _Path = RequireObject(_Manufacturing.at("path"), _Key + ".path");
            for (const auto& [_Field, _Entry] : _Path)
                if (_Field != "vertices" && _Field != "segments" && _Field != "closed"
                    && _Field != "startVertex" && _Field != "closure")
                    throw std::invalid_argument("unsupported tube-path field " + _Field);
            if (!Find(_Path, "vertices") || !Find(_Path, "segments") || !Find(_Path, "closed"))
                throw std::invalid_argument("tube-path requires vertices, segments and closed");
            const bool _Closed = OptionalBool(_Path, "closed", false);
            std::map<std::string, std::array<double, 3>> _Vertices;
            for (const auto& _VertexValue : RequireArray(_Path.at("vertices"), "tube-path vertices"))
            {
                const auto& _Vertex = RequireObject(_VertexValue, "tube-path vertex");
                if (_Vertex.size() != 2 || !Find(_Vertex, "point")) throw std::invalid_argument("path vertex requires key and point");
                const auto _VertexKey = RequireString(_Vertex, "key", "tube-path vertex");
                ValidateStableKey(_VertexKey, "tube-path vertex key");
                if (!_Vertices.emplace(_VertexKey, _Vector(_Vertex.at("point"), "tube-path point")).second)
                    throw std::invalid_argument("duplicate tube-path vertex");
            }
            const auto _Start = RequireString(_Path, "startVertex", "tube-path");
            if (!_Vertices.contains(_Start)) throw std::invalid_argument("tube-path start vertex is missing");
            if (_Closed) (void)RequireString(_Path, "closure", "tube-path");
            auto _Previous = _Start;
            std::set<std::string> _Used{_Start};
            auto& _Segments = _PathSegments[_Key];
            const auto& _SegmentValues = RequireArray(_Path.at("segments"), "tube-path segments");
            if (_SegmentValues.empty()) throw std::invalid_argument("tube-path has no segments");
            for (const auto& _SegmentValue : _SegmentValues)
            {
                const auto& _Segment = RequireObject(_SegmentValue, "tube-path segment");
                if (_Segment.size() != 4 || !Find(_Segment, "sectionFrame"))
                    throw std::invalid_argument("path segment requires key, from, to and sectionFrame");
                const auto _SegmentKey = RequireString(_Segment, "key", "tube-path segment");
                ValidateStableKey(_SegmentKey, "tube-path segment key");
                const auto _From = RequireString(_Segment, "from", "tube-path segment"), _To = RequireString(_Segment, "to", "tube-path segment");
                if (!_Segments.emplace(_SegmentKey).second || !_Vertices.contains(_From)
                    || !_Vertices.contains(_To) || _From != _Previous || _From == _To)
                    throw std::invalid_argument("tube-path segment is missing, duplicated or unordered");
                const auto _Frame = ParseRigidItemPlacement(_Segment.at("sectionFrame"), "tube-path sectionFrame");
                const auto _Origin = _Vector(_Frame.at("origin"), "sectionFrame.origin"), _Axis = _Vector(_Frame.at("zAxis"), "sectionFrame.zAxis");
                double _Length2 = 0;
                for (std::size_t _Index = 0; _Index < 3; ++_Index)
                {
                    const auto _Delta = _Vertices.at(_To)[_Index] - _Vertices.at(_From)[_Index];
                    _Length2 += _Delta * _Delta;
                    if (std::abs(_Origin[_Index] - _Vertices.at(_From)[_Index]) > 1e-7)
                        throw std::invalid_argument("tube-path frame origin differs from segment start");
                }
                if (_Length2 <= 1e-14) throw std::invalid_argument("tube-path segment has zero length");
                for (std::size_t _Index = 0; _Index < 3; ++_Index)
                    if (std::abs(_Axis[_Index] - (_Vertices.at(_To)[_Index] - _Vertices.at(_From)[_Index]) / std::sqrt(_Length2)) > 1e-7)
                        throw std::invalid_argument("tube-path frame zAxis must follow its segment");
                _Used.insert(_To); _Previous = _To;
            }
            if ((_Closed && _Previous != _Start) || (!_Closed && _Previous == _Start) || _Used.size() != _Vertices.size())
                throw std::invalid_argument("tube-path closure or vertex coverage is inconsistent");
            std::set<std::string> _Order;
            for (const auto& _Vertex : ToStringVector(Find(_Manufacturing, "formingOrder"), "formingOrder"))
                if (!_Vertices.contains(_Vertex) || !_Order.emplace(_Vertex).second)
                    throw std::invalid_argument("formingOrder contains a missing or repeated vertex");
        }
        auto _TreeItem = _Item;
        _TreeItem.erase("properties"); _TreeItem.erase("manufacturingInput"); _TreeItem.erase("componentReference"); _TreeItem.erase("designInput");
        _TreeItems.emplace_back(std::move(_TreeItem));
    }
    ObjectMap _Tree{{"schema", std::string(kDisplayModelSchema)},
        {"schemaVersion", static_cast<unsigned long long>(kDisplayModelSchemaVersion)}, {"annotations", VariantArray{}},
        {"resources", _Resources}, {"items", std::move(_TreeItems)}, {"roots", _Root.at("roots")},
        {"coordinateSystem", RequireString(_Root, "coordinateSystem", "manufacturing model")},
        {"lengthUnit", RequireString(_Root, "lengthUnit", "manufacturing model")}};
    auto _Adapted = AdaptDisplayModel(Variant(_Tree));
    auto& _Items = std::get<VariantArray>(_Adapted.at("items").m_Value);
    for (auto& _Value : _Items)
    {
        auto& _Item = std::get<ObjectMap>(_Value.m_Value);
        _Item["properties"] = _Properties.at(RequireString(_Item, "key", "manufacturing item"));
    }
    std::set<std::string> _Mapped;
    for (const auto& _Value : RequireArray(_Root.at("sourceMappings"), "manufacturing sourceMappings"))
    {
        const auto& _Mapping = RequireObject(_Value, "manufacturing source mapping");
        if (_Mapping.size() != 2 || !Find(_Mapping, "sources")) throw std::invalid_argument("source mapping requires itemKey and sources");
        const auto _Target = RequireString(_Mapping, "itemKey", "source mapping");
        if ((!_ManufacturingItems.contains(_Target) && !_DesignItems.contains(_Target)) || !_Mapped.emplace(_Target).second)
            throw std::invalid_argument("source mapping target is missing or duplicated");
        const auto& _Sources = RequireArray(_Mapping.at("sources"), "manufacturing sources");
        if (_Sources.empty()) throw std::invalid_argument("manufacturing sources must not be empty");
        if (_DesignItems.contains(_Target) && _Sources.size() != 1)
            throw std::invalid_argument("design member mapping must preserve one independent display source");
        std::set<std::string> _SourceKeys;
        for (const auto& _SourceValue : _Sources)
        {
            const auto& _Source = RequireObject(_SourceValue, "manufacturing source");
            for (const auto& [_Field, _Entry] : _Source)
                if (_Field != "itemKey" && _Field != "segments") throw std::invalid_argument("unsupported source field " + _Field);
            const auto _SourceKey = RequireString(_Source, "itemKey", "manufacturing source");
            if (_DesignItems.contains(_Target) && _Source.size() != 1)
                throw std::invalid_argument("design member mapping cannot contain manufacturing segment allocation");
            ValidateStableKey(_SourceKey, "manufacturing source.itemKey");
            if (!_SourceKeys.emplace(_SourceKey).second) throw std::invalid_argument("duplicate display source");
            if (const auto _Segments = Find(_Source, "segments"))
            {
                std::set<std::string> _SeenSegments;
                for (const auto& _SegmentValue : RequireArray(*_Segments, "source segments"))
                {
                    const auto& _Segment = RequireObject(_SegmentValue, "source segment");
                    if (_Segment.size() != 2 || !Find(_Segment, "sourceRange"))
                        throw std::invalid_argument("source segment requires segmentKey and sourceRange");
                    const auto _SegmentKey = RequireString(_Segment, "segmentKey", "source segment");
                    if (!_PathSegments.contains(_Target) || !_PathSegments.at(_Target).contains(_SegmentKey)
                        || !_SeenSegments.emplace(_SegmentKey).second) throw std::invalid_argument("source segment is missing or repeated");
                    const auto& _Range = RequireArray(_Segment.at("sourceRange"), "sourceRange");
                    if (_Range.size() != 2) throw std::invalid_argument("sourceRange requires two design fractions");
                    const auto _A = ToDouble(_Range[0], "sourceRange"), _B = ToDouble(_Range[1], "sourceRange");
                    if (!std::isfinite(_A) || !std::isfinite(_B) || _A < 0 || _A > 1 || _B < 0 || _B > 1 || _A == _B)
                        throw std::invalid_argument("sourceRange is outside the design interval");
                }
            }
        }
    }
    std::map<std::string, ObjectMap> _Definitions;
    std::map<std::string, std::vector<std::string>> _Dependencies;
    std::set<std::string> _OutputNamespaces, _ConsumedDesign;
    for (const auto& _Value : RequireArray(_Root.at("processes"), "manufacturing processes"))
    {
        const auto& _Process = RequireObject(_Value, "manufacturing process");
        if (_Process.size() != 3 || RequireString(_Process, "kind", "manufacturing process") != "assembly-process"
            || !Find(_Process, "definition")) throw std::invalid_argument("manufacturing requires pending assembly-process declarations");
        const auto _Key = RequireString(_Process, "key", "manufacturing process");
        ValidateStableKey(_Key, "manufacturing process.key");
        const auto& _Definition = RequireObject(_Process.at("definition"), "manufacturing definition");
        for (const auto& [_Field, _Entry] : _Definition)
            if (_Field != "templateId" && _Field != "processInput" && _Field != "parameters"
                && _Field != "processDrafts" && _Field != "targets" && _Field != "dependencies" && _Field != "outputs")
                throw std::invalid_argument("unsupported manufacturing process field " + _Field);
        if (!Find(_Definition, "processInput") || !Find(_Definition, "parameters") || !Find(_Definition, "processDrafts")
            || !Find(_Definition, "dependencies") || (bool(Find(_Definition, "targets")) == bool(Find(_Definition, "outputs"))))
            throw std::invalid_argument("manufacturing process requires fixed targets or generated outputs");
        if (const auto _Outputs = Find(_Definition, "outputs"))
        {
            if (_Version != kManufacturingDesignInputModelSchemaVersion)
                throw std::invalid_argument("generated outputs require manufacturing input version three");
            const auto& _Values = RequireObject(*_Outputs, "assembly generated outputs");
            if (_Values.empty()) throw std::invalid_argument("generated outputs must not be empty");
            for (const auto& [_Role, _Value] : _Values)
            {
                ValidateStableKey(_Role, "assembly output role");
                const auto& _Output = RequireObject(_Value, "assembly generated output");
                if (_Output.size() != 2 || RequireString(_Output, "kind", "assembly generated output") != "manufacturing-set")
                    throw std::invalid_argument("generated output requires a manufacturing-set namespace");
                const auto _Namespace = RequireString(_Output, "key", "assembly generated output");
                ValidateStableKey(_Namespace, "assembly output namespace");
                if (_AllInputItems.contains(_Namespace) || !_OutputNamespaces.emplace(_Namespace).second)
                    throw std::invalid_argument("assembly output namespace must be unique");
            }
        }
        ValidateStableKey(RequireString(_Definition, "templateId", "manufacturing process"), "assembly templateId");
        _ValidateInput(Variant(_Definition), "manufacturing process", 0);
        if (!_Definitions.emplace(_Key, _Definition).second) throw std::invalid_argument("duplicate manufacturing process");
        _Dependencies[_Key] = ToStringVector(Find(_Definition, "dependencies"), "manufacturing dependencies");
        for (const auto _Field : {"parameters", "processDrafts"})
            if (const auto _Entry = Find(_Definition, _Field)) (void)RequireObject(*_Entry, "manufacturing " + std::string(_Field));
    }
    std::map<std::string, unsigned> _States;
    std::function<void(const std::string&)> _Visit = [&](const std::string& Key_) {
        if (_States[Key_] == 2) return;
        if (_States[Key_] == 1) throw std::invalid_argument("manufacturing dependency cycle");
        _States[Key_] = 1;
        std::set<std::string> _Seen;
        for (const auto& _Dependency : _Dependencies.at(Key_))
        {
            if (!_Definitions.contains(_Dependency) || !_Seen.emplace(_Dependency).second)
                throw std::invalid_argument("manufacturing dependency is missing or repeated");
            _Visit(_Dependency);
        }
        _States[Key_] = 2;
    };
    for (const auto& [_Key, _Definition] : _Definitions) _Visit(_Key);
    for (const auto& [_Key, _Definition] : _Definitions)
    {
        const bool _Generated = Find(_Definition, "outputs") != nullptr;
        const auto& _Input = RequireObject(_Definition.at("processInput"), "assembly process input");
        for (const auto& [_Field, _Entry] : _Input)
            if (_Field != "schema" && _Field != "schemaVersion" && _Field != "parts" && _Field != "geometry" && _Field != "resources")
                throw std::invalid_argument("unsupported assembly input field " + _Field);
        const auto _InputVersion = Find(_Input, "schemaVersion") ? ToDouble(_Input.at("schemaVersion"), "assembly input schemaVersion") : 0;
        if (RequireString(_Input, "schema", "assembly input") != "icax.assembly-process-input"
            || (_InputVersion != 2 && !(_Version == 3 && _InputVersion == 3)) || (_Generated && _InputVersion != 3)
            || !Find(_Input, "parts") || !Find(_Input, "geometry"))
            throw std::invalid_argument("manufacturing process requires a supported assembly input version");
        (void)RequireObject(_Input.at("geometry"), "assembly input geometry");
        if (const auto _Entry = Find(_Input, "resources"))
            for (const auto& [_Role, _Value] : RequireObject(*_Entry, "assembly input resources"))
            {
                ValidateStableKey(_Role, "assembly resource role");
                const auto& _Resource = RequireObject(_Value, "assembly resource binding");
                if (_InputVersion == 2)
                {
                    if (!Find(_Resource, "ref")) throw std::invalid_argument("assembly resource binding requires an explicit ref");
                    continue;
                }
                if (_Resource.size() != 1 || !Find(_Resource, "ref"))
                    throw std::invalid_argument("assembly resource binding requires an explicit ref");
                const auto& _Reference = RequireObject(_Resource.at("ref"), "assembly resource ref");
                const auto _Scope = RequireString(_Reference, "scope", "assembly resource ref");
                if (_Scope != "system" && _Scope != "user" && _Scope != "template")
                    throw std::invalid_argument("unsupported assembly resource ref scope");
                (void)RequireString(_Reference, "id", "assembly resource ref");
                for (const auto& [_Field, _Entry] : _Reference)
                    if (_Field != "scope" && _Field != "id" && !(_Scope == "template" && _Field == "templateId"))
                        throw std::invalid_argument("unsupported assembly resource ref field");
                if (_Scope == "template") ValidateStableKey(RequireString(_Reference, "templateId", "assembly resource ref"), "assembly resource templateId");
            }
        const auto& _Parts = RequireObject(_Input.at("parts"), "assembly input parts");
        const ObjectMap _NoTargets;
        const auto& _Targets = _Generated ? _NoTargets : RequireObject(_Definition.at("targets"), "manufacturing targets");
        if (_Parts.empty() || (!_Generated && _Targets.empty())) throw std::invalid_argument("manufacturing process parts and targets must not be empty");
        for (const auto& [_Role, _Target] : _Targets)
        {
            if (!_Target.Is<std::string>() || !_ManufacturingItems.contains(_Target.To<std::string>()) || !_Parts.contains(_Role))
                throw std::invalid_argument("manufacturing target must reference an actual manufacturing object and role");
            const auto& _Binding = RequireObject(_Parts.at(_Role), "manufacturing target role");
            if (RequireString(_Binding, "scope", "manufacturing target role") != "manufacturing"
                || RequireString(_Binding, "itemKey", "manufacturing target role") != _Target.To<std::string>())
                throw std::invalid_argument("manufacturing process cannot target an observer");
        }
        for (const auto& [_Role, _Value] : _Parts)
        {
            ValidateStableKey(_Role, "assembly role");
            const auto& _Binding = RequireObject(_Value, "assembly role binding");
            const auto _Scope = RequireString(_Binding, "scope", "assembly role binding");
            for (const auto& [_Field, _Entry] : _Binding)
                if (_Field != "scope" && _Field != "data"
                    && !(_Scope == "manufacturing" && (_Field == "itemKey" || _Field == "state"))
                    && !(_Scope == "display" && (_Field == "itemKey" || _Field == "segment"))
                    && !(_Scope == "resource" && (_Field == "resourceKey" || _Field == "placement"))
                    && !(_Scope == "design" && _Field == "itemKeys")
                    && !(_Scope == "process-output" && (_Field == "processKey" || _Field == "outputKey")))
                    throw std::invalid_argument("unsupported assembly role binding field " + _Field);
            if (const auto _Data = Find(_Binding, "data")) (void)RequireObject(*_Data, "assembly actual input data");
            if (_Scope == "design")
            {
                if (!_Generated || _Version != 3 || _InputVersion != 3 || _Binding.size() != 2)
                    throw std::invalid_argument("design binding requires a generated-output process");
                const auto _Keys = ToStringVector(Find(_Binding, "itemKeys"), "assembly design items");
                std::set<std::string> _Seen;
                if (_Keys.empty()) throw std::invalid_argument("assembly design items must not be empty");
                for (const auto& _ItemKey : _Keys)
                {
                    if ((!_DesignItems.contains(_ItemKey) && !_ManufacturingItems.contains(_ItemKey)) || !_Seen.emplace(_ItemKey).second)
                        throw std::invalid_argument("assembly design input must reference distinct actual design items");
                    _ConsumedDesign.insert(_ItemKey);
                }
            }
            else if (_Scope == "process-output")
            {
                const auto _Producer = RequireString(_Binding, "processKey", "assembly output input"),
                    _OutputRole = RequireString(_Binding, "outputKey", "assembly output input");
                if (!_Generated || _Version != 3 || _InputVersion != 3 || _Binding.size() != 3 || !_Definitions.contains(_Producer)
                    || std::find(_Dependencies.at(_Key).begin(), _Dependencies.at(_Key).end(), _Producer) == _Dependencies.at(_Key).end())
                    throw std::invalid_argument("assembly output input requires an explicit producer dependency");
                const auto _Outputs = Find(_Definitions.at(_Producer), "outputs");
                if (!_Outputs || !RequireObject(*_Outputs, "producer outputs").contains(_OutputRole))
                    throw std::invalid_argument("assembly output input references an unknown producer output role");
            }
            else if (_Scope == "manufacturing")
            {
                const auto _ItemKey = RequireString(_Binding, "itemKey", "manufacturing role");
                if (!_ManufacturingItems.contains(_ItemKey) || !Find(_Binding, "state"))
                    throw std::invalid_argument("manufacturing role requires object and explicit state");
                const auto& _State = _Binding.at("state");
                if (_State.Is<std::string>())
                {
                    if (_State.To<std::string>() != "initial") throw std::invalid_argument("unsupported manufacturing state");
                }
                else
                {
                    const auto& _After = RequireObject(_State, "manufacturing state");
                    const auto _ProcessKey = RequireString(_After, "after", "manufacturing state");
                    if (_After.size() != 1 || !_Definitions.contains(_ProcessKey)
                        || std::find(_Dependencies.at(_Key).begin(), _Dependencies.at(_Key).end(), _ProcessKey) == _Dependencies.at(_Key).end())
                        throw std::invalid_argument("manufacturing after-state must be an explicit dependency");
                    bool _Produces = false;
                    if (const auto _Outputs = Find(_Definitions.at(_ProcessKey), "targets"))
                        for (const auto& [_OutputRole, _Output] : RequireObject(*_Outputs, "dependency targets"))
                            if (_Output.Is<std::string>() && _Output.To<std::string>() == _ItemKey) _Produces = true;
                    if (!_Produces) throw std::invalid_argument("dependency does not produce the requested manufacturing state");
                }
            }
            else if (_Scope == "display") ValidateStableKey(RequireString(_Binding, "itemKey", "display observer"), "display observer key");
            else if (_Scope == "resource")
            {
                if (!_ResourceOperators.contains(RequireString(_Binding, "resourceKey", "resource observer")) || !Find(_Binding, "placement"))
                    throw std::invalid_argument("resource observer requires resource and placement");
                (void)ParseRigidItemPlacement(_Binding.at("placement"), "resource observer placement");
            }
            else if (_Scope == "input")
            {
                if (!Find(_Binding, "data") || RequireObject(_Binding.at("data"), "inline observer").empty())
                    throw std::invalid_argument("inline observer must declare actual data");
            }
            else throw std::invalid_argument("unsupported assembly role scope");
        }
    }
    for (const auto& _Key : _DesignItems)
    {
        if (!_Mapped.contains(_Key)) throw std::invalid_argument("design input must retain its independent source mapping");
        if (!_ConsumedDesign.contains(_Key)) throw std::invalid_argument("design input must be consumed by a generated-output process");
    }
    std::map<std::string, std::vector<std::string>> _Children;
    for (const auto& _Value : _Items)
    {
        const auto& _Item = RequireObject(_Value, "manufacturing item");
        _Children[RequireString(_Item, "key", "manufacturing item")] = ToStringVector(Find(_Item, "children"), "manufacturing children");
    }
    VariantArray _OutputKeys;
    std::function<void(const std::string&)> _Collect = [&](const std::string& Key_) {
        if (_ManufacturingItems.contains(Key_)) _OutputKeys.emplace_back(Key_);
        for (const auto& _Child : _Children.at(Key_)) _Collect(_Child);
    };
    for (const auto& _Key : ToStringVector(Find(_Root, "roots"), "manufacturing roots")) _Collect(_Key);
    // Generated sets cannot be counted before assembly has allocated real parts.
    // Keep stable design items for validation, but expose no guessed stock output.
    if (!_OutputNamespaces.empty()) _OutputKeys.clear();
    _Adapted["template"] = ObjectMap{{"id", std::string("icax.internal.manufacturing-input")}, {"version", std::string("3.0.0")}};
    _Adapted["outputs"] = VariantArray{ObjectMap{{"key", std::string("result")}, {"purpose", std::string("result")}, {"items", _OutputKeys}}};
    _Adapted["extensions"] = ObjectMap{{"tubeDesigner.geometryPurpose", std::string("manufacturing")},
        {"tubeDesigner.manufacturingPartCount", static_cast<unsigned long long>(_OutputKeys.size())},
        {"tubeDesigner.manufacturingPartCountKnown", _OutputNamespaces.empty()},
        {"tubeDesigner.manufacturingProcessDeclarations", _Root.at("processes")}};
    // Unallocated inputs have no representation: this validates declarations
    // and object count without inventing final geometry or executing a process.
    (void)ParseNeutralModel(Variant(_Adapted));
    return _Adapted;
}

ObjectMap iCAX::TemplateRuntime::CTemplateCodec::ComposeManufacturingModel(
    const Variant& Definition_, const Variant& Design_)
{
    const auto& _Declaration = RequireObject(Definition_, "manufacturing declaration");
    const std::set<std::string> _Fields{"schema", "schemaVersion", "connections", "processes"};
    if (_Declaration.size() != _Fields.size()
        || RequireString(_Declaration, "schema", "manufacturing declaration") != kManufacturingModelSchema
        || !Find(_Declaration, "schemaVersion") || ToDouble(_Declaration.at("schemaVersion"), "manufacturing declaration schemaVersion") != kManufacturingModelSchemaVersion)
        throw std::invalid_argument("manufacturing version four contains only connections and process declarations");
    for (const auto& [_Key, _Value] : _Declaration)
        if (!_Fields.contains(_Key)) throw std::invalid_argument("unsupported manufacturing declaration field " + _Key);
    const std::function<void(const Variant&, unsigned)> _ValidatePending = [&](const Variant& Value_, unsigned Depth_) {
        if (Depth_ > 64) throw std::invalid_argument("manufacturing declaration is nested too deeply");
        if (Value_.Is<ObjectMap>())
            for (const auto& [_Key, _Value] : RequireObject(Value_, "manufacturing declaration"))
            {
                if (_Key == "result" || _Key == "applicable" || _Key == "functionDigest" || _Key == "executionSignature"
                    || _Key == "resolvedPlan" || _Key == "targetGeometry" || _Key == "resultGeometry"
                    || _Key == "resolvedOperations" || _Key == "planDigest" || _Key == "nativeContactChecks"
                    || _Key == "materialRequirements" || _Key == "forming" || _Key == "assemblyProcessPlan"
                    || _Key == "assemblyGeometryProcesses" || _Key == "tubeDesigner.frameManufacturing"
                    || _Key == "tubeDesigner.cornerProcess" || _Key == "tubeDesigner.endProcess" || _Key == "tubeDesigner.sourceSpans"
                    || _Key == "tubeDesigner.assemblyOutputSets" || _Key.starts_with("tubeDesigner.assemblyProcess"))
                    throw std::invalid_argument("manufacturing declaration contains executed data " + _Key);
                _ValidatePending(_Value, Depth_ + 1);
            }
        else if (Value_.Is<VariantArray>())
            for (const auto& _Value : RequireArray(Value_, "manufacturing declaration")) _ValidatePending(_Value, Depth_ + 1);
        else if ((Value_.Is<double>() && !std::isfinite(Value_.To<double>())) || (Value_.Is<float>() && !std::isfinite(Value_.To<float>())))
            throw std::invalid_argument("manufacturing declaration values must be finite");
    };
    _ValidatePending(Definition_, 0);
    const auto& _Design = RequireObject(Design_, "shared design model");
    (void)AdaptDisplayModel(Design_);
    auto _Processes = RequireArray(_Declaration.at("processes"), "manufacturing process declarations");
    auto _Items = RequireArray(_Design.at("items"), "shared design items");
    auto _Resources = RequireArray(_Design.at("resources"), "shared design resources");
    std::map<std::string, ObjectMap> _DesignItems;
    std::set<std::string> _ConsumedDesign, _ResourceKeys;
    for (const auto& _Value : _Items)
    {
        const auto& _Item = RequireObject(_Value, "shared design item");
        _DesignItems.emplace(RequireString(_Item, "key", "shared design item"), _Item);
    }
    for (const auto& _Value : _Resources)
        _ResourceKeys.insert(RequireString(RequireObject(_Value, "shared design resource"), "key", "shared design resource"));
    std::set<std::string> _SelectedItems, _SelectedTree;
    std::set<std::string> _DeclaredProcessKeys;
    bool _HasSelection = false;
    VariantArray _ExecutableProcesses;
    for (const auto& _Value : _Processes)
    {
        const auto& _Process = RequireObject(_Value, "manufacturing process declaration");
        const auto _ProcessKey = RequireString(_Process, "key", "manufacturing process declaration");
        if (!_DeclaredProcessKeys.emplace(_ProcessKey).second) throw std::invalid_argument("duplicate manufacturing process declaration");
        const auto& _Definition = RequireObject(_Process.at("definition"), "manufacturing process definition");
        if (RequireString(_Definition, "templateId", "manufacturing process definition") != "product-manufacturing-members")
        {
            _ExecutableProcesses.push_back(_Value); continue;
        }
        if (_HasSelection) throw std::invalid_argument("manufacturing can select one explicit fixed member set");
        _HasSelection = true;
        if (_Process.size() != 3 || RequireString(_Process, "kind", "manufacturing selection") != "assembly-process"
            || _Definition.size() != 6 || Find(_Definition, "outputs") || !Find(_Definition, "targets")
            || !Find(_Definition, "parameters") || !RequireObject(_Definition.at("parameters"), "selection parameters").empty()
            || !Find(_Definition, "processDrafts") || !RequireObject(_Definition.at("processDrafts"), "selection drafts").empty()
            || !Find(_Definition, "dependencies") || !RequireArray(_Definition.at("dependencies"), "selection dependencies").empty())
            throw std::invalid_argument("manufacturing member selection cannot declare machining or dependencies");
        ValidateStableKey(RequireString(_Process, "key", "manufacturing selection"), "manufacturing selection key");
        const auto& _Input = RequireObject(_Definition.at("processInput"), "selection input");
        if (_Input.size() != 4 || RequireString(_Input, "schema", "selection input") != "icax.assembly-process-input"
            || ToDouble(_Input.at("schemaVersion"), "selection input version") != 2
            || !RequireObject(_Input.at("geometry"), "selection geometry").empty())
            throw std::invalid_argument("manufacturing member selection requires fixed item references only");
        const auto& _Targets = RequireObject(_Definition.at("targets"), "selected manufacturing members"),
            _Parts = RequireObject(_Input.at("parts"), "selection member bindings");
        if (_Targets.empty() || _Targets.size() != _Parts.size()) throw std::invalid_argument("manufacturing selection requires distinct member roles");
        for (const auto& [_Role, _Value] : _Targets)
        {
            ValidateStableKey(_Role, "selected member role");
            if (!_Value.Is<std::string>()) throw std::invalid_argument("selected member requires an existing design item key");
            const auto _Key = _Value.To<std::string>();
            if (!_DesignItems.contains(_Key) || !Find(_DesignItems.at(_Key), "geometry") || !_SelectedItems.emplace(_Key).second || !_Parts.contains(_Role))
                throw std::invalid_argument("manufacturing selection references unknown, grouped or repeated design members");
            const auto& _Binding = RequireObject(_Parts.at(_Role), "selection member binding");
            if (_Binding.size() != 3 || RequireString(_Binding, "scope", "selection binding") != "manufacturing"
                || RequireString(_Binding, "itemKey", "selection binding") != _Key || RequireString(_Binding, "state", "selection binding") != "initial")
                throw std::invalid_argument("manufacturing selection must bind the same initial design item");
        }
    }
    _Processes = std::move(_ExecutableProcesses);
    if (_HasSelection)
    {
        std::map<std::string, std::string> _Parents;
        for (const auto& [_Key, _Item] : _DesignItems)
            for (const auto& _Child : ToStringVector(Find(_Item, "children"), "shared design children")) _Parents[_Child] = _Key;
        _SelectedTree = _SelectedItems;
        for (const auto& _Key : _SelectedItems)
        {
            auto _Parent = _Parents.find(_Key);
            while (_Parent != _Parents.end()) { _SelectedTree.insert(_Parent->second); _Parent = _Parents.find(_Parent->second); }
        }
        for (const auto& _Value : _Processes)
            if (Find(RequireObject(RequireObject(_Value, "manufacturing process").at("definition"), "manufacturing definition"), "outputs"))
                throw std::invalid_argument("fixed member selection cannot preselect a generated manufacturing set");
    }
    const auto _IdentityFrame = []() { return ObjectMap{{"origin", VariantArray{0.0, 0.0, 0.0}},
        {"xAxis", VariantArray{1.0, 0.0, 0.0}}, {"yAxis", VariantArray{0.0, 1.0, 0.0}}, {"zAxis", VariantArray{0.0, 0.0, 1.0}}}; };
    const auto _RelativeFrame = [&](const ObjectMap& Product_, const ObjectMap& Local_) {
        const char* _Axes[]{"xAxis", "yAxis", "zAxis"};
        ObjectMap _Result;
        for (const auto _Key : {"origin", "xAxis", "yAxis", "zAxis"})
        {
            VariantArray _Coordinates;
            const auto _Vector = RequireArray(Product_.at(_Key), "design frame vector");
            const auto _LocalOrigin = RequireArray(Local_.at("origin"), "local frame origin");
            for (const auto _Axis : _Axes)
            {
                const auto& _LocalAxis = RequireArray(Local_.at(_Axis), "local frame axis");
                double _Value = 0;
                for (std::size_t _Index = 0; _Index < 3; ++_Index)
                    _Value += (ToDouble(_Vector[_Index], "design frame") - (std::string(_Key) == "origin"
                        ? ToDouble(_LocalOrigin[_Index], "local origin") : 0.0)) * ToDouble(_LocalAxis[_Index], "local axis");
                _Coordinates.emplace_back(_Value);
            }
            _Result[_Key] = std::move(_Coordinates);
        }
        return _Result;
    };
    for (auto& _ProcessValue : _Processes)
    {
        auto _Process = RequireObject(_ProcessValue, "manufacturing process declaration");
        auto _Definition = RequireObject(_Process.at("definition"), "manufacturing process definition");
        auto _Input = RequireObject(_Definition.at("processInput"), "manufacturing process input");
        if (Find(RequireObject(_Input.at("geometry"), "manufacturing process geometry"), "connections"))
            throw std::invalid_argument("process geometry cannot duplicate top-level manufacturing connections");
        auto _Parts = RequireObject(_Input.at("parts"), "manufacturing input parts");
        const auto _ProcessKey = RequireString(_Process, "key", "manufacturing process");
        for (auto& [_Role, _Value] : _Parts)
        {
            auto _Binding = RequireObject(_Value, "manufacturing part binding");
            const auto _Scope = RequireString(_Binding, "scope", "manufacturing part binding");
            if (_Scope == "design")
                for (const auto& _Key : ToStringVector(Find(_Binding, "itemKeys"), "manufacturing design items"))
                {
                    if (!_DesignItems.contains(_Key) || !Find(_DesignItems.at(_Key), "geometry"))
                        throw std::invalid_argument("manufacturing binding references a missing shared design leaf");
                    _ConsumedDesign.insert(_Key);
                }
            else if (_Scope == "manufacturing" || _Scope == "display")
            {
                const auto _ItemKey = RequireString(_Binding, "itemKey", "manufacturing shared item binding");
                if (!_DesignItems.contains(_ItemKey) || !Find(_DesignItems.at(_ItemKey), "geometry"))
                    throw std::invalid_argument("manufacturing binding references an unknown shared design item");
                if (_HasSelection && _Scope == "manufacturing" && !_SelectedItems.contains(_ItemKey))
                    throw std::invalid_argument("processing binding is outside the explicitly selected manufacturing members");
                if (const auto _DataValue = _Scope == "manufacturing" ? Find(_Binding, "data") : nullptr)
                {
                    auto _Data = RequireObject(*_DataValue, "manufacturing binding data");
                    if (const auto _InitialValue = Find(_Data, "initialGeometry"))
                    {
                        auto _Initial = RequireObject(*_InitialValue, "manufacturing initial geometry relation");
                        if (_Initial.size() != 1 || !Find(_Initial, "itemPlacement"))
                            throw std::invalid_argument("shared initial geometry requires its item placement relation");
                        for (const auto& [_Field, _Entry] : _Initial)
                            if (_Field != "itemPlacement") throw std::invalid_argument("shared initial geometry cannot redeclare a resource");
                        const auto& _Geometry = RequireObject(_DesignItems.at(_ItemKey).at("geometry"), "shared item geometry");
                        const auto _LocalFrame = Find(_Initial, "itemPlacement")
                            ? ParseRigidItemPlacement(_Initial.at("itemPlacement"), "initial item placement") : _IdentityFrame();
                        const auto _ProductFrame = Find(_Geometry, "placement")
                            ? ParseRigidItemPlacement(_Geometry.at("placement"), "shared design placement") : _IdentityFrame();
                        const auto _Relative = _RelativeFrame(_ProductFrame, _LocalFrame);
                        auto _Resource = RequireString(_Geometry, "resource", "shared item geometry");
                        const auto _Identity = _IdentityFrame();
                        bool _IsIdentity = true;
                        for (const auto _Field : {"origin", "xAxis", "yAxis", "zAxis"})
                        {
                            const auto& _A = RequireArray(_Relative.at(_Field), "relative frame"), _B = RequireArray(_Identity.at(_Field), "identity frame");
                            for (std::size_t _Index = 0; _Index < 3; ++_Index)
                                if (std::abs(ToDouble(_A[_Index], "relative frame") - ToDouble(_B[_Index], "identity frame")) > 1e-8) _IsIdentity = false;
                        }
                        if (!_IsIdentity)
                        {
                            const auto _Key = "manufacturing.input." + _ProcessKey + "." + _Role;
                            if (!_ResourceKeys.emplace(_Key).second) throw std::invalid_argument("private initial geometry resource collides with shared design");
                            _Resources.emplace_back(ObjectMap{{"key", _Key}, {"operator", std::string("transform")},
                                {"inputs", VariantArray{_Resource}}, {"arguments", ObjectMap{{"placement", _Relative}}}});
                            _Resource = _Key;
                        }
                        _Initial["resource"] = _Resource; _Data["initialGeometry"] = std::move(_Initial);
                        _Binding["data"] = std::move(_Data);
                    }
                }
                _Value = std::move(_Binding);
            }
        }
        _Input["parts"] = std::move(_Parts);
        if (Find(_Definition, "outputs"))
        {
            auto _Geometry = RequireObject(_Input.at("geometry"), "assembly design geometry");
            if (Find(_Geometry, "connections")) throw std::invalid_argument("process geometry cannot duplicate top-level manufacturing connections");
            _Geometry["connections"] = _Declaration.at("connections"); _Input["geometry"] = std::move(_Geometry);
        }
        _Definition["processInput"] = std::move(_Input); _Process["definition"] = std::move(_Definition); _ProcessValue = std::move(_Process);
    }
    VariantArray _Mappings;
    for (auto& _ItemValue : _Items)
    {
        auto _Item = RequireObject(_ItemValue, "shared design item");
        const auto _Key = RequireString(_Item, "key", "shared design item");
        auto _Properties = Find(_Item, "properties") ? RequireObject(_Item.at("properties"), "design properties") : ObjectMap{};
        _Properties = InternalDesignProperties(std::move(_Properties));
        _Item["properties"] = _Properties;
        if (!Find(_Item, "geometry")) continue;
        // Every retained design leaf declares its source explicitly, including
        // fixed geometry and non-linear items. Allocation may replace this
        // identity with a richer mapping; no consumer guesses it from a key.
        _Mappings.emplace_back(ObjectMap{{"itemKey", _Key}, {"sources", VariantArray{ObjectMap{{"itemKey", _Key}}}}});
        if (_ConsumedDesign.contains(_Key))
        {
            const auto _MemberValue = Find(_Properties, "assemblyFrame.member"), _ProfileValue = Find(_Properties, "tubeDesigner.profile");
            if (!_MemberValue || !_ProfileValue)
                throw std::invalid_argument("generated assembly requires shared tube profile and member facts");
            const auto& _Member = RequireObject(*_MemberValue, "shared tube member facts"),
                _Profile = RequireObject(*_ProfileValue, "shared tube profile");
            const auto& _Frame = RequireObject(_Member.at("sectionFrame"), "shared member section frame");
            _Item["designInput"] = ObjectMap{{"kind", std::string("tube-member")},
                {"profileResource", RequireString(_Profile, "sectionResource", "shared tube profile")},
                {"start", _Member.at("start")}, {"end", _Member.at("end")}, {"sectionFrame", ObjectMap{
                    {"origin", _Frame.at("originAtStart")}, {"xAxis", _Frame.at("xAxis")},
                    {"yAxis", _Frame.at("yAxis")}, {"zAxis", _Frame.at("zAxis")}}}};
            _Item.erase("geometry");
            for (const auto _Field : {"length", "stockLength", "station", "stockStart", "startReserve", "endReserve",
                "bendAllowance", "stockState", "formingOrder", "assemblyFrame.member"}) _Properties.erase(_Field);
            if (!Find(_Properties, "manufacturing.partKind")) _Properties["manufacturing.partKind"] = std::string("tube");
            if (!Find(_Properties, "manufacturing.sourcing")) _Properties["manufacturing.sourcing"] = std::string("made");
            _Item["properties"] = std::move(_Properties);
        }
        _ItemValue = std::move(_Item);
    }
    VariantArray _Roots = RequireArray(_Design.at("roots"), "shared design roots");
    if (_HasSelection)
    {
        VariantArray _SelectedValues, _SelectedMappings, _SelectedRoots;
        for (const auto& _Value : _Items)
        {
            auto _Item = RequireObject(_Value, "shared design item");
            if (!_SelectedTree.contains(RequireString(_Item, "key", "shared design item"))) continue;
            VariantArray _Children;
            for (const auto& _Child : ToStringVector(Find(_Item, "children"), "shared design children"))
                if (_SelectedTree.contains(_Child)) _Children.emplace_back(_Child);
            _Item["children"] = std::move(_Children); _SelectedValues.emplace_back(std::move(_Item));
        }
        for (const auto& _Value : _Mappings)
            if (_SelectedItems.contains(RequireString(RequireObject(_Value, "source mapping"), "itemKey", "source mapping"))) _SelectedMappings.push_back(_Value);
        for (const auto& _Value : _Roots)
            if (_SelectedTree.contains(_Value.To<std::string>())) _SelectedRoots.push_back(_Value);
        _Items = std::move(_SelectedValues); _Mappings = std::move(_SelectedMappings); _Roots = std::move(_SelectedRoots);
    }
    // Connections belong to manufacturing; their participants still reference
    // shared design leaves. Do not apply the historical display-only ban on
    // connection requirements such as insertion depth or selected joint kind.
    std::set<std::string> _ConnectionKeys;
    for (const auto& _Value : RequireArray(_Declaration.at("connections"), "manufacturing connections"))
    {
        const auto& _Connection = RequireObject(_Value, "manufacturing connection");
        for (const auto& [_Field, _Entry] : _Connection)
            if (_Field != "key" && _Field != "kind" && _Field != "items" && _Field != "properties")
                throw std::invalid_argument("unsupported manufacturing connection field " + _Field);
        const auto _Key = RequireString(_Connection, "key", "manufacturing connection");
        ValidateStableKey(_Key, "manufacturing connection key");
        if (!_ConnectionKeys.emplace(_Key).second) throw std::invalid_argument("duplicate manufacturing connection");
        ValidateStableKey(RequireString(_Connection, "kind", "manufacturing connection"), "manufacturing connection kind");
        const auto _Participants = ToStringVector(Find(_Connection, "items"), "manufacturing connection participants");
        std::set<std::string> _Seen;
        if (_Participants.size() < 2) throw std::invalid_argument("manufacturing connection requires distinct design participants");
        for (const auto& _ItemKey : _Participants)
            if (!_DesignItems.contains(_ItemKey) || !Find(_DesignItems.at(_ItemKey), "geometry") || !_Seen.emplace(_ItemKey).second)
                throw std::invalid_argument("manufacturing connection references missing or repeated design participants");
        if (const auto _Properties = Find(_Connection, "properties"))
        {
            const auto& _Values = RequireObject(*_Properties, "manufacturing connection properties");
            ValidateDisplayProperties(*_Properties, "manufacturing connection design facts");
            const std::function<void(const Variant&)> _AnchorReferences = [&](const Variant& Value_) {
                if (Value_.Is<ObjectMap>())
                {
                    const auto& _Object = RequireObject(Value_, "manufacturing connection facts");
                    if (Find(_Object, "itemKey") && !_DesignItems.contains(RequireString(_Object, "itemKey", "manufacturing connection anchor")))
                        throw std::invalid_argument("manufacturing connection anchor references an unknown design item");
                    for (const auto& [_Key, _Child] : _Object) _AnchorReferences(_Child);
                }
                else if (Value_.Is<VariantArray>()) for (const auto& _Child : RequireArray(Value_, "manufacturing connection facts")) _AnchorReferences(_Child);
            };
            _AnchorReferences(*_Properties);
            if (const auto _Anchors = Find(_Values, "participantAnchors"))
            {
                std::set<std::string> _SeenAnchors;
                for (const auto& _AnchorValue : RequireArray(*_Anchors, "manufacturing connection anchors"))
                {
                    const auto& _Anchor = RequireObject(_AnchorValue, "manufacturing connection anchor");
                    const auto _ItemKey = RequireString(_Anchor, "itemKey", "manufacturing connection anchor");
                    if (!_Seen.contains(_ItemKey) || !_SeenAnchors.emplace(_ItemKey).second)
                        throw std::invalid_argument("manufacturing connection anchor references missing or repeated participant");
                }
            }
        }
    }
    ObjectMap _Composed{{"schema", std::string(kManufacturingModelSchema)}, {"schemaVersion", 3ull},
        {"coordinateSystem", _Design.at("coordinateSystem")}, {"lengthUnit", _Design.at("lengthUnit")},
        {"resources", std::move(_Resources)}, {"items", std::move(_Items)}, {"roots", std::move(_Roots)},
        {"sourceMappings", std::move(_Mappings)}, {"processes", std::move(_Processes)}};
    (void)AdaptManufacturingInputModel(Variant(_Composed));
    return _Composed;
}

void iCAX::TemplateRuntime::CTemplateCodec::ValidateManufacturingExecutionModel(
    const Variant& Definition_, const Variant& Execution_, const Variant* Design_)
{
    const auto& _DefinitionDocument = RequireObject(Definition_, "manufacturing definition");
    if (Find(_DefinitionDocument, "schemaVersion")
        && ToDouble(_DefinitionDocument.at("schemaVersion"), "manufacturing definition version") == kManufacturingModelSchemaVersion)
    {
        if (!Design_) throw std::invalid_argument("manufacturing declaration execution requires shared design");
        ValidateManufacturingExecutionModel(Variant(ComposeManufacturingModel(Definition_, *Design_)), Execution_);
        return;
    }
    const auto& _ExecutionDocument = RequireObject(Execution_, "manufacturing execution");
    if (RequireString(_ExecutionDocument, "schema", "manufacturing execution") != kNeutralModelSchema
        || !Find(_ExecutionDocument, "schemaVersion")
        || ToDouble(_ExecutionDocument.at("schemaVersion"), "manufacturing execution schemaVersion") != kNeutralModelResourcesSchemaVersion)
        throw std::invalid_argument("manufacturing execution must return a private neutral model version two");
    const auto _Declared = ParseNeutralModel(Definition_), _Actual = ParseNeutralModel(Execution_);
    if (_Declared.Outputs.size() != 1 || _Actual.Outputs.size() != 1
        || _Actual.Outputs.front().Purpose != "result")
        throw std::invalid_argument("manufacturing execution must return one actual result output");
    const auto& _Root = RequireObject(Definition_, "manufacturing definition");
    std::map<std::string, ObjectMap> _Definitions;
    using SOutputIdentity = std::pair<std::string, std::string>;
    std::map<SOutputIdentity, std::string> _ExpectedOutputs;
    std::set<std::string> _ConsumedInputs;
    std::set<SOutputIdentity> _ConsumedOutputs;
    for (const auto& _Value : RequireArray(_Root.at("processes"), "manufacturing declarations"))
    {
        const auto& _Process = RequireObject(_Value, "manufacturing declaration");
        const auto _ProcessKey = RequireString(_Process, "key", "manufacturing declaration");
        const auto& _Definition = RequireObject(_Process.at("definition"), "manufacturing definition");
        _Definitions.emplace(_ProcessKey, _Definition);
        if (const auto _Outputs = Find(_Definition, "outputs"))
        {
            for (const auto& [_Role, _Output] : RequireObject(*_Outputs, "manufacturing output declarations"))
                _ExpectedOutputs.emplace(SOutputIdentity{_ProcessKey, _Role},
                    RequireString(RequireObject(_Output, "manufacturing output declaration"), "key", "manufacturing output declaration"));
            for (const auto& [_Role, _Part] : RequireObject(RequireObject(_Definition.at("processInput"), "assembly input").at("parts"), "assembly parts"))
            {
                const auto& _Binding = RequireObject(_Part, "assembly binding");
                if (RequireString(_Binding, "scope", "assembly binding") == "process-output")
                    _ConsumedOutputs.emplace(RequireString(_Binding, "processKey", "assembly output binding"),
                        RequireString(_Binding, "outputKey", "assembly output binding"));
            }
        }
    }
    if (_ExpectedOutputs.empty())
    {
        if (_Declared.Outputs.front().ItemKeys != _Actual.Outputs.front().ItemKeys)
            throw std::invalid_argument("manufacturing execution changed the declared fixed object identities");
        if (const auto _Sets = Find(_Actual.Extensions, "tubeDesigner.assemblyOutputSets");
            _Sets && !RequireArray(*_Sets, "assembly output sets").empty())
            throw std::invalid_argument("manufacturing execution returned undeclared generated output sets");
        return;
    }
    // Resolve provenance from explicit design bindings or prior generated sets.
    // Dependency validation already excludes cycles and undeclared producer roles.
    std::map<SOutputIdentity, std::set<std::string>> _ExpectedSources;
    std::function<const std::set<std::string>&(const SOutputIdentity&)> _Sources =
        [&](const SOutputIdentity& Identity_) -> const std::set<std::string>& {
        if (_ExpectedSources.contains(Identity_)) return _ExpectedSources.at(Identity_);
        std::set<std::string> _Values;
        const auto& _Definition = _Definitions.at(Identity_.first);
        const auto& _Parts = RequireObject(RequireObject(_Definition.at("processInput"), "assembly input").at("parts"), "assembly parts");
        for (const auto& [_Role, _Value] : _Parts)
        {
            const auto& _Binding = RequireObject(_Value, "assembly binding");
            const auto _Scope = RequireString(_Binding, "scope", "assembly binding");
            if (_Scope == "design")
                for (const auto& _Key : ToStringVector(Find(_Binding, "itemKeys"), "assembly design items"))
                {
                    _Values.insert(_Key); _ConsumedInputs.insert(_Key);
                }
            else if (_Scope == "process-output")
            {
                const auto& _Prior = _Sources({RequireString(_Binding, "processKey", "assembly output binding"),
                    RequireString(_Binding, "outputKey", "assembly output binding")});
                _Values.insert(_Prior.begin(), _Prior.end());
            }
        }
        return _ExpectedSources.emplace(Identity_, std::move(_Values)).first->second;
    };
    for (const auto& [_Identity, _Namespace] : _ExpectedOutputs) (void)_Sources(_Identity);
    const auto _OutputValues = Find(_Actual.Extensions, "tubeDesigner.assemblyOutputSets");
    if (!_OutputValues) throw std::invalid_argument("manufacturing execution has no generated output-set provenance");
    std::set<std::string> _ActualOutputItems(_Actual.Outputs.front().ItemKeys.begin(), _Actual.Outputs.front().ItemKeys.end());
    if (_ActualOutputItems.size() != _Actual.Outputs.front().ItemKeys.size() || _ActualOutputItems.empty())
        throw std::invalid_argument("manufacturing execution output items are empty or repeated");
    const auto _Count = Find(_Actual.Extensions, "tubeDesigner.manufacturingPartCount");
    if (!_Count || ToUInt64(*_Count, "manufacturing actual part count") != _ActualOutputItems.size())
        throw std::invalid_argument("manufacturing executed item count does not match its actual output");
    std::set<SOutputIdentity> _SeenSets;
    std::set<std::string> _CoveredItems;
    for (const auto& _Value : RequireArray(*_OutputValues, "assembly output sets"))
    {
        const auto& _Output = RequireObject(_Value, "assembly output set");
        const std::set<std::string> _Fields{"processKey", "outputKey", "key", "kind", "itemKeys", "sourceItemKeys"};
        if (_Output.size() != _Fields.size() || std::any_of(_Output.begin(), _Output.end(),
            [&](const auto& _Entry) { return !_Fields.contains(_Entry.first); }))
            throw std::invalid_argument("assembly output set must contain its complete declared provenance");
        const SOutputIdentity _Identity{RequireString(_Output, "processKey", "assembly output set"),
            RequireString(_Output, "outputKey", "assembly output set")};
        if (!_ExpectedOutputs.contains(_Identity) || !_SeenSets.emplace(_Identity).second
            || RequireString(_Output, "key", "assembly output set") != _ExpectedOutputs.at(_Identity)
            || RequireString(_Output, "kind", "assembly output set") != "manufacturing-set")
            throw std::invalid_argument("manufacturing execution returned an undeclared or repeated output namespace");
        const auto _ItemKeys = ToStringVector(Find(_Output, "itemKeys"), "assembly output items"),
            _SourceKeys = ToStringVector(Find(_Output, "sourceItemKeys"), "assembly output source items");
        std::set<std::string> _SourcesSet(_SourceKeys.begin(), _SourceKeys.end()), _ItemsSet;
        if (_SourcesSet.size() != _SourceKeys.size() || _SourcesSet != _ExpectedSources.at(_Identity) || _ItemKeys.empty())
            throw std::invalid_argument("manufacturing output provenance does not match its explicit design inputs");
        for (const auto& _ItemKey : _ItemKeys)
        {
            ValidateStableKey(_ItemKey, "assembly generated item key");
            if (!_ItemsSet.emplace(_ItemKey).second || (!_ConsumedOutputs.contains(_Identity) && !_ActualOutputItems.contains(_ItemKey)))
                throw std::invalid_argument("assembly generated set must reference distinct actual output items");
            // Prior sets may be consumed and replaced. Their immutable snapshots
            // remain named records, while only terminal sets become real stock.
            if (!_ConsumedOutputs.contains(_Identity)) _CoveredItems.insert(_ItemKey);
        }
    }
    if (_SeenSets.size() != _ExpectedOutputs.size())
        throw std::invalid_argument("manufacturing execution omitted a declared generated output set");
    // Unconsumed fixed geometry/components retain their independent identities.
    for (const auto& _Value : RequireArray(_Root.at("items"), "manufacturing input items"))
    {
        const auto& _Item = RequireObject(_Value, "manufacturing input item");
        const auto _Key = RequireString(_Item, "key", "manufacturing input item");
        if ((Find(_Item, "geometry") || Find(_Item, "componentReference")) && !_ConsumedInputs.contains(_Key))
        {
            if (!_ActualOutputItems.contains(_Key)) throw std::invalid_argument("manufacturing execution omitted a fixed input item");
            _CoveredItems.insert(_Key);
        }
    }
    if (_CoveredItems != _ActualOutputItems)
        throw std::invalid_argument("manufacturing execution returned parts outside its declared output sets");
    for (const auto& _Item : _Actual.Items)
        if (_ActualOutputItems.contains(_Item.Key) && !_Item.Representations.contains("result"))
            throw std::invalid_argument("manufacturing execution output item has no actual result geometry");
}

iCAX::Data::ObjectMap
iCAX::TemplateRuntime::CTemplateCodec::ExpandNeutralModelResources(const Variant& Document_)
{
    const auto& _Root = RequireObject(Document_, "neutral model");
    if (RequireString(_Root, "schema", "neutral model") != kNeutralModelSchema)
        throw std::invalid_argument("unsupported neutral model schema");
    const auto _SchemaVersion = Find(_Root, "schemaVersion");
    if (!_SchemaVersion) throw std::invalid_argument("neutral model.schemaVersion is required");
    const auto _Version = ToDouble(*_SchemaVersion, "neutral model.schemaVersion");
    if (_Version == kNeutralModelSchemaVersion) return _Root;
    if (_Version != kNeutralModelResourcesSchemaVersion)
        throw std::invalid_argument("unsupported neutral model schema version");
    if (Find(_Root, "geometry"))
        throw std::invalid_argument("neutral model version 2 must use resources, not geometry");
    const auto _ResourceValue = Find(_Root, "resources");
    if (!_ResourceValue) throw std::invalid_argument("neutral model.resources is required");
    auto _Geometry = RequireArray(*_ResourceValue, "neutral model.resources");
    std::unordered_set<std::string> _ResourceKeys;
    SNeutralModel _ResourceGraph;
    std::size_t _Index = 0;
    for (const auto& _Value : _Geometry)
    {
        const auto _Path = "neutral model.resources[" + std::to_string(_Index++) + "]";
        const auto& _Node = RequireObject(_Value, _Path);
        for (const auto& [_Key, _Entry] : _Node)
            if (_Key != "key" && _Key != "operator" && _Key != "inputs" && _Key != "arguments")
                throw std::invalid_argument(_Path + " contains unsupported field " + _Key);
        SGeometryNode _Definition;
        _Definition.Key = RequireString(_Node, "key", _Path);
        ValidateStableKey(_Definition.Key, _Path + ".key");
        if (!_ResourceKeys.emplace(_Definition.Key).second)
            throw std::invalid_argument("duplicate resource key: " + _Definition.Key);
        _Definition.Operator = ParseGeometryOperator(RequireString(_Node, "operator", _Path));
        const auto _Inputs = Find(_Node, "inputs"), _Arguments = Find(_Node, "arguments");
        if (!_Inputs || !_Arguments)
            throw std::invalid_argument(_Path + ".inputs and .arguments are required");
        _Definition.Inputs = ToStringVector(_Inputs, _Path + ".inputs");
        _Definition.Arguments = RequireObject(*_Arguments, _Path + ".arguments");
        if (_Definition.Operator == EGeometryOperator::Boolean)
        {
            // Boolean declarations also carry named dependencies. Validate
            // those before any item instance is synthesized, just as the SDK
            // resource adapter does.
            if (Find(_Definition.Arguments, "target"))
                _Definition.Inputs.push_back(RequireString(_Definition.Arguments, "target", _Path + ".arguments"));
            const auto _Tools = ToStringVector(Find(_Definition.Arguments, "tools"), _Path + ".arguments.tools");
            _Definition.Inputs.insert(_Definition.Inputs.end(), _Tools.begin(), _Tools.end());
        }
        _ResourceGraph.Geometry.push_back(std::move(_Definition));
    }
    // The declared resources form a self-contained graph. Item placements must
    // never repair a resource dependency that references a missing declaration.
    ValidateNeutralModelGraph(_ResourceGraph);

    struct SItemResourceReference final
    {
        std::size_t ItemIndex = 0;
        std::string ItemKey, Purpose, ResourceKey, InstanceKey;
        std::optional<ObjectMap> Placement;
    };
    std::vector<SItemResourceReference> _References;
    auto _OccupiedKeys = _ResourceKeys;
    if (const auto _Items = Find(_Root, "items"))
    {
        _Index = 0;
        for (const auto& _Value : RequireArray(*_Items, "neutral model.items"))
        {
            const auto _ItemIndex = _Index++;
            const auto _Path = "neutral model.items[" + std::to_string(_ItemIndex) + "]";
            const auto& _Item = RequireObject(_Value, _Path);
            const auto _ItemKey = RequireString(_Item, "key", _Path);
            ValidateStableKey(_ItemKey, _Path + ".key");
            const auto _Representations = Find(_Item, "representations");
            if (!_Representations) continue;
            for (const auto& [_Purpose, _Value] : RequireObject(*_Representations, _Path + ".representations"))
            {
                const auto _RepresentationPath = _Path + ".representations." + _Purpose;
                ValidateStableKey(_Purpose, _RepresentationPath);
                const auto& _Representation = RequireObject(_Value, _RepresentationPath);
                for (const auto& [_Key, _Entry] : _Representation)
                    if (_Key != "resource" && _Key != "placement" && _Key != "instanceKey")
                        throw std::invalid_argument(_RepresentationPath + " contains unsupported field " + _Key);
                SItemResourceReference _Reference;
                _Reference.ItemIndex = _ItemIndex;
                _Reference.ItemKey = _ItemKey;
                _Reference.Purpose = _Purpose;
                _Reference.ResourceKey = RequireString(_Representation, "resource", _RepresentationPath);
                if (!_ResourceKeys.contains(_Reference.ResourceKey))
                    throw std::invalid_argument(_RepresentationPath + " references missing resource " + _Reference.ResourceKey);
                if (const auto _Placement = Find(_Representation, "placement"))
                    _Reference.Placement = ParseRigidItemPlacement(*_Placement, _RepresentationPath + ".placement");
                if (Find(_Representation, "instanceKey"))
                {
                    _Reference.InstanceKey = RequireString(_Representation, "instanceKey", _RepresentationPath);
                    ValidateStableKey(_Reference.InstanceKey, _RepresentationPath + ".instanceKey");
                    if (_Reference.Placement && _ResourceKeys.contains(_Reference.InstanceKey))
                        throw std::invalid_argument(_RepresentationPath + ".instanceKey collides with resource " + _Reference.InstanceKey);
                    if (!_Reference.Placement)
                        throw std::invalid_argument(_RepresentationPath + ".instanceKey requires placement");
                    _OccupiedKeys.emplace(_Reference.InstanceKey);
                }
                _References.push_back(std::move(_Reference));
            }
        }
    }

    auto _Expanded = _Root;
    std::unordered_map<std::string, ObjectMap> _Instances;
    for (const auto& _Reference : _References)
    {
        auto _GeometryKey = _Reference.ResourceKey;
        if (_Reference.Placement)
        {
            _GeometryKey = _Reference.InstanceKey;
            if (_GeometryKey.empty())
            {
                _GeometryKey = "instance." + _Reference.ItemKey + "." + _Reference.Purpose;
                if (_OccupiedKeys.contains(_GeometryKey))
                    throw std::invalid_argument("generated instanceKey collides with a declared key: " + _GeometryKey);
                _OccupiedKeys.emplace(_GeometryKey);
            }
            const ObjectMap _Identity{{"resource", _Reference.ResourceKey}, {"placement", *_Reference.Placement}};
            if (const auto _Existing = _Instances.find(_GeometryKey); _Existing != _Instances.end())
            {
                if (_Existing->second != _Identity)
                    throw std::invalid_argument("instanceKey has conflicting resource or placement: " + _GeometryKey);
            }
            else
            {
                _Instances.emplace(_GeometryKey, _Identity);
                _Geometry.emplace_back(ObjectMap{{"key", _GeometryKey}, {"operator", std::string("transform")},
                    {"inputs", VariantArray{_Reference.ResourceKey}},
                    {"arguments", ObjectMap{{"placement", *_Reference.Placement}}}});
            }
        }
        auto& _Item = std::get<ObjectMap>(std::get<VariantArray>(_Expanded.at("items").m_Value)[_Reference.ItemIndex].m_Value);
        std::get<ObjectMap>(_Item.at("representations").m_Value)[_Reference.Purpose] = _GeometryKey;
    }
    _Expanded.erase("resources");
    _Expanded["schemaVersion"] = static_cast<unsigned long long>(kNeutralModelSchemaVersion);
    _Expanded["geometry"] = std::move(_Geometry);
    return _Expanded;
}

iCAX::TemplateRuntime::SNeutralModel
iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(const Variant& Document_)
{
    const auto& _Root = RequireObject(Document_, "neutral model");
    const auto _Schema = RequireString(_Root, "schema", "neutral model");
    if (_Schema == kDisplayModelSchema || _Schema == kManufacturingModelSchema)
    {
        auto _Result = ParseNeutralModel(Variant(_Schema == kDisplayModelSchema
            ? AdaptDisplayModel(Document_) : AdaptManufacturingModel(Document_)));
        _Result.TemplateID.clear();
        _Result.TemplateVersion.clear();
        _Result.PackageDigest.clear();
        _Result.Parameters.clear();
        return _Result;
    }
    if (_Schema != kNeutralModelSchema)
        throw std::invalid_argument("unsupported neutral model schema");
    const auto _SchemaVersion = Find(_Root, "schemaVersion");
    if (_SchemaVersion && ToDouble(*_SchemaVersion, "schemaVersion") == kNeutralModelResourcesSchemaVersion)
        return ParseNeutralModel(Variant(ExpandNeutralModelResources(Document_)));
    if (!_SchemaVersion || ToDouble(*_SchemaVersion, "schemaVersion") != kNeutralModelSchemaVersion)
        throw std::invalid_argument("unsupported neutral model schema version");

    SNeutralModel _Result;
    const auto _TemplateValue = Find(_Root, "template");
    if (!_TemplateValue) throw std::invalid_argument("neutral model.template is required");
    const auto& _Template = RequireObject(*_TemplateValue, "neutral model.template");
    _Result.TemplateID = RequireString(_Template, "id", "neutral model.template");
    ValidateStableKey(_Result.TemplateID, "neutral model.template.id");
    _Result.TemplateVersion = RequireString(_Template, "version", "neutral model.template");
    _Result.PackageDigest = OptionalString(_Template, "packageDigest");
    _Result.CoordinateSystem = OptionalString(_Root, "coordinateSystem", _Result.CoordinateSystem);
    _Result.LengthUnit = OptionalString(_Root, "lengthUnit", _Result.LengthUnit);
    if (const auto _Parameters = Find(_Root, "parameters"))
        _Result.Parameters = RequireObject(*_Parameters, "neutral model.parameters");
    if (const auto _Extensions = Find(_Root, "extensions"))
        _Result.Extensions = RequireObject(*_Extensions, "neutral model.extensions");

    const auto _Geometry = Find(_Root, "geometry");
    if (!_Geometry) throw std::invalid_argument("neutral model.geometry is required");
    std::size_t _Index = 0;
    for (const auto& _Value : RequireArray(*_Geometry, "neutral model.geometry"))
    {
        const auto _Path = "neutral model.geometry[" + std::to_string(_Index++) + "]";
        const auto& _Node = RequireObject(_Value, _Path);
        SGeometryNode _Definition;
        _Definition.Key = RequireString(_Node, "key", _Path);
        _Definition.Operator = ParseGeometryOperator(RequireString(_Node, "operator", _Path));
        _Definition.Inputs = ToStringVector(Find(_Node, "inputs"), _Path + ".inputs");
        if (const auto _Arguments = Find(_Node, "arguments"))
            _Definition.Arguments = RequireObject(*_Arguments, _Path + ".arguments");
        _Result.Geometry.push_back(std::move(_Definition));
    }

    if (const auto _Items = Find(_Root, "items"))
    {
        _Index = 0;
        for (const auto& _Value : RequireArray(*_Items, "neutral model.items"))
        {
            const auto _Path = "neutral model.items[" + std::to_string(_Index++) + "]";
            const auto& _Item = RequireObject(_Value, _Path);
            SModelItem _Definition;
            _Definition.Key = RequireString(_Item, "key", _Path);
            _Definition.DisplayName = RequiredLocalizedText(_Item, "displayName", _Path);
            if (const auto _Representations = Find(_Item, "representations"))
            {
                for (const auto& [_Purpose, _GeometryKey] : RequireObject(*_Representations, _Path + ".representations"))
                {
                    if (!_GeometryKey.Is<std::string>())
                        throw std::invalid_argument(_Path + ".representations." + _Purpose + " must be a string");
                    _Definition.Representations.emplace(_Purpose, _GeometryKey.To<std::string>());
                }
            }
            _Definition.Children = ToStringVector(Find(_Item, "children"), _Path + ".children");
            if (const auto _Properties = Find(_Item, "properties"))
                _Definition.Properties = RequireObject(*_Properties, _Path + ".properties");
            _Result.Items.push_back(std::move(_Definition));
        }
    }

    if (const auto _Outputs = Find(_Root, "outputs"))
    {
        _Index = 0;
        for (const auto& _Value : RequireArray(*_Outputs, "neutral model.outputs"))
        {
            const auto _Path = "neutral model.outputs[" + std::to_string(_Index++) + "]";
            const auto& _Output = RequireObject(_Value, _Path);
            SOutputSet _Definition;
            _Definition.Key = RequireString(_Output, "key", _Path);
            _Definition.Purpose = RequireString(_Output, "purpose", _Path);
            _Definition.ItemKeys = ToStringVector(Find(_Output, "items"), _Path + ".items");
            if (const auto _Properties = Find(_Output, "properties"))
                _Definition.Properties = RequireObject(*_Properties, _Path + ".properties");
            _Result.Outputs.push_back(std::move(_Definition));
        }
    }

    if (const auto _Relationships = Find(_Root, "relationships"))
    {
        _Index = 0;
        for (const auto& _Value : RequireArray(*_Relationships, "neutral model.relationships"))
        {
            const auto _Path = "neutral model.relationships[" + std::to_string(_Index++) + "]";
            const auto& _Relationship = RequireObject(_Value, _Path);
            SModelRelationship _Definition;
            _Definition.Key = RequireString(_Relationship, "key", _Path);
            _Definition.Kind = RequireString(_Relationship, "kind", _Path);
            _Definition.ItemKeys = ToStringVector(Find(_Relationship, "items"), _Path + ".items");
            if (const auto _Properties = Find(_Relationship, "properties"))
                _Definition.Properties = RequireObject(*_Properties, _Path + ".properties");
            _Result.Relationships.push_back(std::move(_Definition));
        }
    }

    if (const auto _Tables = Find(_Root, "tables"))
    {
        _Index = 0;
        for (const auto& _Value : RequireArray(*_Tables, "neutral model.tables"))
        {
            const auto _Path = "neutral model.tables[" + std::to_string(_Index++) + "]";
            const auto& _Table = RequireObject(_Value, _Path);
            SDataTable _Definition;
            _Definition.Key = RequireString(_Table, "key", _Path);
            _Definition.DisplayName = RequiredLocalizedText(_Table, "displayName", _Path);
            if (const auto _Columns = Find(_Table, "columns"))
            {
                std::size_t _ColumnIndex = 0;
                for (const auto& _ColumnValue : RequireArray(*_Columns, _Path + ".columns"))
                {
                    const auto _ColumnPath = _Path + ".columns[" + std::to_string(_ColumnIndex++) + "]";
                    const auto& _Column = RequireObject(_ColumnValue, _ColumnPath);
                    STableColumn _ColumnDefinition;
                    _ColumnDefinition.Key = RequireString(_Column, "key", _ColumnPath);
                    _ColumnDefinition.DisplayName = RequiredLocalizedText(_Column, "displayName", _ColumnPath);
                    _ColumnDefinition.ValueType = OptionalString(_Column, "valueType", "string");
                    _ColumnDefinition.Unit = OptionalString(_Column, "unit");
                    _Definition.Columns.push_back(std::move(_ColumnDefinition));
                }
            }
            if (const auto _Rows = Find(_Table, "rows"))
            {
                std::size_t _RowIndex = 0;
                for (const auto& _RowValue : RequireArray(*_Rows, _Path + ".rows"))
                {
                    const auto _RowPath = _Path + ".rows[" + std::to_string(_RowIndex++) + "]";
                    const auto& _Row = RequireObject(_RowValue, _RowPath);
                    STableRow _RowDefinition;
                    _RowDefinition.Key = RequireString(_Row, "key", _RowPath);
                    _RowDefinition.ParentKey = OptionalString(_Row, "parentKey");
                    _RowDefinition.ItemKey = OptionalString(_Row, "itemKey");
                    if (const auto _Values = Find(_Row, "values"))
                        _RowDefinition.Values = RequireObject(*_Values, _RowPath + ".values");
                    _Definition.Rows.push_back(std::move(_RowDefinition));
                }
            }
            _Result.Tables.push_back(std::move(_Definition));
        }
    }

    if (const auto _Diagnostics = Find(_Root, "diagnostics"))
    {
        _Index = 0;
        for (const auto& _Value : RequireArray(*_Diagnostics, "neutral model.diagnostics"))
        {
            const auto _Path = "neutral model.diagnostics[" + std::to_string(_Index++) + "]";
            const auto& _Diagnostic = RequireObject(_Value, _Path);
            SDiagnostic _Definition;
            _Definition.Severity = RequireString(_Diagnostic, "severity", _Path);
            _Definition.Code = RequireString(_Diagnostic, "code", _Path);
            _Definition.Message = RequireString(_Diagnostic, "message", _Path);
            _Definition.ParameterKeys = ToStringVector(Find(_Diagnostic, "parameterKeys"), _Path + ".parameterKeys");
            _Definition.ModelKey = OptionalString(_Diagnostic, "modelKey");
            _Result.Diagnostics.push_back(std::move(_Definition));
        }
    }
    ValidateNeutralModelGraph(_Result);
    return _Result;
}

iCAX::Data::ObjectMap
iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
    const STemplateDescriptor& Descriptor_, const ObjectMap& Values_)
{
    ObjectMap _Result;
    std::unordered_set<std::string> _Known;
    for (const auto& _Definition : Descriptor_.Parameters)
    {
        _Known.emplace(_Definition.Key);
        const auto _Iterator = Values_.find(_Definition.Key);
        if (_Iterator == Values_.end() || _Iterator->second.Is<std::monostate>())
        {
            if (!_Definition.DefaultValue.Is<std::monostate>())
                _Result[_Definition.Key] = _Definition.DefaultValue;
            continue;
        }
        if (_Definition.ReadOnly && !_Definition.DefaultValue.Is<std::monostate>())
            _Result[_Definition.Key] = _Definition.DefaultValue;
        else
            _Result[_Definition.Key] = NormalizeValue(_Definition, _Iterator->second, false);
    }
    for (const auto& [_Key, _Value] : Values_)
        if (!_Known.contains(_Key))
            throw std::invalid_argument("unknown template parameter: " + _Key);
    // Preserve inactive drafts while checking their type, finiteness and enum
    // membership. Bounds and required values apply only to enabled fields.
    for (const auto& _Definition : Descriptor_.Parameters)
    {
        const auto _Visible = Find(_Definition.Presentation, "visible");
        const bool _Active = !(_Visible && _Visible->Is<bool>() && !_Visible->To<bool>())
            && MatchesCondition(_Definition.VisibleWhen, _Result)
            && MatchesCondition(_Definition.EnabledWhen, _Result);
        if (!_Active) continue;
        const auto _Iterator = _Result.find(_Definition.Key);
        if (_Iterator == _Result.end())
        {
            if (_Definition.Required) throw std::invalid_argument(_Definition.Key + " is required");
        }
        else
            _Iterator->second = NormalizeValue(_Definition, _Iterator->second);
    }
    return _Result;
}

iCAX::Data::ObjectMap
iCAX::TemplateRuntime::CTemplateCodec::MakePresentationDescriptor(
    const STemplateDescriptor& Descriptor_, const std::string& strLocale_)
{
    ObjectMap _Result;
    _Result["id"] = Descriptor_.ID;
    _Result["version"] = Descriptor_.Version;
    _Result["packageDigest"] = Descriptor_.PackageDigest;
    _Result["name"] = Descriptor_.DisplayName.Resolve(strLocale_);
    _Result["description"] = Descriptor_.Description;
    _Result["available"] = true;
    _Result["extensions"] = Descriptor_.Extensions;

    std::map<std::string, std::string> _GroupNames;
    VariantArray _Groups;
    for (const auto& _Group : Descriptor_.Groups)
    {
        ObjectMap _Value;
        _Value["key"] = _Group.Key;
        _Value["displayName"] = _Group.DisplayName.Resolve(strLocale_);
        _Value["order"] = static_cast<long long>(_Group.Order);
        _Groups.emplace_back(_Value);
        _GroupNames.emplace(_Group.Key, _Group.DisplayName.Resolve(strLocale_));
    }
    _Result["groups"] = _Groups;

    VariantArray _Parameters;
    for (const auto& _Definition : Descriptor_.Parameters)
    {
        ObjectMap _Field;
        _Field["key"] = _Definition.Key;
        _Field["name"] = _Definition.Key;
        _Field["displayName"] = _Definition.DisplayName.Resolve(strLocale_);
        _Field["label"] = _Definition.DisplayName.Resolve(strLocale_);
        _Field["description"] = _Definition.Description;
        _Field["valueType"] = ValueTypeName(_Definition.ValueType);
        _Field["quantity"] = QuantityName(_Definition.Quantity);
        _Field["unit"] = _Definition.Unit;
        _Field["required"] = _Definition.Required;
        _Field["readOnly"] = _Definition.ReadOnly;
        _Field["defaultValue"] = _Definition.DefaultValue;
        _Field["groupKey"] = _Definition.GroupKey;
        _Field["group"] = _GroupNames.contains(_Definition.GroupKey)
            ? _GroupNames.at(_Definition.GroupKey) : _Definition.GroupKey;
        if (_Definition.Order.has_value())
            _Field["order"] = static_cast<long long>(*_Definition.Order);
        switch (_Definition.ValueType)
        {
        case EParameterValueType::Number: _Field["type"] = std::string("number"); break;
        case EParameterValueType::Integer: _Field["type"] = std::string("integer"); break;
        case EParameterValueType::Boolean: _Field["type"] = std::string("boolean"); break;
        case EParameterValueType::Enumeration: _Field["type"] = std::string("select"); break;
        case EParameterValueType::String: _Field["type"] = _Definition.ReadOnly ? std::string("readonly") : std::string("text"); break;
        }
        if (_Definition.Constraints.Minimum) _Field["min"] = *_Definition.Constraints.Minimum;
        if (_Definition.Constraints.Maximum) _Field["max"] = *_Definition.Constraints.Maximum;
        if (_Definition.Constraints.Step) _Field["step"] = *_Definition.Constraints.Step;
        if (!_Definition.Choices.empty())
        {
            VariantArray _Choices;
            for (const auto& _Choice : _Definition.Choices)
            {
                ObjectMap _Value;
                _Value["value"] = _Choice.Value;
                _Value["displayName"] = _Choice.DisplayName.Resolve(strLocale_);
                _Value["label"] = _Choice.DisplayName.Resolve(strLocale_);
                _Value["description"] = _Choice.Description;
                _Choices.emplace_back(_Value);
            }
            _Field["choices"] = _Choices;
            _Field["options"] = _Choices;
        }
        if (_Definition.VisibleWhen.Kind != EConditionKind::Always)
            _Field["visibleWhen"] = ConditionToPresentation(_Definition.VisibleWhen);
        if (_Definition.EnabledWhen.Kind != EConditionKind::Always)
            _Field["enabledWhen"] = ConditionToPresentation(_Definition.EnabledWhen);
        _Field["presentation"] = _Definition.Presentation;
        _Parameters.emplace_back(_Field);
    }
    _Result["parameters"] = _Parameters;
    return _Result;
}

iCAX::Data::ObjectMap
iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
    const STemplateDescriptor& Descriptor_, const ObjectMap& Parameters_,
    const std::string& strTemplatePath_)
{
    ObjectMap _Request;
    _Request["protocol"] = std::string(kTemplateProtocol);
    _Request["protocolVersion"] = static_cast<unsigned long long>(kTemplateProtocolVersion);
    _Request["operation"] = std::string("evaluate");
    _Request["templatePath"] = strTemplatePath_;
    ObjectMap _Template;
    _Template["id"] = Descriptor_.ID;
    _Template["version"] = Descriptor_.Version;
    _Template["packageDigest"] = Descriptor_.PackageDigest;
    _Template["parameters"] = MakePresentationDescriptor(Descriptor_).at("parameters");
    _Template["extensions"] = Descriptor_.Extensions;
    _Request["template"] = _Template;
    _Request["parameters"] = Parameters_;
    ObjectMap _Context;
    _Context["neutralModelSchema"] = std::string(kNeutralModelSchema);
    _Context["neutralModelSchemaVersion"] = static_cast<unsigned long long>(kNeutralModelSchemaVersion);
    _Context["coordinateSystem"] = std::string("right-handed-x-width-y-depth-z-height");
    _Context["lengthUnit"] = std::string("mm");
    _Request["context"] = _Context;
    return _Request;
}
