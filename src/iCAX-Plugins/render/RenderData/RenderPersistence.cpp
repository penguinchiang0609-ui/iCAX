#include "pch.h"
#include "RenderDataTypes.h"
#include "Resources/ResourcePersistenceRegistrationCatalog.h"
#include <bit>
#include <type_traits>

namespace
{
    // Version 1 uses little-endian scalar fields, never native struct layouts.
    struct Writer
    {
        std::vector<std::uint8_t> Bytes;
        template<class T> void Field(T& value)
        {
            static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
            auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
            if constexpr (std::endian::native == std::endian::big) std::reverse(bytes.begin(), bytes.end());
            Bytes.insert(Bytes.end(), bytes.begin(), bytes.end());
        }
        void Field(std::string& value) { auto size = std::uint64_t(value.size()); Field(size); Bytes.insert(Bytes.end(), value.begin(), value.end()); }
        template<class T> void Field(std::vector<T>& values) { auto size = std::uint64_t(values.size()); Field(size); for (auto& v : values) Field(v); }
        void Field(iCAX::Render::SFloat3& v) { Field(v.x); Field(v.y); Field(v.z); }
        void Field(iCAX::Render::SFloat2& v) { Field(v.x); Field(v.y); }
        void Field(iCAX::Data::uuid& v) { for (auto b : v.as_bytes()) { auto byte = std::to_integer<std::uint8_t>(b); Field(byte); } }
    };
    struct Reader
    {
        std::span<const std::uint8_t> Bytes;
        void Require(std::uint64_t size) { if (size > Bytes.size()) throw std::runtime_error("Truncated render resource"); }
        template<class T> void Field(T& value)
        {
            static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
            Require(sizeof(T)); std::array<std::uint8_t, sizeof(T)> bytes;
            std::copy_n(Bytes.begin(), sizeof(T), bytes.begin()); Bytes = Bytes.subspan(sizeof(T));
            if constexpr (std::endian::native == std::endian::big) std::reverse(bytes.begin(), bytes.end());
            value = std::bit_cast<T>(bytes);
        }
        void Field(std::string& value) { std::uint64_t size; Field(size); Require(size); value.assign(reinterpret_cast<const char*>(Bytes.data()), size); Bytes = Bytes.subspan(size); }
        template<class T> void Field(std::vector<T>& values) { std::uint64_t size; Field(size); if (size > Bytes.size() / sizeof(T)) throw std::runtime_error("Invalid render resource count"); values.resize(size); for (auto& v : values) Field(v); }
        void Field(iCAX::Render::SFloat3& v) { Field(v.x); Field(v.y); Field(v.z); }
        void Field(iCAX::Render::SFloat2& v) { Field(v.x); Field(v.y); }
        void Field(iCAX::Data::uuid& v) { std::array<std::uint8_t, 16> bytes; for (auto& b : bytes) Field(b); v = iCAX::Data::uuid(bytes); }
    };
    template<class Archive> void Fields(Archive& a, iCAX::Render::SRenderMeshData& v)
    {
        a.Field(v.nGeometryID); a.Field(v.nDataVersion); a.Field(v.eTopology); a.Field(v.nFlags);
        a.Field(v.Positions); a.Field(v.Normals); a.Field(v.TextureCoordinates); a.Field(v.VertexColorsRGBA); a.Field(v.Indices);
    }
    template<class Archive> void Fields(Archive& a, iCAX::Render::SRenderMaterialData& v)
    {
        a.Field(v.nDataVersion); a.Field(v.nColorRGBA); a.Field(v.nAmbientRGBA); a.Field(v.nSpecularRGBA); a.Field(v.nEmissiveRGBA);
        a.Field(v.nLineWidth); a.Field(v.nFlags); a.Field(v.nMaterialFlags); a.Field(v.strBaseColorTextureResourceID);
    }
    template<class Archive> void Fields(Archive& a, iCAX::Render::SRenderTextureData& v)
    {
        a.Field(v.nDataVersion); a.Field(v.strImageResourceID); a.Field(v.strSourceURI); a.Field(v.nFlags);
    }
    template<class T, std::uint32_t Kind> iCAX::Resource::CResourceVersionCodec Codec()
    {
        iCAX::Resource::CResourceVersionCodec codec;
        codec.Serialize = [](const std::shared_ptr<void>& value)->std::optional<std::vector<std::uint8_t>> {
            if (!value) return std::nullopt;
            Writer writer; std::uint32_t magic = 0x52414349, version = 1, kind = Kind;
            writer.Field(magic); writer.Field(version); writer.Field(kind); Fields(writer, *std::static_pointer_cast<T>(value)); return std::move(writer.Bytes);
        };
        codec.Deserialize = [](std::span<const std::uint8_t> bytes)->std::shared_ptr<void> {
            Reader reader{bytes}; std::uint32_t magic, version, kind; reader.Field(magic); reader.Field(version); reader.Field(kind);
            if (magic != 0x52414349 || version != 1 || kind != Kind) throw std::runtime_error("Invalid render resource header");
            auto value = std::make_shared<T>(); Fields(reader, *value);
            if (!reader.Bytes.empty()) throw std::runtime_error("Trailing render resource data"); return value;
        };
        return codec;
    }
    auto MeshCodec() { return Codec<iCAX::Render::SRenderMeshData, 1>(); }
    auto MaterialCodec() { return Codec<iCAX::Render::SRenderMaterialData, 2>(); }
    auto TextureCodec() { return Codec<iCAX::Render::SRenderTextureData, 3>(); }
    ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC("render.mesh", iCAX::Render::SRenderMeshData, MeshCodec)
    ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC("render.material", iCAX::Render::SRenderMaterialData, MaterialCodec)
    ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC("render.texture", iCAX::Render::SRenderTextureData, TextureCodec)
}
