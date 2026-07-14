#pragma once

#include "Database/ComponentHelper.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        using iCAX::Database::CComponentBase;

        inline iCAX::Data::PropertyValue ToStringVariant(IN const std::string& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline std::string FromStringVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<std::string>();
        }

        inline iCAX::Data::PropertyValue ToUInt64Variant(IN const unsigned long long& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::PropertyValue ToBoolVariant(IN const bool& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::PropertyValue ToDoubleVariant(IN const double& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline unsigned long long FromUInt64Variant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<unsigned long long>();
        }

        inline bool FromBoolVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<bool>();
        }

        inline double FromDoubleVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<double>();
        }

        inline iCAX::Data::PropertyValue ToUuidVariant(IN const iCAX::Data::uuid& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::uuid FromUuidVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            if (Value_.Is<iCAX::Data::uuid>())
            {
                return Value_.To<iCAX::Data::uuid>();
            }
            if (Value_.Is<std::string>())
            {
                auto _Parsed = iCAX::Data::uuid::from_string(Value_.To<std::string>());
                if (_Parsed.has_value())
                {
                    return *_Parsed;
                }
            }
            return iCAX::Data::uuid();
        }

        inline iCAX::Data::PropertyValue ToVariantArrayVariant(IN const iCAX::Data::VariantArray& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::VariantArray FromVariantArrayVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            if (Value_.Is<iCAX::Data::VariantArray>())
            {
                return Value_.To<iCAX::Data::VariantArray>();
            }
            return {};
        }

        inline bool StringEqual(IN const std::string& Lhs_, IN const std::string& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool UInt64Equal(IN const unsigned long long& Lhs_, IN const unsigned long long& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool BoolEqual(IN const bool& Lhs_, IN const bool& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool DoubleEqual(IN const double& Lhs_, IN const double& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool UuidEqual(IN const iCAX::Data::uuid& Lhs_, IN const iCAX::Data::uuid& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool VariantArrayEqual(IN const iCAX::Data::VariantArray& Lhs_, IN const iCAX::Data::VariantArray& Rhs_)
        {
            return Lhs_ == Rhs_;
        }
    }
}
