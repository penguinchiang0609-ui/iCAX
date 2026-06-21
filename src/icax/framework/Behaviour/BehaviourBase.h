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
        * @remark
        *   1、Behaviour 是 Component 对应的 system/行为逻辑。
        *   2、行为对象由注册表复用，必须尽量无状态；项目数据应通过 Component、Repository 或 Context 获取。
        *   3、公开生命周期方法由 Dispatcher 调用，派生类只需要覆写 OnXxx 钩子。
        */
        class _SYSTEM_EXP CBehaviourBase
        {
        public:
            virtual ~CBehaviourBase() = default;

        public:
            /*
            * @brief 获取行为类型名称
            * @return 行为类名，通常由 AUTO_REGIST_BEHAVIOUR 宏生成。
            */
            virtual std::string GetBehaviourClass() const = 0;

            /*
            * @brief 获取该行为关注的组件类型名称
            * @return 组件类型名称。
            */
            virtual std::string GetComponentClass() const = 0;

        public:
            /*
            * @brief 组件进入 Universe 后的首次通知。
            * @param [in,out] Component_ 目标组件。
            * @param [in] Context_ Universe 运行上下文。
            * @details 默认会先为该组件类型构建实体视图缓存，再调用 OnAwake。
            */
            void Awake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);

            /*
            * @brief 组件第一次参与帧循环时调用。
            */
            void Start(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);

            /*
            * @brief 组件启用后调用。
            */
            void Enable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);

            /*
            * @brief 帧更新前调用。
            * @param [in] nDeltaTime_ 当前帧间隔秒数。
            * @param [in] nTotalTime_ 累计运行秒数。
            */
            void PreUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_);

            /*
            * @brief 帧更新调用。
            */
            void Update(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_);

            /*
            * @brief 帧更新后调用。
            */
            void PostUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_);

            /*
            * @brief 组件禁用后调用。
            */
            void Disable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);

            /*
            * @brief 组件销毁前调用。
            * @details 保留历史拼写 Destory，不在本次注释整理中改名。
            */
            void Destory(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_);

            /*
            * @brief 组件字段修改前调用。
            * @param [in] NewValues_ 即将写入的新字段集合。
            */
            void Modifing(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_);

            /*
            * @brief 组件字段修改后调用。
            * @param [in] NewValues_ 已写入的新字段集合。
            */
            void Modified(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_);

        public:
            /*
            * @brief 获取函数指针
            * @param [in] strFnName_ 函数名称。
            * @return 函数指针；不存在时返回 nullptr。
            * @details 该接口用于少量动态调用场景，常规行为逻辑优先通过生命周期钩子表达。
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
