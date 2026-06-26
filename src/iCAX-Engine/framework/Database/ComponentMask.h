#pragma once
#include "Database.h"
#include <bitset>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        //!< 此处魔鬼数字，当下支持最多4096个组件，如果后续要扩容，直接修改此处的值即可
        constexpr size_t MAX_COMPONENTS = 4096;

        /*
        * @brief 组件掩码
        */
        class _DATABASE_EXP CComponentMask final
        {
        public:
            using BitSet = std::bitset<MAX_COMPONENTS>;

            /*
            * @brief 设置
            * @param [in] strClassName_
            */
            void Set(IN const std::string& strClassName_);

            /*
            * @brief 取消
            * @param [in] strClassName_
            */
            void Reset(IN const std::string& strClassName_);

            /*
            * @brief 测试
            * @param [in] strClassName_
            * @return bool
            */
            bool Test(IN const std::string& strClassName_) const;

            /*
            * @brief 匹配
            * @param [in] Required_
            * @return bool
            */
            bool Matches(IN const CComponentMask& Required_) const;

            /*
            * @brief 获取名称列表
            * @return std::vector<std::string>
            */
            std::vector<std::string> GetClassNames() const;

            /*
            * @brief == 运算符
            * @param Right_
            */
            bool operator==(const CComponentMask& Right_) const;

            /*
            * @brief != 运算符
            * @param Right_
            */
            bool operator!=(const CComponentMask& Right_) const;

            /*
            * @brief < 运算符
            * @param Right_
            */
            bool operator<(const CComponentMask& Right_) const;

        private:
            BitSet m_Bits;
        };
    }
}