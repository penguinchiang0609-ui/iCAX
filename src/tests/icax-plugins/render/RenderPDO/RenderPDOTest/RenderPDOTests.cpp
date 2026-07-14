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

    SRenderID MakeID(IN uint16_t nValue_)
    {
        SRenderID _ID;
        _ID.Bytes[0] = static_cast<uint8_t>(nValue_ & 0xFFu);
        _ID.Bytes[1] = static_cast<uint8_t>((nValue_ >> 8) & 0xFFu);
        return _ID;
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
    EXPECT_EQ(104u, sizeof(SRenderMeshPDOHeader));
    EXPECT_TRUE(std::is_standard_layout_v<SRenderPolylinePDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderPolylinePDOHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderToolpathPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderToolpathPDOHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<SRenderObjectPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderObjectPDOHeader>);
    EXPECT_EQ(96u, sizeof(SRenderObjectPDOHeader));
    EXPECT_TRUE(std::is_standard_layout_v<SRenderCameraPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SRenderCameraPDOHeader>);
    EXPECT_EQ(72u, sizeof(SRenderCameraPDOHeader));
    EXPECT_TRUE(std::is_standard_layout_v<STransformPDOHeader>);
    EXPECT_TRUE(std::is_trivially_copyable_v<STransformPDOHeader>);
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
    EXPECT_STREQ("render.object", GetRenderPDOPayloadTypeName(ERenderPDOPayloadKind::Object));
    EXPECT_STREQ("render.camera", GetRenderPDOPayloadTypeName(ERenderPDOPayloadKind::Camera));
    EXPECT_STREQ("render.transform", GetRenderPDOPayloadTypeName(ERenderPDOPayloadKind::Transform));

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
    _Header.nGeometryID = MakeID(101);
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

    const uint64_t _TextureCoordinatesOffset = _PayloadSize;
    const uint64_t _PayloadSizeWithTextureCoordinates = _TextureCoordinatesOffset + sizeof(SFloat2) * 4;
    _Header.nFlags = kMeshFlagHasTextureCoordinates;
    _Header.nTextureCoordinatesOffset = _TextureCoordinatesOffset;
    _Header.Header.nPayloadSize = _PayloadSizeWithTextureCoordinates;
    _Error.clear();
    EXPECT_TRUE(ValidateMeshPDOHeader(_Header, _PayloadSizeWithTextureCoordinates, &_Error));
    EXPECT_TRUE(_Error.empty());

    _Header.nTextureCoordinatesOffset = 0;
    EXPECT_FALSE(ValidateMeshPDOHeader(_Header, _PayloadSizeWithTextureCoordinates, &_Error));
    EXPECT_FALSE(_Error.empty());

    _Header.nFlags = 0;
    _Header.nTextureCoordinatesOffset = 0;
    _Header.Header.nPayloadSize = _PayloadSize;
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
    _Header.nGeometryID = MakeID(202);
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
    _Header.nGeometryID = MakeID(303);
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

TEST(RenderPDOValidationTest, ObjectHeaderValidatesOneEntityObject)
{
    SRenderObjectPDOHeader _Header;
    const uint64_t _PayloadSize = sizeof(SRenderObjectPDOHeader);

    InitializeHeader(_Header, ERenderPDOPayloadKind::Object, _PayloadSize, 6);
    _Header.nObjectID = MakeID(1);
    _Header.nGeometryID = MakeID(2);
    _Header.nMaterialID = MakeID(3);
    _Header.nGeometryKind = static_cast<uint32_t>(ERenderGeometryKind::Mesh);
    _Header.nRenderClass = static_cast<uint32_t>(ERenderClass::Model);

    EXPECT_TRUE(ValidateObjectPDOHeader(_Header, _PayloadSize));

    _Header.nObjectID = {};
    std::string _Error;
    EXPECT_FALSE(ValidateObjectPDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());

    _Header.nObjectID = MakeID(1);
    _Header.Header.nPayloadSize = _PayloadSize + 1;
    _Error.clear();
    EXPECT_FALSE(ValidateObjectPDOHeader(_Header, _PayloadSize + 1, &_Error));
    EXPECT_FALSE(_Error.empty());
}

TEST(RenderPDOValidationTest, CameraHeaderValidatesOneEntityCamera)
{
    SRenderCameraPDOHeader _Header;
    const uint64_t _PayloadSize = sizeof(SRenderCameraPDOHeader);

    InitializeHeader(_Header, ERenderPDOPayloadKind::Camera, _PayloadSize, 7);
    _Header.nCameraID = MakeID(1);

    EXPECT_TRUE(ValidateCameraPDOHeader(_Header, _PayloadSize));

    _Header.nCameraID = {};
    std::string _Error;
    EXPECT_FALSE(ValidateCameraPDOHeader(_Header, _PayloadSize, &_Error));
    EXPECT_FALSE(_Error.empty());

    _Header.nCameraID = MakeID(1);
    _Header.Header.nPayloadSize = _PayloadSize + 1;
    _Error.clear();
    EXPECT_FALSE(ValidateCameraPDOHeader(_Header, _PayloadSize + 1, &_Error));
    EXPECT_FALSE(_Error.empty());
}

TEST(RenderPDOValidationTest, TransformHeaderValidatesTransformID)
{
    STransformPDOHeader _Header;
    InitializeHeader(_Header, ERenderPDOPayloadKind::Transform, sizeof(STransformPDOHeader), 8);
    _Header.nTransformID = MakeID(101);

    EXPECT_TRUE(ValidateTransformPDOHeader(_Header, sizeof(STransformPDOHeader)));

    _Header.nTransformID = {};
    std::string _Error;
    EXPECT_FALSE(ValidateTransformPDOHeader(_Header, sizeof(STransformPDOHeader), &_Error));
    EXPECT_FALSE(_Error.empty());
}
