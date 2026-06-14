#pragma once
#include "Database.h"
#include "IComponentEvent.h"
#include <bitset>

namespace iCAX
{
    namespace Database
    {
        class CMaskRegistry 
        {
        public:
            /*
            * @brief 获取索引
            * @param [in] strClassName_
            * @return size_t
            */
            static size_t GetComponentIndex(IN const std::string& strClassName_);

            /*
            * @brief 根据索引获取名称
            * @param [in] nIndex_
            * @return std::string
            */
            static std::string GetComponentClass(IN size_t nIndex_);

        private:
            static std::unordered_map<std::string, size_t> s_mapNameToIndex;
            static std::unordered_map<size_t, std::string> s_mapIndexToName;
        };
    }
}