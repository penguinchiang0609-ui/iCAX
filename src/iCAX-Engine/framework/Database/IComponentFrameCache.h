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
        * @brief 组件帧缓存
        * @details
        *   按组件类维护当前帧和上一帧的组件集合。
        *   BehaviourDispatcher 使用两份集合判断哪些组件需要触发 Start。
        *   该接口不是 Entity 查询 View，不表达业务视图或 Entity 成员关系。
        */
        class _DATABASE_EXP IComponentFrameCache
        {
        public:
            IComponentFrameCache() = default;
            virtual ~IComponentFrameCache() = default;

        public:
            /*
            * @brief 确保指定组件类的当前帧缓存已经构建
            * @param [in] strClassName_ 组件类名
            * @param [in] bForceReset_ 是否强制重建
            * @details 缓存包含拥有该组件或其子类组件的实体组件。
            */
            virtual void EnsureComponentCache(
                IN const std::string& strClassName_,
                IN const bool bForceReset_ = false) = 0;

            /*
            * @brief 获取当前帧组件集合
            * @param [in] strClassName_ 组件类名
            */
            virtual std::vector<std::shared_ptr<CComponentBase>> GetCurrentComponents(
                IN const std::string& strClassName_) = 0;

            /*
            * @brief 获取上一帧组件集合
            * @param [in] strClassName_ 组件类名
            */
            virtual std::vector<std::shared_ptr<CComponentBase>> GetPreviousComponents(
                IN const std::string& strClassName_) = 0;

            /*
            * @brief 将当前组件集合保存为下一帧的上一帧集合
            * @details BehaviourDispatcher 在完成 Start 检测后调用。
            */
            virtual void CaptureCurrentAsPrevious() = 0;
        };
    }
}
