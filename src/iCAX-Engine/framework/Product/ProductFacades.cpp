#include "pch.h"
#include "ProductFacades.h"

#include "Data/VariantSerializer.h"


std::vector<uint8_t> iCAX::Product::EncodeProductPayload(IN const iCAX::Data::Variant& Payload_)
{
    const auto _Text = iCAX::Data::VariantSerializer::Serialize(Payload_);
    return std::vector<uint8_t>(_Text.begin(), _Text.end());
}

iCAX::Data::Variant iCAX::Product::DecodeProductPayload(IN const std::vector<uint8_t>& Payload_)
{
    if (Payload_.empty())
    {
        return iCAX::Data::Variant(iCAX::Data::ObjectMap{});
    }

    const std::string _Text(Payload_.begin(), Payload_.end());
    try
    {
        return iCAX::Data::VariantSerializer::Deserialize(_Text);
    }
    catch (const std::exception& _Error)
    {
        throw std::invalid_argument("Invalid Product payload: " + std::string(_Error.what()));
    }
}
