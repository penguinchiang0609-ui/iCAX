#pragma once

#include "ViewContent.h"

#include "Services/IService.h"

namespace iCAX::View
{
    /*
    * @brief 产品作用域的 View 内容服务。
    * @details 服务保存可重建缓存；调用方不在渲染帧中查询 Repository。
    */
    class _VIEW_EXP IViewContentService : public iCAX::Services::IService
    {
    public:
        ~IViewContentService() override = default;

        /*
        * @brief 创建独立的 View 运行实例。
        * @details 每次调用都创建新的 InstanceID、Database EntityView 使用权和输出会话。
        */
        virtual SViewContent OpenView(
            IN iCAX::Project::IProjectContext& Project_,
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewDefinitionID& DefinitionID_,
            IN const iCAX::Data::ObjectMap& Context_ = {}) = 0;

        /*
        * @brief 读取已创建 ViewInstance 的最新内容。
        */
        virtual SViewContent GetContent(
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewInstanceID& InstanceID_) = 0;

        /*
        * @brief 释放 ViewInstance 及其全部输出资源。
        */
        virtual bool CloseView(
            IN iCAX::Project::IProjectContext& Project_,
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewInstanceID& InstanceID_) = 0;

        /*
        * @brief 兼容旧调用的一次性 Definition 内容缓存。
        * @details 该入口不创建输出会话；需要 PDO/渲染等运行资源时应使用 OpenView。
        */
        virtual SViewContent GetContent(
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewDefinitionID& DefinitionID_,
            IN const iCAX::Data::ObjectMap& Context_ = {}) = 0;
    };
}
