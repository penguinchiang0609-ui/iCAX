#pragma once
#include "Render.h"
#include "Behaviour/BehaviourBase.h"
#include "../RenderComponent/SceneComponent.h"
#include "Behaviour/BehaviourHelper.h"

using namespace iCAX::Behaviour;

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief
        */
        class _RENDERBEHAVIOUR_EXP CSceneBehaviour : public CBehaviourBase
        {
        public:
            /*
            * @brief 构造函数
            */
            CSceneBehaviour();

            /*
            * @brief 析构函数
            */
            virtual ~CSceneBehaviour();

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
            virtual void OnStart(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 修改Enable状态时触发
            * @param [in] World_
            * @param [in] pComponent_
            */
            virtual void OnEnable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 每一帧预触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnPreUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 每一帧触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 每一帧后触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            virtual void OnPostUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 禁用时触发
            * @param [in] World_
            * @param [in] pComponent_
            */
            virtual void OnDisable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 销毁时触发
            * @param [in] World_
            * @param [in] pComponent_
            */
            virtual void OnDestroy(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 组件数据修改前触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] NewValues_
            */
            virtual void OnModifing(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_) override;

            /*
            * @brief 组件数据修改后触发
            * @param [in] World_
            * @param [in] pComponent_
            * @param [in] NewValues_
            */
            virtual void OnModified(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_) override;
            AUTO_REGIST_BEHAVIOUR(CSceneBehaviour);
        };
    }
}