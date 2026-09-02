#include "pch.h"

#include "RenderResource.h"

namespace
{
    template<typename TPoint>
    std::vector<float> Flatten3(IN const std::vector<TPoint>& Points_)
    {
        std::vector<float> _Values;
        _Values.reserve(Points_.size() * 3);
        for (const auto& _Point : Points_)
        {
            _Values.push_back(_Point.x);
            _Values.push_back(_Point.y);
            _Values.push_back(_Point.z);
        }
        return _Values;
    }

    std::vector<float> Flatten2(
        IN const std::vector<iCAX::Render::SFloat2>& Points_)
    {
        std::vector<float> _Values;
        _Values.reserve(Points_.size() * 2);
        for (const auto& _Point : Points_)
        {
            _Values.push_back(_Point.x);
            _Values.push_back(_Point.y);
        }
        return _Values;
    }

    iCAX::Resource::CFlatBufferResource FinishGeometry(
        IN const iCAX::Render::ERenderGeometryKind eKind_,
        IN const uint64_t nDataVersion_,
        IN const uint32_t nTopology_,
        IN const uint32_t nFlags_,
        IN const std::vector<float>& Positions_,
        IN const std::vector<float>& Normals_,
        IN const std::vector<float>& TextureCoordinates_,
        IN const std::vector<uint32_t>& VertexColors_,
        IN const std::vector<uint32_t>& Indices_,
        IN const std::vector<uint32_t>& Ranges_,
        IN const std::vector<float>& ToolAxes_,
        IN const std::vector<float>& Feeds_,
        IN const std::vector<uint32_t>& PointFlags_,
        IN const std::vector<uint32_t>& Spans_)
    {
        flatbuffers::FlatBufferBuilder _Builder;
        const auto _Positions = _Builder.CreateVector(Positions_);
        const auto _Normals = _Builder.CreateVector(Normals_);
        const auto _TextureCoordinates =
            _Builder.CreateVector(TextureCoordinates_);
        const auto _VertexColors = _Builder.CreateVector(VertexColors_);
        const auto _Indices = _Builder.CreateVector(Indices_);
        const auto _Ranges = _Builder.CreateVector(Ranges_);
        const auto _ToolAxes = _Builder.CreateVector(ToolAxes_);
        const auto _Feeds = _Builder.CreateVector(Feeds_);
        const auto _PointFlags = _Builder.CreateVector(PointFlags_);
        const auto _Spans = _Builder.CreateVector(Spans_);

        const auto _Start = _Builder.StartTable();
        _Builder.AddElement<uint32_t>(4, 1, 0);
        _Builder.AddElement<uint32_t>(6, static_cast<uint32_t>(eKind_), 0);
        _Builder.AddElement<uint64_t>(8, nDataVersion_, 0);
        _Builder.AddElement<uint32_t>(10, nTopology_, 0);
        _Builder.AddElement<uint32_t>(12, nFlags_, 0);
        _Builder.AddOffset(14, _Positions);
        _Builder.AddOffset(16, _Normals);
        _Builder.AddOffset(18, _TextureCoordinates);
        _Builder.AddOffset(20, _VertexColors);
        _Builder.AddOffset(22, _Indices);
        _Builder.AddOffset(24, _Ranges);
        _Builder.AddOffset(26, _ToolAxes);
        _Builder.AddOffset(28, _Feeds);
        _Builder.AddOffset(30, _PointFlags);
        _Builder.AddOffset(32, _Spans);
        const auto _Root = _Builder.EndTable(_Start);
        _Builder.Finish(
            flatbuffers::Offset<void>(_Root),
            iCAX::Render::kRenderGeometryIdentifier);
        return iCAX::Resource::MakeFlatBufferResource(_Builder);
    }

    bool HasIdentifier(
        IN const iCAX::Resource::CFlatBufferResource& Resource_,
        IN const char* pIdentifier_) noexcept
    {
        if (Resource_.Size() < 8 || !pIdentifier_)
        {
            return false;
        }
        return std::memcmp(Resource_.Data() + 4, pIdentifier_, 4) == 0;
    }
}

iCAX::Resource::CFlatBufferResource iCAX::Render::MakeRenderGeometryResource(
    IN const SRenderMeshData& Mesh_)
{
    return FinishGeometry(
        ERenderGeometryKind::Mesh,
        Mesh_.nDataVersion,
        static_cast<uint32_t>(Mesh_.eTopology),
        Mesh_.nFlags,
        Flatten3(Mesh_.Positions),
        Flatten3(Mesh_.Normals),
        Flatten2(Mesh_.TextureCoordinates),
        Mesh_.VertexColorsRGBA,
        Mesh_.Indices,
        {}, {}, {}, {}, {});
}

