#include "TemplateCodec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

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

    Variant NormalizeValue(const SParameterDefinition& Definition_, const Variant& Value_)
    {
        switch (Definition_.ValueType)
        {
        case EParameterValueType::Number:
        {
            const auto _Number = ToDouble(Value_, Definition_.Key);
            if (!std::isfinite(_Number))
                throw std::invalid_argument(Definition_.Key + " must be finite");
            if (Definition_.Constraints.Minimum && _Number < *Definition_.Constraints.Minimum)
                throw std::invalid_argument(Definition_.Key + " is below its minimum");
            if (Definition_.Constraints.Maximum && _Number > *Definition_.Constraints.Maximum)
                throw std::invalid_argument(Definition_.Key + " exceeds its maximum");
            return Variant(_Number);
        }
        case EParameterValueType::Integer:
        {
            const auto _Number = ToInt64(Value_, Definition_.Key);
            if (Definition_.Constraints.Minimum && static_cast<double>(_Number) < *Definition_.Constraints.Minimum)
                throw std::invalid_argument(Definition_.Key + " is below its minimum");
            if (Definition_.Constraints.Maximum && static_cast<double>(_Number) > *Definition_.Constraints.Maximum)
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
            if (Definition_.Constraints.MinimumLength && _Text.size() < *Definition_.Constraints.MinimumLength)
                throw std::invalid_argument(Definition_.Key + " is shorter than its minimum length");
            if (Definition_.Constraints.MaximumLength && _Text.size() > *Definition_.Constraints.MaximumLength)
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
            // Compatibility with the current generic form renderer.
            if (Condition_.Kind == EConditionKind::Equals)
            {
                _Result["name"] = Condition_.ParameterKey;
            }
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
    return _Result;
}

iCAX::TemplateRuntime::SNeutralModel
iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(const Variant& Document_)
{
    const auto& _Root = RequireObject(Document_, "neutral model");
    if (RequireString(_Root, "schema", "neutral model") != kNeutralModelSchema)
        throw std::invalid_argument("unsupported neutral model schema");
    const auto _SchemaVersion = Find(_Root, "schemaVersion");
    if (!_SchemaVersion || ToUInt64(*_SchemaVersion, "schemaVersion") != kNeutralModelSchemaVersion)
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
            else if (_Definition.Required)
                throw std::invalid_argument(_Definition.Key + " is required");
            continue;
        }
        if (_Definition.ReadOnly && !_Definition.DefaultValue.Is<std::monostate>())
            _Result[_Definition.Key] = _Definition.DefaultValue;
        else
            _Result[_Definition.Key] = NormalizeValue(_Definition, _Iterator->second);
    }
    for (const auto& [_Key, _Value] : Values_)
        if (!_Known.contains(_Key))
            throw std::invalid_argument("unknown template parameter: " + _Key);
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
        _Field["order"] = static_cast<long long>(_Definition.Order);
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
