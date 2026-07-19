#include "pch.h"
#include "ApplicationRuntimeFacades.h"

#include "Data/VariantSerializer.h"


std::vector<uint8_t> iCAX::Application::EncodeApplicationRuntimePayload(IN const iCAX::Data::Variant& Payload_)
{
    const auto _Text = iCAX::Data::VariantSerializer::Serialize(Payload_);
    return std::vector<uint8_t>(_Text.begin(), _Text.end());
}

iCAX::Data::Variant iCAX::Application::DecodeApplicationRuntimePayload(IN const std::vector<uint8_t>& Payload_)
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
        throw std::invalid_argument("Invalid ApplicationRuntime payload: " + std::string(_Error.what()));
    }
}
