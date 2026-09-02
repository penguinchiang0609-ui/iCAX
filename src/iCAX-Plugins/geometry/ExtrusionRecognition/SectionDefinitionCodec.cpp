#include "pch.h"
#include "SectionDefinitionCodec.h"

#include <boost/json/src.hpp>

namespace iCAX::ExtrusionRecognition
{
namespace
{
    namespace json = boost::json;

    iCAX::Data::Variant ToVariant(IN const json::value& Value_);

    iCAX::Data::ObjectMap ToObjectMap(IN const json::object& Object_)
    {
        iCAX::Data::ObjectMap _Result;
        for (const auto& _Item : Object_)
        {
            _Result[std::string(_Item.key_c_str())] = ToVariant(_Item.value());
        }
        return _Result;
    }

    iCAX::Data::VariantArray ToVariantArray(IN const json::array& Array_)
    {
        iCAX::Data::VariantArray _Result;
        _Result.reserve(Array_.size());
        for (const auto& _Item : Array_)
        {
            _Result.emplace_back(ToVariant(_Item));
        }
        return _Result;
    }

    iCAX::Data::Variant ToVariant(IN const json::value& Value_)
    {
        if (Value_.is_null()) return {};
        if (Value_.is_bool()) return Value_.as_bool();
        if (Value_.is_string()) return std::string(Value_.as_string().c_str());
        if (Value_.is_int64()) return static_cast<long long>(Value_.as_int64());
        if (Value_.is_uint64()) return static_cast<unsigned long long>(Value_.as_uint64());
        if (Value_.is_double()) return Value_.as_double();
        if (Value_.is_array()) return ToVariantArray(Value_.as_array());
        if (Value_.is_object()) return ToObjectMap(Value_.as_object());
        return {};
    }

    std::string OptionalString(
        IN const json::object& Object_,
        IN const char* pName_,
        IN const std::string& strDefault_ = {})
    {
        const auto* _pValue = Object_.if_contains(pName_);
        if (!_pValue || _pValue->is_null())
        {
            return strDefault_;
        }
        if (!_pValue->is_string())
        {
            throw std::invalid_argument(
                std::string("Section definition field must be string: ") + pName_);
        }
        return std::string(_pValue->as_string().c_str());
    }

    iCAX::Data::ObjectMap OptionalObject(
        IN const json::object& Object_,
        IN const char* pName_)
    {
        const auto* _pValue = Object_.if_contains(pName_);
        if (!_pValue || _pValue->is_null())
        {
            return {};
        }
        if (!_pValue->is_object())
        {
            throw std::invalid_argument(
                std::string("Section definition field must be object: ") + pName_);
        }
        return ToObjectMap(_pValue->as_object());
    }

    std::map<std::string, std::string> OptionalMetadata(
        IN const json::object& Object_)
    {
        std::map<std::string, std::string> _Result;
        const auto* _pMetadata = Object_.if_contains("metadata");
        if (!_pMetadata || _pMetadata->is_null())
        {
            return _Result;
        }
        if (!_pMetadata->is_object())
        {
            throw std::invalid_argument(
                "Section definition field must be object: metadata");
        }
        for (const auto& _Item : _pMetadata->as_object())
        {
            if (!_Item.value().is_string())
            {
                throw std::invalid_argument(
                    "Section definition metadata values must be strings");
            }
            _Result[std::string(_Item.key_c_str())]
                = std::string(_Item.value().as_string().c_str());
        }
        return _Result;
    }

    SSectionTypeDefinition DecodeDefinition(IN const json::object& Object_)
    {
        SSectionTypeDefinition _Definition;
        _Definition.TypeID = OptionalString(Object_, "typeId");
        if (_Definition.TypeID.empty())
        {
            _Definition.TypeID = OptionalString(Object_, "id");
        }
        _Definition.DisplayName = OptionalString(
            Object_, "displayName", _Definition.TypeID);
        if (const auto _Name = OptionalString(Object_, "name");
            _Definition.DisplayName == _Definition.TypeID && !_Name.empty())
        {
            _Definition.DisplayName = _Name;
        }
        _Definition.ParameterSchema = OptionalObject(Object_, "parameterSchema");
        _Definition.Bindings = OptionalObject(Object_, "bindings");
        _Definition.Parameters = OptionalObject(Object_, "parameters");
        _Definition.Placement = OptionalObject(Object_, "placement");
        _Definition.Metadata = OptionalMetadata(Object_);

        const auto* _pMatch = Object_.if_contains("match");
        if (_pMatch)
        {
            _Definition.Match = ToVariant(*_pMatch);
        }
        return _Definition;
    }

    std::string VariantOptionalString(
        IN const iCAX::Data::ObjectMap& Object_,
        IN const char* pName_,
        IN const std::string& strDefault_ = {})
    {
        const auto _Iter = Object_.find(pName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::invalid_argument(
                std::string("Section definition field must be string: ") + pName_);
        }
        return _Iter->second.To<std::string>();
    }

    iCAX::Data::ObjectMap VariantOptionalObject(
        IN const iCAX::Data::ObjectMap& Object_,
        IN const char* pName_)
    {
        const auto _Iter = Object_.find(pName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return {};
        }
        if (!_Iter->second.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument(
                std::string("Section definition field must be object: ") + pName_);
        }
        return _Iter->second.To<iCAX::Data::ObjectMap>();
    }

