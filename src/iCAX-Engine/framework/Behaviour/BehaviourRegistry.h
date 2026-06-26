#pragma once
#include "System.h"
#include "BehaviourBase.h"
#include "IBehaviourRegistry.h"

#include <unordered_map>

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
            * @brief 注册 Behaviour 工厂
            * @param [in] Descriptor_ Behaviour 描述。
            */
            virtual void RegisterBehaviourFactory(IN const CBehaviourDescriptor& Descriptor_) override;

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
            * @brief 创建行为
            * @param [in] nType_
            * @return std::shared_ptr<CBehaviourBase>
            */
            virtual std::shared_ptr<CBehaviourBase> CreateBehaviourByType(IN const std::type_index& nType_) const override;

            /*
            * @brief 创建行为
            * @param [in] nType_
            * @return std::shared_ptr<CBehaviourBase>
            */
            virtual std::shared_ptr<CBehaviourBase> CreateBehaviourByType(IN const std::string& strType_) const override;

            /*
            * @brief 获取行为描述
            */
            virtual const CBehaviourDescriptor* GetDescriptorByType(IN const std::type_index& nType_) const override;

            /*
            * @brief 获取行为描述
            */
            virtual const CBehaviourDescriptor* GetDescriptorByType(IN const std::string& strType_) const override;

            /*
            * @brief 获取行为
            * @param [in] strComponent_ 组件名称
            * @return 绑定到该组件类型的 Behaviour C++ 类型列表。
            */
            virtual std::vector<std::type_index> GetTypeIndexByComponentType(IN const std::string& strComponent_) const override;

        private:
            std::unordered_map<std::type_index, CBehaviourDescriptor> m_Behaviours;
            std::unordered_map<std::string, std::type_index> m_BehaviourTypesByName;
            std::unordered_map<std::string, std::type_index> m_BehaviourTypesByComponent;
        };

    }
}
