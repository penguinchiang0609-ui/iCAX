#pragma once
#include "System.h"
#include <memory>
#include <string>
#include <vector>
#include "../Database/ComponentBase.h"
#include "Data/uuid.h"
#include "IUniverseContext.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 行为
        * @remark 每个组件对应一个行为，行为必须无状态
        */
        class _SYSTEM_EXP CBehaviourBase
        {
        public:
            virtual ~CBehaviourBase() = default;

        public:
            /*
            * @brief 获取行为类型名称
            */
            virtual std::string GetBehaviourClass() const = 0;

            /*
            * @brief 获取该行为关注的组件类型名称
            */
            virtual std::string GetComponentClass() const = 0;

        public:
            void Awake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);
            void Start(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);
            void Enable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);
            void PreUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_);
            void Update(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_);
            void PostUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_);
            void Disable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);
            void Destory(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);
            void Modifing(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_);
            void Modified(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_);

        public:
            /*
            * @brief 获取函数指针
            * @param [in] strFnName_
            * @return void*
            */
            virtual void* GetFunction(const std::string& strFnName_) const { return nullptr; };

        protected:
            virtual void OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) {}
            virtual void OnStart(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) {}
            virtual void OnEnable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) {}
            virtual void OnPreUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) {}
            virtual void OnUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) {}
            virtual void OnPostUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) {}
            virtual void OnDisable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) {}
            virtual void OnDestroy(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) {}
            virtual void OnModifing(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_) {}
            virtual void OnModified(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_) {}
        };
    }
}
