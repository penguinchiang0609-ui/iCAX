#pragma once

#include "Database/ComponentHelper.h"

#include <string>

namespace iCAX::CAM::Intent
{
inline iCAX::Data::PropertyValue ToStringVariant(IN const std::string& Value_) { return iCAX::Data::PropertyValue(Value_); }
inline std::string FromStringVariant(IN const iCAX::Data::PropertyValue& Value_) { return Value_.To<std::string>(); }
inline bool StringEqual(IN const std::string& Left_, IN const std::string& Right_) { return Left_ == Right_; }

inline iCAX::Data::PropertyValue ToBoolVariant(IN const bool& Value_) { return iCAX::Data::PropertyValue(Value_); }
inline bool FromBoolVariant(IN const iCAX::Data::PropertyValue& Value_) { return Value_.To<bool>(); }
inline bool BoolEqual(IN const bool& Left_, IN const bool& Right_) { return Left_ == Right_; }

inline iCAX::Data::PropertyValue ToUInt64Variant(IN const unsigned long long& Value_) { return iCAX::Data::PropertyValue(Value_); }
inline unsigned long long FromUInt64Variant(IN const iCAX::Data::PropertyValue& Value_) { return Value_.To<unsigned long long>(); }
inline bool UInt64Equal(IN const unsigned long long& Left_, IN const unsigned long long& Right_) { return Left_ == Right_; }

inline iCAX::Data::PropertyValue ToDoubleVariant(IN const double& Value_) { return iCAX::Data::PropertyValue(Value_); }
inline double FromDoubleVariant(IN const iCAX::Data::PropertyValue& Value_) { return Value_.To<double>(); }
inline bool DoubleEqual(IN const double& Left_, IN const double& Right_) { return Left_ == Right_; }

inline iCAX::Data::PropertyValue ToUuidVariant(IN const iCAX::Data::uuid& Value_) { return iCAX::Data::PropertyValue(Value_); }
inline iCAX::Data::uuid FromUuidVariant(IN const iCAX::Data::PropertyValue& Value_)
{
    if (Value_.Is<iCAX::Data::uuid>()) return Value_.To<iCAX::Data::uuid>();
    if (Value_.Is<std::string>())
    {
        const auto _Parsed = iCAX::Data::uuid::from_string(Value_.To<std::string>());
        if (_Parsed.has_value()) return *_Parsed;
    }
    return {};
}
inline bool UuidEqual(IN const iCAX::Data::uuid& Left_, IN const iCAX::Data::uuid& Right_) { return Left_ == Right_; }

inline iCAX::Data::PropertyValue ToVariantArrayVariant(IN const iCAX::Data::VariantArray& Value_) { return iCAX::Data::PropertyValue(Value_); }
inline iCAX::Data::VariantArray FromVariantArrayVariant(IN const iCAX::Data::PropertyValue& Value_)
{
    return Value_.Is<iCAX::Data::VariantArray>() ? Value_.To<iCAX::Data::VariantArray>() : iCAX::Data::VariantArray();
}
inline bool VariantArrayEqual(IN const iCAX::Data::VariantArray& Left_, IN const iCAX::Data::VariantArray& Right_) { return Left_ == Right_; }
}
