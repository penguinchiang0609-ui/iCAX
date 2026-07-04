#include "pch.h"
#include "IProductContext.h"

#include <stdexcept>

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

iCAX::Mail::CMailPostOffice iCAX::Product::IProductContext::GetBackendPostOffice() const
{
    throw std::logic_error("Product backend post office is not configured");
}

iCAX::Mail::CMailPostOffice iCAX::Product::IProductContext::GetFrontendPostOffice() const
{
    throw std::logic_error("Product frontend post office is not configured");
}
