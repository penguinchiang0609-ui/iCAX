#include "pch.h"
#include "IProductContext.h"


iCAX::Product::IProductContext::IProductContext() = default;

iCAX::Product::IProductContext::~IProductContext() = default;

iCAX::Data::PropertyBag iCAX::Product::IProductContext::GetSettings() const
{
    return GetProductData().Settings;
}

void iCAX::Product::IProductContext::ReplaceSettings(IN const iCAX::Data::PropertyBag&)
{
    throw std::logic_error("Product settings are not writable in this ProductContext");
}

const iCAX::Data::uuid& iCAX::Product::IProductContext::GetProductChannelID() const
{
    static const iCAX::Data::uuid _Nil = iCAX::Data::GenerateNilUUID();
    return _Nil;
}

iCAX::Interaction::CSDOEndpoint iCAX::Product::IProductContext::GetBackendSDOEndpoint() const
{
    throw std::logic_error("Product backend SDO endpoint is not configured");
}

iCAX::Interaction::CSDOEndpoint iCAX::Product::IProductContext::GetFrontendSDOEndpoint() const
{
    throw std::logic_error("Product frontend SDO endpoint is not configured");
}
