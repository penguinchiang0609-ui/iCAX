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

        virtual SViewContent GetContent(
            IN iCAX::Project::ISceneContext& Scene_,
            IN const ViewDefinitionID& DefinitionID_,
            IN const iCAX::Data::ObjectMap& Context_ = {}) = 0;
    };
}
