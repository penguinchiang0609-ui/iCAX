#pragma once
#include "System.h"
#include "BehaviourBase.h"
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace iCAX
{
    namespace Behaviour
    {
        class IBehaviourRegistry;

        /*
        * @brief Behaviour调度器
        * @remark
        *   Dispatcher 保存 Universe 已绑定的行为列表，并负责按帧顺序或 Repository 事件调用 Behaviour。
        *   行为执行顺序等于绑定顺序，业务逻辑不要依赖 unordered_set 的遍历顺序。
        */
        class _SYSTEM_EXP CBehaviourDispatcher final
        {
        public:
            enum NotifyType
            {
                kAddComponent = 0,
                kEnableComponent,
                kDisableComponent,
                kDestroyComponent,
                kModifingComponent,
                kModifiedComponent,
            };

        public:
            /*
            * @brief 构造函数
            */
            explicit CBehaviourDispatcher(IN std::shared_ptr<IBehaviourRegistry> pRegistry_);

        public:
            /*
            * @brief 追加行为
            * @param [in] nType_
            * @return true 表示新增绑定；false 表示该行为已绑定。
            */
            bool Pushback(IN const std::type_index& nType_);

            /*
            * @brief 暂停指定类型的行为
            * @param [in] nType_
            */
            void Pause(IN const std::type_index& nType_);

            /*
            * @brief 是否暂停
            * @param [in] nType_
            * @return true 表示该行为已暂停。
            */
            bool IsPaused(IN const std::type_index& nType_) const;

            /*
            * @brief 恢复指定类型的行为执行
            * @param [in] nType_
            */
            void Resume(IN const std::type_index& nType_);

            /*
            * @brief 获取所有被暂停的行为
            * @return  std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>>
            */
            std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> GetPaused() const;

            /*
            * @brief 获取所有被绑定的行为
            * @return  std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>>
            */
            std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> GetALL() const;

            /*
            * @brief 是否已包含
            * @param [in] nType_
            * @return bool
            */
            bool HasBehaviour(IN const std::type_index& nType_) const;

            /*
            * @brief 注销行为
            * @param [in] nType_
            */
            void UnregisterBehaviour(IN const std::type_index& nType_);

            /*
            * @brief tick
            * @param [in] Context_ Universe 上下文。
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            * @details
            *   执行顺序为：新组件 Start -> RefreshPreCache -> PreUpdate -> Update -> PostUpdate。
            *   暂停的行为在上述阶段都会跳过。
            */
            void Tick(IN const IUniverseContext& Context_, IN const double& nDeltaTime_, IN const double& nTotalTime_) const;

            /*
            * @brief 通知
            * @param [in] Context_ Universe 上下文。
            * @param [in] nType_
            * @param [in] pComponent_
            * @param [in] Properties_
            * @details Repository 事件会被映射到 Awake/Enable/Disable/Destroy/Modifying/Modified。
            */
            void OnNotify(IN const IUniverseContext& Context_, IN NotifyType nType_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& Properties_) const;

        private:
            std::unordered_set<std::type_index> m_setBehaviourIndex;
            //! 一个Component只有一个Behaviour与之对应
            //! 如果发现一个behaviour越来越庞大，此刻应该思考component是否应该拆分
            std::unordered_map<std::string, std::shared_ptr<CBehaviourBase>> m_BehavioursMap;
            std::vector<std::shared_ptr<CBehaviourBase>> m_OrderedList;
            std::unordered_set<std::type_index> m_setPaused;
            std::shared_ptr<IBehaviourRegistry> m_pRegistry;
        };
    }
}
