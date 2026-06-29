#include <gtest/gtest.h>

#include <RenderPDO/RenderPDO.h>

#include <stdexcept>
#include <string>
#include <type_traits>

using namespace iCAX::RenderPDO;

namespace
{
    template<typename THeader>
    void InitializeHeader(
        THeader& Header_,
        ERenderPDOPayloadKind eKind_,
        uint64_t nPayloadSize_,
        uint64_t nDataVersion_ = 1)
    {
        Header_.Header.nPayloadKind = static_cast<uint32_t>(eKind_);
        Header_.Header.nHeaderSize = sizeof(THeader);
        Header_.Header.nPayloadSize = nPayloadSize_;
        Header_.Header.nDataVersion = nDataVersion_;
    }
}

TEST(RenderPDOLayoutTest, LayoutsAreBinarySafe)
{
    EXPECT_TRUE(std::is_standard_layout_v<SRenderPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderPDOHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderAABB>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderAABB>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderMeshPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderMeshPDOHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderPolylinePDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderPolylinePDOHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderToolpathPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderToolpathPDOHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderInstanceListPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderInstanceListPDOHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderViewStateData>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderViewStateData>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderViewStatePDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderViewStatePDOHeader>);
}

TEST(RenderPDODeclTest, CreatesPDODeclarationForRenderPayload)
{
    const auto _Decl = MakeRenderPDODecl(
        ERenderPDOPayloadKind::Mesh,
        "main-view",
        iCAX::PDO::kDirection2External,
        4096);

    EXPECT_EQ(kRenderPDOLayoutVersion, _Decl.nVersion);
    EXPECT_NE(0u, _Decl.nID);
    EXPECT_EQ(iCAX::PDO::kDirection2External, _Decl.eDirection);
    EXPECT_EQ(4096, _Decl.nPayloadSize);
    EXPECT_STREQ("render.mesh", GetRenderPDOPayloadTypeName(ERenderPDOPayloadKind::Mesh));
    EXPECT_STREQ("render.view_state", GetRenderPDOPayloadTypeName(ERenderPDOPayloadKind::ViewState));

    EXPECT_THROW(MakeRenderPDODecl(ERenderPDOPayloadKind::Unknown, "main", iCAX::PDO::kDirection2External, 16), std::invalid_argument);
    EXPECT_THROW(MakeRenderPDODecl(ERenderPDOPayloadKind::Mesh, "", iCAX::PDO::kDirection2External, 16), std::invalid_argument);
    EXPECT_THROW(MakeRenderPDODecl(ERenderPDOPayloadKind::Mesh, "main", iCAX::PDO::kDirectionNil, 16), std::invalid_argument);
    EXPECT_THROW(MakeRenderPDODecl(ERenderPDOPayloadKind::Mesh, "main", iCAX::PDO::kDirection2External, 0), std::invalid_argument);
}

