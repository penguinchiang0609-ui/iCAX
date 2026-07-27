#pragma once

#include "IViewContentService.h"

#include <memory>

namespace iCAX::View
{
    /*
    * @brief 基于 Database IEntityView 的通用 View 内容服务。
    * @details
    *   派生产品只注册 SViewDefinition；成员集合、revision 与缓存不再由产品自行监听 Repository 维护。
    */
    class _VIEW_EXP CViewContentService : public IViewContentService
    {
    public:
        CViewContentService();
        ~CViewContentService() override;

        void OnLoad() override;
        void OnUnload() override;

        void OnSceneTick(
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN const iCAX::Product::IProductContext& ProductContext_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN const double& nDeltaTime_,
            IN const double& nTotalTime_) override;

        SViewContent OpenView(
            IN iCAX::Project::IProjectContext& Project_,
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewDefinitionID& DefinitionID_,
            IN const iCAX::Data::ObjectMap& Context_ = {}) override;

        SViewContent GetContent(
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewInstanceID& InstanceID_) override;

        bool CloseView(
            IN iCAX::Project::IProjectContext& Project_,
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewInstanceID& InstanceID_) override;

        SViewContent GetContent(
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewDefinitionID& DefinitionID_,
            IN const iCAX::Data::ObjectMap& Context_ = {}) override;

    protected:
        bool RegisterDefinition(IN SViewDefinition Definition_);

    private:
        struct SImpl;
        std::unique_ptr<SImpl> m_pImpl;
    };
}
