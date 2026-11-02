#pragma once
#include "GeometryData/BRepPersistence.h"
#include "Resources/ResourcePersistenceRegistrationCatalog.h"

// Both mesh importers use exactly the same neutral resource codec. Registration
// is idempotent when STL and DAE are loaded by the same product.
namespace {
    struct TriangleMeshPersistenceRegistration {
        TriangleMeshPersistenceRegistration() {
            iCAX::Resource::CResourcePersistenceRegistrationCatalog::Register([](iCAX::Resource::CResourceLibrary& library) {
                using Mesh = iCAX::GeometryData::CTriangleMeshResource;
                iCAX::Resource::CResourceVersionCodec codec;
                codec.Serialize=[](const std::shared_ptr<void>& value)->std::optional<std::vector<std::uint8_t>> {
                    if(!value)return std::nullopt;
                    iCAX::GeometryData::Persistence::Writer writer;
                    writer.Write(std::array<std::uint8_t,8>{'I','C','A','X','T','M',1,0});
                    writer.Write(*std::static_pointer_cast<Mesh>(value));return std::move(writer.Bytes);
                };
                codec.Deserialize=[](std::span<const std::uint8_t> bytes)->std::shared_ptr<void> {
                    using namespace iCAX::GeometryData::Persistence;
                    Reader reader(bytes);std::array<std::uint8_t,8> magic;reader.Read(magic);
                    Check(magic==std::array<std::uint8_t,8>{'I','C','A','X','T','M',1,0});
                    auto mesh=std::make_shared<Mesh>();reader.Read(*mesh);Check(reader.End());return mesh;
                };
                library.RegisterPersistenceCodec<Mesh>(Mesh::kResourceTypeName,std::move(codec));
            },this);
        }
    };
    const TriangleMeshPersistenceRegistration triangleMeshPersistenceRegistration;
}
