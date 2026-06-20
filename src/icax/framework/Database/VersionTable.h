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
            iCAX::Data::uuid  nEntity;
            std::string strComponent;

            bool operator==(const VersionKey& other) const noexcept
            {
                return nEntity == other.nEntity &&
                    strComponent == other.strComponent;
            }
        };
        struct VersionKeyHash
        {
            std::size_t operator()(const VersionKey& k) const noexcept {
                std::size_t h1 = std::hash<iCAX::Data::uuid>{}(k.nEntity);
                std::size_t h2 = std::hash<std::string>{}(k.strComponent);
                return h1 ^ (h2 << 1);
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
