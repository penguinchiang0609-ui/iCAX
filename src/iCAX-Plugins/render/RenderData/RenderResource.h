#pragma once

#include "RenderDataExport.h"
#include "RenderDataTypes.h"

#include "Resources/ResourceFlatBuffer.h"

namespace iCAX::Render
{
    inline constexpr const char* kRenderGeometryIdentifier = "ICRG";
    inline constexpr const char* kRenderMaterialIdentifier = "ICRM";
    inline constexpr uint32_t kRenderResourceSchemaVersion = 1;

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderGeometryResource(IN const SRenderMeshData& Mesh_);

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderGeometryResource(IN const SRenderPolylineData& Polyline_);

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderGeometryResource(IN const SRenderToolpathData& Toolpath_);

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderMaterialResource(IN const SRenderMaterialData& Material_);

    _RENDER_DATA_EXP bool IsRenderGeometryResource(
        IN const iCAX::Resource::CFlatBufferResource& Resource_) noexcept;

    _RENDER_DATA_EXP bool IsRenderMaterialResource(
        IN const iCAX::Resource::CFlatBufferResource& Resource_) noexcept;
}
