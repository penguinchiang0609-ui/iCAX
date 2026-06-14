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
        /*
        * @brief Behaviour调度器
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
            CBehaviourDispatcher();

        public:
            /*
            * @brief 追加行为
            * @param [in] nType_
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
            * @param [in] World_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            void Tick(IN const IUniverseContext& Context_, IN class IWorld& World_, IN const double& nDeltaTime_, IN const double& nTotalTime_) const;

            /*
            * @brief 通知
            * @param [in] World_
            * @param [in] nType_
            * @param [in] pComponent_
            * @param [in] Properties_
            */
            void OnNotify(IN const IUniverseContext& Context_, IN class IWorld& World_, IN NotifyType nType_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& Properties_) const;

        private:
            std::unordered_set<std::type_index> m_setBehaviourIndex;
            //! 一个Component只有一个Behaviour与之对应
            //! 如果发现一个behaviour越来越庞大，此刻应该思考component是否应该拆分
            std::unordered_map<std::string, std::shared_ptr<CBehaviourBase>> m_BehavioursMap;
            std::vector<std::shared_ptr<CBehaviourBase>> m_OrderedList;
            std::unordered_set<std::type_index> m_setPaused;
        };
    }
}
