#pragma once
#include "IUniverse.h"

namespace iCAX
{
    namespace Behaviour
    {
        class CBehaviourDispatcher;
        class IBehaviourRegistry;

        /*
        * @brief 宇宙
        * @remark 
        *   Universe 是行为运行容器，不拥有 Repository，也不代表 Project。
        *   Scene 线程负责驱动 Universe；Universe 不提供跨线程同步。
        */
        class CUniverse final : public IUniverse
        {
        public:
            /*
            * @brief 构造函数
            */
            explicit CUniverse(IN std::shared_ptr<IBehaviourRegistry> pRegistry_);

            /*
            * @brief 析构函数
            */
            virtual ~CUniverse();

        public:
            /*
            * @brief 获取宇宙ID
            * @return iCAX::Data::uuid
            */
            virtual iCAX::Data::uuid GetID() const override;

            /*
            * @brief 帧前交换PDO双缓冲
            */
            virtual void PreSwapPDO() override;

            /*
            * @brief 每帧执行
            */
            virtual void Tick(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_) override;

            /*
            * @brief 帧后交换PDO双缓冲
            */
            virtual void PostSwapPDO() override;

            /*
            * @brief 清空
            * @param [in] bForced_
            */
            virtual void Cleanup(IN const bool& bForced_ = false) override;

            /*
            * @brief 是否有效
            * @return bool
            */
            virtual bool IsValid() const override;

        public:
            /*
            * @brief 修改前事件
            * @param [in] Args_
            */
            virtual void OnRepositoryChanging(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) override;

            /*
            * @brief 更改后事件
            * @param [in] Args_
            */
            virtual void OnRepositoryChanged(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) override;

            virtual bool BindBehaviourByIndex(IN const std::type_index& nType_) override;
            virtual bool HasBindBehaviourByIndex(IN const std::type_index& nType_) const override;
            virtual void UnbindBehaviourByIndex(IN const std::type_index& nType_) override;
            virtual void PauseFrameUpdateByIndex(IN const std::type_index& nType_) override;
            virtual bool IsFrameUpdatePausedByIndex(IN const std::type_index& nType_) const override;
            virtual void ResumeFrameUpdateByIndex(IN const std::type_index& nType_) override;
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetFrameUpdatePausedBehaviours() const override;
            virtual std::vector<std::shared_ptr<CBehaviourBase>> GetAllBehaviours() const override;

        private:
            iCAX::Data::uuid m_ID;
            std::shared_ptr<CBehaviourDispatcher> m_pDispatcher;
            std::shared_ptr<IBehaviourRegistry> m_pRegistry;
        };
    }
}
