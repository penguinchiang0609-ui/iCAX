#pragma once
#include "Core.h"
#include "Behaviour/BehaviourBase.h"
#include "../CoreComponent/TransformComponent.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "ProcedureCall/IPCRegistry.h"

using namespace iCAX::Behaviour;

namespace iCAX
{
    namespace Core
    {
        /*
        * @brief 层次
        */
        class _COREBEHAVIOUR_EXP TransformBehaviour : public CBehaviourBase
        {
        public:
            /*
            * @brief 构造函数
            */
            TransformBehaviour();

            /*
            * @brief 析构函数
            */
            virtual ~TransformBehaviour();

        public:
            /*
            * @brief 获取组件类型名称
            * @return std::string
            * @details
            *   该行为会关注主类型的增删改
            */
            virtual std::string GetComponentClass() const override;

        protected:
            /*
            * @brief 创建之后触发
            * @param [in] World_
            * @param [in] pComponent_
            */
            virtual void OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 创建之后下一帧触发
            * @param [in] World_
            * @param [in] pComponent_
            */
            virtual void OnStart(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 修改Enable状态时触发
            * @param [in] World_
            * @param [in] pComponent_
            */
            virtual void OnEnable(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 每一帧预触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnPreUpdate(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 每一帧触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnUpdate(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 每一帧后触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnPostUpdate(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 禁用时触发
            * @param [in] World_
            * @param [in] pComponent_
            */
            virtual void OnDisable(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 销毁时触发
            * @param [in] World_
            * @param [in] pComponent_
            */
            virtual void OnDestroy(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 组件数据修改前触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] NewValues_
            */
            virtual void OnModifing(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 组件数据修改后触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] NewValues_
            */
            virtual void OnModified(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_) override;

            AUTO_REGIST_BEHAVIOUR(TransformBehaviour);


        };

        /*
        * @brief 变换
        * @param [in] World_
        * @param [in] Component_
        * @param [in] Tranform_
        */
        int Tranform(IN OUT void* pContext_, IN const void* IParam_, OUT void* OParam_); //(IN class IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Math::Tranform3& Tranform_);

        AUTO_REGIST_PC(CoreModule, Tranform)
       /* AUTO_REGIST_PC(TransformBehaviour, Tranform, "void(iCAX::Behaviour::IWorld&, iCAX::Database::CComponentBase&, const iCAX::Math::Tranform3&)>");*/
    }
}