TEST(RenderPDOValidationTest, MeshHeaderValidatesOffsetsAndTriangleTopology)
{
    SRenderMeshPDOHeader _Header;
    const uint64_t _PositionsOffset = sizeof(SRenderMeshPDOHeader);
    const uint64_t _IndicesOffset = _PositionsOffset + sizeof(SFloat3) * 4;
    const uint64_t _PayloadSize = _IndicesOffset + sizeof(uint32_t) * 6;

    InitializeHeader(_Header, ERenderPDOPayloadKind::Mesh, _PayloadSize, 8);
    _Header.nGeometryID = 101;
    _Header.nTopology = static_cast<uint32_t>(ERenderTopology::TriangleList);
    _Header.nVertexCount = 4;
    _Header.nIndexCount = 6;
    _Header.nPositionsOffset = _PositionsOffset;
    _Header.nIndicesOffset = _IndicesOffset;

    std::string _Error;
    EXPECT_TRUE(ValidateMeshPDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_TRUE(_Error.empty());

    _Header.nFlags = kMeshFlagHasNormals;
    EXPECT_FALSE(ValidateMeshPDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());

    _Header.nFlags = 0;
    _Header.nIndexCount = 5;
    EXPECT_FALSE(ValidateMeshPDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());
}

TEST(RenderPDOValidationTest, PolylineHeaderValidatesPointAndRangeBlocks)
{
    SRenderPolylinePDOHeader _Header;
    const uint64_t _PointsOffset = sizeof(SRenderPolylinePDOHeader);
    const uint64_t _RangesOffset = _PointsOffset + sizeof(SFloat3) * 3;
    const uint64_t _PayloadSize = _RangesOffset + sizeof(SRenderPolylineRangeData) * 1;

    InitializeHeader(_Header, ERenderPDOPayloadKind::Polyline, _PayloadSize, 3);
    _Header.nGeometryID = 202;
    _Header.nPointCount = 3;
    _Header.nRangeCount = 1;
    _Header.nPointsOffset = _PointsOffset;
    _Header.nRangesOffset = _RangesOffset;

    EXPECT_TRUE(ValidatePolylinePDOHeader(_Header, _PayloadSize));

    _Header.nRangesOffset = _PayloadSize + 4;
    std::string _Error;
    EXPECT_FALSE(ValidatePolylinePDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());
}

TEST(RenderPDOValidationTest, ToolpathHeaderValidatesPointAndSpanBlocks)
{
    SRenderToolpathPDOHeader _Header;
    const uint64_t _PointsOffset = sizeof(SRenderToolpathPDOHeader);
    const uint64_t _SpansOffset = _PointsOffset + sizeof(SRenderToolpathPointData) * 2;
    const uint64_t _PayloadSize = _SpansOffset + sizeof(SRenderToolpathSpanData) * 1;

    InitializeHeader(_Header, ERenderPDOPayloadKind::Toolpath, _PayloadSize, 4);
    _Header.nGeometryID = 303;
    _Header.nPointCount = 2;
    _Header.nSpanCount = 1;
    _Header.nPointsOffset = _PointsOffset;
    _Header.nSpansOffset = _SpansOffset;

    EXPECT_TRUE(ValidateToolpathPDOHeader(_Header, _PayloadSize));

    _Header.Header.nDataVersion = 0;
    std::string _Error;
    EXPECT_FALSE(ValidateToolpathPDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());
}

TEST(RenderPDOValidationTest, InstanceListAllowsEmptyListAndOptionalStyles)
{
    SRenderInstanceListPDOHeader _Empty;
    InitializeHeader(_Empty, ERenderPDOPayloadKind::InstanceList, sizeof(SRenderInstanceListPDOHeader), 5);
    EXPECT_TRUE(ValidateInstanceListPDOHeader(_Empty, sizeof(SRenderInstanceListPDOHeader)));

    SRenderInstanceListPDOHeader _Header;
    const uint64_t _InstancesOffset = sizeof(SRenderInstanceListPDOHeader);
    const uint64_t _StylesOffset = _InstancesOffset + sizeof(SRenderInstanceData) * 2;
    const uint64_t _PayloadSize = _StylesOffset + sizeof(SRenderStyleData);

    InitializeHeader(_Header, ERenderPDOPayloadKind::InstanceList, _PayloadSize, 6);
    _Header.nInstanceCount = 2;
    _Header.nStyleCount = 1;
    _Header.nInstancesOffset = _InstancesOffset;
    _Header.nStylesOffset = _StylesOffset;

    EXPECT_TRUE(ValidateInstanceListPDOHeader(_Header, _PayloadSize));

    _Header.nStylesOffset = sizeof(SRenderInstanceListPDOHeader) - 1;
    std::string _Error;
    EXPECT_FALSE(ValidateInstanceListPDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());
}

TEST(RenderPDOValidationTest, ViewStateHeaderValidatesViewArrayAndActiveIndex)
{
    SRenderViewStatePDOHeader _Header;
    const uint64_t _ViewsOffset = sizeof(SRenderViewStatePDOHeader);
    const uint64_t _PayloadSize = _ViewsOffset + sizeof(SRenderViewStateData) * 2;

    InitializeHeader(_Header, ERenderPDOPayloadKind::ViewState, _PayloadSize, 7);
    _Header.nViewCount = 2;
    _Header.nActiveViewIndex = 1;
    _Header.nViewsOffset = _ViewsOffset;

    EXPECT_TRUE(ValidateViewStatePDOHeader(_Header, _PayloadSize));

    _Header.nActiveViewIndex = 2;
    std::string _Error;
    EXPECT_FALSE(ValidateViewStatePDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());

    _Header.nActiveViewIndex = 1;
    _Header.nViewsOffset = _PayloadSize + 1;
    EXPECT_FALSE(ValidateViewStatePDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());
}
