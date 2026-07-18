#pragma once
#include "Database/ComponentHelper.h"
#include <string>
namespace iCAX::CAM::Process
{
inline iCAX::Data::PropertyValue ToStringVariant(IN const std::string& V_) { return iCAX::Data::PropertyValue(V_); }
inline std::string FromStringVariant(IN const iCAX::Data::PropertyValue& V_) { return V_.To<std::string>(); }
inline bool StringEqual(IN const std::string& A_, IN const std::string& B_) { return A_ == B_; }
inline iCAX::Data::PropertyValue ToBoolVariant(IN const bool& V_) { return iCAX::Data::PropertyValue(V_); }
inline bool FromBoolVariant(IN const iCAX::Data::PropertyValue& V_) { return V_.To<bool>(); }
inline bool BoolEqual(IN const bool& A_, IN const bool& B_) { return A_ == B_; }
inline iCAX::Data::PropertyValue ToUInt64Variant(IN const unsigned long long& V_) { return iCAX::Data::PropertyValue(V_); }
inline unsigned long long FromUInt64Variant(IN const iCAX::Data::PropertyValue& V_) { return V_.To<unsigned long long>(); }
inline bool UInt64Equal(IN const unsigned long long& A_, IN const unsigned long long& B_) { return A_ == B_; }
inline iCAX::Data::PropertyValue ToVariantArrayVariant(IN const iCAX::Data::VariantArray& V_) { return iCAX::Data::PropertyValue(V_); }
inline iCAX::Data::VariantArray FromVariantArrayVariant(IN const iCAX::Data::PropertyValue& V_) { return V_.Is<iCAX::Data::VariantArray>() ? V_.To<iCAX::Data::VariantArray>() : iCAX::Data::VariantArray(); }
inline bool VariantArrayEqual(IN const iCAX::Data::VariantArray& A_, IN const iCAX::Data::VariantArray& B_) { return A_ == B_; }
}
