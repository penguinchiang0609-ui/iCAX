#include "pch.h"
#include "PCEntry.h"
#include <cstdint>
#include "Data/StableId.h"

//! 创建PCID
iCAX::PC::PCID iCAX::PC::MakePCID(IN const std::string& nModule_, IN const std::string& nName_)
{
    return iCAX::Data::MakeStableId(nModule_, nName_);
}
