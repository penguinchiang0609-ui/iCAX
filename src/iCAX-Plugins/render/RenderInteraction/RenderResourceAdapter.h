#pragma once

#include "RenderInteractionExport.h"

#include "RenderData/RenderDataTypes.h"
#include "Resources/ResourceInfo.h"

namespace iCAX::Resource
{
    class CResourceLibrary;
}

namespace iCAX::RenderInteraction
{
    /*
    * @brief 把业务/CAD 资源派生为前端可直接读取的标准渲染资源。
    * @details 返回值始终是资源池中的版本化 URL；这里不创建渲染场景，也不发布 PDO。
    */
    _RENDER_INTERACTION_EXP iCAX::Resource::CResourceReference
        EnsureFrontendGeometryResource(
            IN iCAX::Resource::CResourceLibrary& Resources_,
            IN const std::string& strSourceResourceURL_,
            IN iCAX::Render::ERenderGeometryKind eKind_);

    _RENDER_INTERACTION_EXP iCAX::Resource::CResourceReference
        EnsureFrontendMaterialResource(
            IN iCAX::Resource::CResourceLibrary& Resources_,
            IN const std::string& strSourceResourceURL_);
}
