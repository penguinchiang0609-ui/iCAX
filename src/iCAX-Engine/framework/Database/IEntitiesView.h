#pragma once
#include "Database.h"
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        class CComponentBase;

        /*
        * @brief 实体集视图
        * @remark
        *   根据组件掩码构建实体集视图缓存。
        *   BehaviourDispatcher 使用当前缓存和上一帧缓存判断哪些组件需要触发 Start。
        */
        class _DATABASE_EXP IEntitiesView
        {
        public:
            /*
            * @brief 构造函数
            */
            IEntitiesView() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IEntitiesView() = default;

        public:
            /*
            * @brief 构建缓存
            * @param [in] strClassName_ 组件类名
            * @param [in] bForceReset_ 是否强制重建
            * @details 缓存包含拥有该组件或其子类组件的实体组件。
            */
            virtual void BuildCache(IN const std::string& strClassName_, IN const bool bForceReset_ = false) = 0;

            /*
            * @brief 获取缓存
            * @param [in] strClassName_ 组件类名
            * @return std::vector<std::shared_ptr<CComponentBase>>
            */
            virtual std::vector<std::shared_ptr<CComponentBase>> GetEntities(IN const std::string& strClassName_) = 0;

            /*
            * @brief 获取前缓存
            * @param [in] strClassName_ 组件类名
            * @return std::vector<std::shared_ptr<CComponentBase>>
            * @details 前缓存表示上一帧的组件集合。
            */
            virtual std::vector<std::shared_ptr<CComponentBase>> GetPreEntities(IN const std::string& strClassName_) = 0;

            /*
            * @brief 刷新前缓存
            * @details 通常在 BehaviourDispatcher 完成 Start 检测后调用。
            */
            virtual void RefreshPreCache() = 0;
        };
    }
}
