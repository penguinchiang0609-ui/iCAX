#include "pch.h"
#include "StableId.h"
#include "CommonFunction.h"

iCAX::Data::StableId32 iCAX::Data::MakeStableId32(IN const std::string& strName_)
{
    return FNV1a32(strName_.c_str());
}

iCAX::Data::StableId iCAX::Data::MakeStableId(IN const std::string& strScope_, IN const std::string& strName_)
{
    return (uint64_t(MakeStableId32(strScope_)) << 32) | MakeStableId32(strName_);
}
