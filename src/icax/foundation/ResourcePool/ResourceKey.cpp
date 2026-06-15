#include "pch.h"
#include "ResourceKey.h"

bool iCAX::Resource::CResourceKey::IsValid() const
{
    return !Type.empty() && ID != iCAX::Data::uuid();
}

bool iCAX::Resource::operator==(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept
{
    return Left_.Type == Right_.Type && Left_.ID == Right_.ID;
}

bool iCAX::Resource::operator!=(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept
{
    return !(Left_ == Right_);
}

bool iCAX::Resource::operator<(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept
{
    if (Left_.Type != Right_.Type)
    {
        return Left_.Type < Right_.Type;
    }
    return Left_.ID < Right_.ID;
}

std::string iCAX::Resource::ToString(IN const CResourceKey& Key_)
{
    return Key_.Type + ":" + iCAX::Data::to_string(Key_.ID);
}
