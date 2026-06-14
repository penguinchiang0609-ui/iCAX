#include "pch.h"
#include "MaskRegistry.h"
#include <cassert>
#include <unordered_map>
#include "ComponentMask.h"

std::unordered_map<std::string, size_t> iCAX::Database::CMaskRegistry::s_mapNameToIndex;
std::unordered_map<size_t, std::string> iCAX::Database::CMaskRegistry::s_mapIndexToName;

//!< 获取索引
size_t iCAX::Database::CMaskRegistry::GetComponentIndex(IN const std::string& strClassName_)
{
    static size_t _NextIndex = 0;

    auto it = s_mapNameToIndex.find(strClassName_);
    if (it != s_mapNameToIndex.end())
        return it->second;

    assert(_NextIndex < MAX_COMPONENTS && "Exceeded max number of components");
    s_mapNameToIndex[strClassName_] = _NextIndex;
    s_mapIndexToName[_NextIndex] = strClassName_;
    return _NextIndex++;
}

//!< 根据索引获取名称
std::string iCAX::Database::CMaskRegistry::GetComponentClass(IN size_t nIndex_)
{
    auto it = s_mapIndexToName.find(nIndex_);
    if (it != s_mapIndexToName.end())
        return it->second;

    return std::string();
}
