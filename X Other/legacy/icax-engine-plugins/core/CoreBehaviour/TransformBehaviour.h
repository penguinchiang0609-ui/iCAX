#pragma once
#include "Core.h"
#include "Behaviour/BehaviourBase.h"
#include "../CoreComponent/TransformComponent.h"
#include "Behaviour/IBehaviourRegistry.h"

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
            * @param [in] pComponent_
            */
            virtual void OnAwake(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_) override;

            /*
            * @brief 创建之后下一帧触发
            * @param [in] pComponent_
            */
            virtual void OnStart(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_) override;

            /*
            * @brief 修改Enable状态时触发
            * @param [in] pComponent_
            */
            virtual void OnEnable(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_) override;

            /*
            * @brief 每一帧预触发
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnPreUpdate(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_) override;

            /*
            * @brief 每一帧触发
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnUpdate(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_) override;

            /*
            * @brief 每一帧后触发
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnPostUpdate(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_) override;

            /*
            * @brief 禁用时触发
            * @param [in] pComponent_
            */
            virtual void OnDisable(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_) override;

            /*
            * @brief 销毁时触发
            * @param [in] DestroyInfo_ 被销毁组件的快照信息。
            */
            virtual void OnDestroy(
                IN const iCAX::Behaviour::CComponentDestroyInfo& DestroyInfo_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_) override;

            /*
            * @brief 组件数据修改前触发
            * @param [in] pComponent_
            * @param [in] NewValues_
            */
            virtual void OnModifying(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Data::PropertySet& NewValues_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_) override;

            /*
            * @brief 组件数据修改后触发
            * @param [in] pComponent_
            * @param [in] NewValues_
            */
            virtual void OnModified(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Data::PropertySet& NewValues_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_) override;

            AUTO_REGIST_BEHAVIOUR(TransformBehaviour);


        };

        /*
        * @brief 变换
        * @param [in] Component_
        * @param [in] Tranform_
        */
        int Tranform(IN OUT void* pContext_, IN const void* IParam_, OUT void* OParam_);
    }
}