    SSectionTypeDefinition DecodeVariantDefinition(
        IN const iCAX::Data::ObjectMap& Object_)
    {
        SSectionTypeDefinition _Definition;
        _Definition.TypeID = VariantOptionalString(Object_, "typeId");
        if (_Definition.TypeID.empty())
        {
            _Definition.TypeID = VariantOptionalString(Object_, "id");
        }
        _Definition.DisplayName = VariantOptionalString(
            Object_, "displayName", _Definition.TypeID);
        if (const auto _Name = VariantOptionalString(Object_, "name");
            _Definition.DisplayName == _Definition.TypeID && !_Name.empty())
        {
            _Definition.DisplayName = _Name;
        }
        _Definition.ParameterSchema = VariantOptionalObject(Object_, "parameterSchema");
        _Definition.Bindings = VariantOptionalObject(Object_, "bindings");
        _Definition.Parameters = VariantOptionalObject(Object_, "parameters");
        _Definition.Placement = VariantOptionalObject(Object_, "placement");

        if (const auto _Match = Object_.find("match"); _Match != Object_.end())
        {
            _Definition.Match = _Match->second;
        }
        if (const auto _Metadata = Object_.find("metadata");
            _Metadata != Object_.end())
        {
            if (!_Metadata->second.Is<iCAX::Data::ObjectMap>())
            {
                throw std::invalid_argument(
                    "Section definition field must be object: metadata");
            }
            for (const auto& [_Name, _Value]
                : _Metadata->second.To<iCAX::Data::ObjectMap>())
            {
                if (!_Value.Is<std::string>())
                {
                    throw std::invalid_argument(
                        "Section definition metadata values must be strings");
                }
                _Definition.Metadata[_Name] = _Value.To<std::string>();
            }
        }
        return _Definition;
    }
}

SDefinitionLoadResult DecodeSectionDefinitions(
    IN const iCAX::Data::Variant& Document_)
{
    SDefinitionLoadResult _Result;
    try
    {
        iCAX::Data::VariantArray _Definitions;
        if (Document_.Is<iCAX::Data::VariantArray>())
        {
            _Definitions = Document_.To<iCAX::Data::VariantArray>();
        }
        else if (Document_.Is<iCAX::Data::ObjectMap>())
        {
            const auto _Root = Document_.To<iCAX::Data::ObjectMap>();
            const auto _Iter = _Root.find("definitions");
            if (_Iter != _Root.end()
                && _Iter->second.Is<iCAX::Data::VariantArray>())
            {
                _Definitions = _Iter->second.To<iCAX::Data::VariantArray>();
            }
        }
        if (_Definitions.empty())
        {
            _Result.Diagnostics.push_back(
                "Section definition document must be a non-empty array or contain a definitions array");
            return _Result;
        }
        _Result.Definitions.reserve(_Definitions.size());
        for (std::size_t _Index = 0; _Index < _Definitions.size(); ++_Index)
        {
            if (!_Definitions[_Index].Is<iCAX::Data::ObjectMap>())
            {
                _Result.Diagnostics.push_back(
                    "Section definition at index " + std::to_string(_Index)
                    + " must be an object");
                return _Result;
            }
            _Result.Definitions.push_back(DecodeVariantDefinition(
                _Definitions[_Index].To<iCAX::Data::ObjectMap>()));
        }
        _Result.bValid = true;
        return _Result;
    }
    catch (const std::exception& Error_)
    {
        _Result.Definitions.clear();
        _Result.Diagnostics.push_back(Error_.what());
        return _Result;
    }
}

SDefinitionLoadResult DecodeSectionDefinitionsJSON(IN std::string_view JSON_)
{
    SDefinitionLoadResult _Result;
    try
    {
        boost::system::error_code _Error;
        const auto _Document = json::parse(JSON_, _Error);
        if (_Error)
        {
            _Result.Diagnostics.push_back(
                "Invalid section definition JSON: " + _Error.message());
            return _Result;
        }

        const json::array* _pDefinitions = nullptr;
        if (_Document.is_array())
        {
            _pDefinitions = &_Document.as_array();
        }
        else if (_Document.is_object())
        {
            const auto* _pValue = _Document.as_object().if_contains("definitions");
            if (_pValue && _pValue->is_array())
            {
                _pDefinitions = &_pValue->as_array();
            }
        }
        if (!_pDefinitions)
        {
            _Result.Diagnostics.push_back(
                "Section definition JSON must be an array or contain a definitions array");
            return _Result;
        }
        if (_pDefinitions->empty())
        {
            _Result.Diagnostics.push_back(
                "Section definition list cannot be empty");
            return _Result;
        }

        _Result.Definitions.reserve(_pDefinitions->size());
        for (std::size_t _Index = 0; _Index < _pDefinitions->size(); ++_Index)
        {
            const auto& _Value = (*_pDefinitions)[_Index];
            if (!_Value.is_object())
            {
                _Result.Diagnostics.push_back(
                    "Section definition at index " + std::to_string(_Index)
                    + " must be an object");
                return _Result;
            }
            _Result.Definitions.push_back(DecodeDefinition(_Value.as_object()));
        }
        _Result.bValid = true;
        return _Result;
    }
    catch (const std::exception& Error_)
    {
        _Result.Definitions.clear();
        _Result.Diagnostics.push_back(Error_.what());
        return _Result;
    }
}
}