iCAX::Resource::CFlatBufferResource iCAX::Render::MakeRenderGeometryResource(
    IN const SRenderPolylineData& Polyline_)
{
    std::vector<uint32_t> _Ranges;
    _Ranges.reserve(Polyline_.Ranges.size() * 5);
    for (const auto& _Range : Polyline_.Ranges)
    {
        _Ranges.push_back(_Range.nFirstPoint);
        _Ranges.push_back(_Range.nPointCount);
        _Ranges.push_back(static_cast<uint32_t>(_Range.eRenderClass));
        _Ranges.push_back(_Range.nStyleID);
        _Ranges.push_back(_Range.nFlags);
    }
    return FinishGeometry(
        ERenderGeometryKind::Polyline,
        Polyline_.nDataVersion,
        static_cast<uint32_t>(ERenderTopology::LineStrip),
        0,
        Flatten3(Polyline_.Points),
        {}, {}, {}, {}, _Ranges, {}, {}, {}, {});
}

iCAX::Resource::CFlatBufferResource iCAX::Render::MakeRenderGeometryResource(
    IN const SRenderToolpathData& Toolpath_)
{
    std::vector<SFloat3> _Positions;
    std::vector<SFloat3> _Axes;
    std::vector<float> _Feeds;
    std::vector<uint32_t> _PointFlags;
    _Positions.reserve(Toolpath_.Points.size());
    _Axes.reserve(Toolpath_.Points.size());
    _Feeds.reserve(Toolpath_.Points.size());
    _PointFlags.reserve(Toolpath_.Points.size());
    for (const auto& _Point : Toolpath_.Points)
    {
        _Positions.push_back(_Point.Position);
        _Axes.push_back(_Point.ToolAxis);
        _Feeds.push_back(_Point.nFeed);
        _PointFlags.push_back(_Point.nFlags);
    }
    std::vector<uint32_t> _Spans;
    _Spans.reserve(Toolpath_.Spans.size() * 6);
    for (const auto& _Span : Toolpath_.Spans)
    {
        _Spans.push_back(_Span.nFirstPoint);
        _Spans.push_back(_Span.nPointCount);
        _Spans.push_back(static_cast<uint32_t>(_Span.eMoveKind));
        _Spans.push_back(_Span.nToolID);
        _Spans.push_back(_Span.nStyleID);
        _Spans.push_back(_Span.nFlags);
    }
    return FinishGeometry(
        ERenderGeometryKind::Toolpath,
        Toolpath_.nDataVersion,
        static_cast<uint32_t>(ERenderTopology::LineStrip),
        0,
        Flatten3(_Positions),
        {}, {}, {}, {}, {}, Flatten3(_Axes), _Feeds, _PointFlags, _Spans);
}

iCAX::Resource::CFlatBufferResource iCAX::Render::MakeRenderMaterialResource(
    IN const SRenderMaterialData& Material_)
{
    flatbuffers::FlatBufferBuilder _Builder;
    const auto _Texture =
        _Builder.CreateString(Material_.strBaseColorTextureResourceID);
    const auto _Start = _Builder.StartTable();
    _Builder.AddElement<uint32_t>(4, 1, 0);
    _Builder.AddElement<uint64_t>(6, Material_.nDataVersion, 0);
    _Builder.AddElement<uint32_t>(8, Material_.nColorRGBA, 0xFFFFFFFFu);
    _Builder.AddElement<uint32_t>(10, Material_.nAmbientRGBA, 0xFFFFFFFFu);
    _Builder.AddElement<uint32_t>(12, Material_.nSpecularRGBA, 0x000000FFu);
    _Builder.AddElement<uint32_t>(14, Material_.nEmissiveRGBA, 0x000000FFu);
    _Builder.AddElement<float>(16, Material_.nLineWidth, 1.0f);
    _Builder.AddElement<uint32_t>(18, Material_.nFlags, 0);
    _Builder.AddElement<uint32_t>(20, Material_.nMaterialFlags, 0);
    _Builder.AddOffset(22, _Texture);
    const auto _Root = _Builder.EndTable(_Start);
    _Builder.Finish(
        flatbuffers::Offset<void>(_Root),
        kRenderMaterialIdentifier);
    return iCAX::Resource::MakeFlatBufferResource(_Builder);
}

bool iCAX::Render::IsRenderGeometryResource(
    IN const iCAX::Resource::CFlatBufferResource& Resource_) noexcept
{
    return HasIdentifier(Resource_, kRenderGeometryIdentifier);
}

bool iCAX::Render::IsRenderMaterialResource(
    IN const iCAX::Resource::CFlatBufferResource& Resource_) noexcept
{
    return HasIdentifier(Resource_, kRenderMaterialIdentifier);
}
