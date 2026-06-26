#include "pch.h"
#include "ResourceKey.h"

bool iCAX::Resource::CResourceKey::IsValid() const
{
    return !Source.empty();
}

bool iCAX::Resource::operator==(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept
{
    return Left_.Source == Right_.Source;
}

bool iCAX::Resource::operator!=(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept
{
    return !(Left_ == Right_);
}

bool iCAX::Resource::operator<(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept
{
    return Left_.Source < Right_.Source;
}

std::string iCAX::Resource::ToString(IN const CResourceKey& Key_)
{
    return Key_.Source;
}
