#pragma once

#include "RenderDataExport.h"
#include "RenderDataTypes.h"

#include "Resources/ResourceFlatBuffer.h"

namespace iCAX::Render
{
    inline constexpr const char* kRenderGeometryIdentifier = "ICRG";
    inline constexpr const char* kRenderMaterialIdentifier = "ICRM";
    inline constexpr const char* kRenderTextureIdentifier = "ICRT";
    inline constexpr uint32_t kRenderResourceSchemaVersion = 1;

    struct SRenderMeshEdges
    {
        std::vector<SFloat3> Positions;
        // Packed (first point, point count, feature edge flag) per CAD edge.
        std::vector<uint32_t> Ranges;
    };
    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderGeometryResource(const SRenderMeshData& Mesh_, const SRenderMeshEdges& Edges_);

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderGeometryResource(IN const SRenderMeshData& Mesh_);

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderGeometryResource(IN const SRenderPolylineData& Polyline_);

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderGeometryResource(IN const SRenderToolpathData& Toolpath_);

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderMaterialResource(IN const SRenderMaterialData& Material_);

    _RENDER_DATA_EXP iCAX::Resource::CFlatBufferResource
        MakeRenderTextureResource(
            IN const std::vector<std::uint8_t>& EncodedImage_,
            IN uint32_t nPixelWidth_, IN uint32_t nPixelHeight_,
            IN const std::string& strMimeType_ = "image/png",
            IN uint64_t nDataVersion_ = 1);

    _RENDER_DATA_EXP bool IsRenderGeometryResource(
        IN const iCAX::Resource::CFlatBufferResource& Resource_) noexcept;

    _RENDER_DATA_EXP bool IsRenderMaterialResource(
        IN const iCAX::Resource::CFlatBufferResource& Resource_) noexcept;

    _RENDER_DATA_EXP bool IsRenderTextureResource(
        IN const iCAX::Resource::CFlatBufferResource& Resource_) noexcept;
}
