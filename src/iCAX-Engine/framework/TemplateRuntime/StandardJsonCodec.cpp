#include "StandardJsonCodec.h"

#include <boost/json/src.hpp>

#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace
{
    namespace json = boost::json;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;

    Variant FromJson(const json::value& Value_)
    {
        if (Value_.is_null()) return {};
        if (Value_.is_bool()) return Value_.as_bool();
        if (Value_.is_string()) return std::string(Value_.as_string().c_str());
        if (Value_.is_int64()) return static_cast<long long>(Value_.as_int64());
        if (Value_.is_uint64()) return static_cast<unsigned long long>(Value_.as_uint64());
        if (Value_.is_double()) return Value_.as_double();
        if (Value_.is_array())
        {
            VariantArray _Result;
            _Result.reserve(Value_.as_array().size());
            for (const auto& _Item : Value_.as_array()) _Result.emplace_back(FromJson(_Item));
            return _Result;
        }
        if (Value_.is_object())
        {
            ObjectMap _Result;
            for (const auto& _Item : Value_.as_object())
                _Result.emplace(std::string(_Item.key_c_str()), FromJson(_Item.value()));
            return _Result;
        }
        throw std::invalid_argument("standard JSON contains an unsupported value");
    }

    json::value ToJson(const Variant& Value_)
    {
        return std::visit([](const auto& Item_) -> json::value {
            using TValue = std::decay_t<decltype(Item_)>;
            if constexpr (std::is_same_v<TValue, std::monostate>) return nullptr;
            else if constexpr (std::is_same_v<TValue, bool>) return Item_;
            else if constexpr (std::is_same_v<TValue, char>
                || std::is_same_v<TValue, std::uint8_t>
                || std::is_same_v<TValue, short>
                || std::is_same_v<TValue, int>
                || std::is_same_v<TValue, long long>)
            {
                return static_cast<std::int64_t>(Item_);
            }
            else if constexpr (std::is_same_v<TValue, unsigned short>
                || std::is_same_v<TValue, unsigned int>
                || std::is_same_v<TValue, unsigned long long>)
            {
                return static_cast<std::uint64_t>(Item_);
            }
            else if constexpr (std::is_same_v<TValue, float>
                || std::is_same_v<TValue, double>)
            {
                const auto _Number = static_cast<double>(Item_);
                if (!std::isfinite(_Number))
                    throw std::invalid_argument("standard JSON cannot contain a non-finite number");
                return _Number;
            }
            else if constexpr (std::is_same_v<TValue, std::string>) return json::string(Item_);
            else if constexpr (std::is_same_v<TValue, ObjectMap>)
            {
                json::object _Result;
                for (const auto& [_Key, _Value] : Item_) _Result.emplace(_Key, ToJson(_Value));
                return _Result;
            }
            else if constexpr (std::is_same_v<TValue, VariantArray>)
            {
                json::array _Result;
                _Result.reserve(Item_.size());
                for (const auto& _Value : Item_) _Result.emplace_back(ToJson(_Value));
                return _Result;
            }
            else
            {
                throw std::invalid_argument("iCAX value type is not representable as standard JSON");
            }
        }, Value_.m_Value);
    }
}

iCAX::Data::Variant iCAX::TemplateRuntime::CStandardJsonCodec::Parse(
    const std::string& strJson_)
{
    boost::system::error_code _Error;
    json::parse_options _Options;
    // Frozen neutral geometry is hashed in Python. The fast default parser can
    // change a rotation coefficient by one ULP and invalidate the saved cutter.
    _Options.numbers = json::number_precision::precise;
    const auto _Value = json::parse(strJson_, _Error, {}, _Options);
    if (_Error) throw std::invalid_argument("invalid standard JSON: " + _Error.message());
    return FromJson(_Value);
}

std::string iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(
    const iCAX::Data::Variant& Value_)
{
    return json::serialize(ToJson(Value_));
}
