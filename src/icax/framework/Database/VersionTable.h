#pragma once

#include "Data/uuid.h"
#include <unordered_map>
#include <unordered_set>

namespace iCAX
{
    namespace Database
    {
        struct VersionKey
        {
            iCAX::Data::uuid nDomain;
            iCAX::Data::uuid  nEntity;
            std::string strComponent;

            bool operator==(const VersionKey& other) const noexcept
            {
                return nDomain == other.nDomain &&
                    nEntity == other.nEntity &&
                    strComponent == other.strComponent;
            }
        };
        struct VersionKeyHash
        {
            std::size_t operator()(const VersionKey& k) const noexcept {
                std::size_t h1 = std::hash<iCAX::Data::uuid>{}(k.nDomain);
                std::size_t h2 = std::hash<iCAX::Data::uuid>{}(k.nEntity);
                std::size_t h3 = std::hash<std::string>{}(k.strComponent);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };

        struct VersionTable
        {
        public:
            /*
            * @brief 获取版本
            * @param [in] Key_
            * @returtn size_t
            */
            size_t GetVersion(IN const VersionKey& Key_) const;

            /*
            * @brief 版本号+1
            * @param [in] Key_
            */
            void BumpVersion(IN const VersionKey& Key_);

            /*
            * @brief 重置
            * @param [in] Key_
            */
            void Reset(IN const VersionKey& Key_);

            /*
            * @brief 移除
            * @param [in] Key_
            */
            void Remove(IN const VersionKey& Key_);

            /*
            * @brief 清空
            */
            void Clear();

            /*
            * @brief 清空dirty标记
            */
            void ClearDirty();

            /*
            * @brief 是否被污染
            * @param [in] Key_
            * @return bool
            */
            bool IsDirty(IN const VersionKey& Key_);

        private:
            std::unordered_map<VersionKey, size_t, VersionKeyHash> m_mapVersions;
            std::unordered_set<VersionKey, VersionKeyHash> m_setDirty;
        };
    }
}

//namespace std
//{
//    template <>
//    struct hash<iCAX::Database::VersionKey>
//    {
//        std::size_t operator()(IN const iCAX::Database::VersionKey& ptLHS_) const noexcept
//        {
//            auto HashCombine = [](std::size_t seed, std::size_t value) {
//                return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
//                };
//
//            std::size_t seed = 0;
//            seed = HashCombine(seed, std::hash<iCAX::Data::uuid>{}(ptLHS_.nDomain));
//            seed = HashCombine(seed, std::hash<iCAX::Data::uuid>{}(ptLHS_.nEntity));
//            seed = HashCombine(seed, std::hash<std::string>{}(ptLHS_.strComponent));
//            return seed;
//        }
//    };
//}