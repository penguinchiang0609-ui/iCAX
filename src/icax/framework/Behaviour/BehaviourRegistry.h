#pragma once
#include "System.h"
#include "BehaviourBase.h"
#include "IBehaviourRegistry.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief Behaviour注册表
        */
        class CBehaviourRegistry final : public IBehaviourRegistry
        {
        public:
            /*
            * @brief 构造函数
            */
            CBehaviourRegistry();

            /*
            * @brief 构造函数
            */
            virtual ~CBehaviourRegistry();

        public:
            /*
            * @brief 注册Behaviour
            * @param [in] pBehaviour_
            */
            virtual void RegisterBehaviourByInstance(IN const std::shared_ptr<CBehaviourBase>& pBehaviour_) override;

            /*
            * @brief 是否已包含
            * @param [in] nType_
            * @return bool
            */
            virtual bool HasBehaviourByType(IN const std::type_index& nType_) const override;

            /*
            * @brief 是否已包含
            * @param [in] nType_
            * @return bool
            */
            virtual bool HasBehaviourByType(IN const std::string& strType_) const override;

            /*
            * @brief 获取行为
            * @param [in] nType_
            * @return std::shared_ptr<CBehaviourBase>
            */
            virtual std::shared_ptr<CBehaviourBase> GetBehaviourByType(IN const std::type_index& nType_) const override;

            /*
            * @brief 获取行为
            * @param [in] nType_
            * @return std::shared_ptr<CBehaviourBase>
            */
            virtual std::shared_ptr<CBehaviourBase> GetBehaviourByType(IN const std::string& strType_) const override;

            /*
            * @brief 获取行为
            * @param [in] strComponent_ 组件名称
            * @return std::type_index
            */
            virtual std::vector<std::type_index> GetTypeIndexByComponentType(IN const std::string& strComponent_) const override;

        private:
            std::unordered_map<std::type_index, std::shared_ptr<CBehaviourBase>> m_Behaviours;
            std::unordered_map<std::string, std::shared_ptr<CBehaviourBase>> m_BehavioursByName;
        };

    }
}