#include "pch.h"
#include "IntentGeometryResources.h"
#include "GeometryData/BRepPersistence.h"
#include "Resources/ResourcePersistenceRegistrationCatalog.h"

namespace
{
    constexpr std::array<std::uint8_t,8> Magic{'I','C','A','X','I','C',1,0};
    iCAX::Resource::CResourceVersionCodec CurveCodec()
    {
        using Curve=iCAX::CAM::Intent::CIntentCurveResource;
        using namespace iCAX::GeometryData::Persistence;
        iCAX::Resource::CResourceVersionCodec codec;
        codec.Serialize=[](const std::shared_ptr<void>& value)->std::optional<std::vector<std::uint8_t>>
        {
            if(!value)return std::nullopt;const auto& curve=*std::static_pointer_cast<Curve>(value);Writer w;
            w.Write(Magic);w.Write(curve.Version);w.Write(curve.HasCurve2);w.Write(curve.HasCurve3);w.Write(curve.Parameterization);
            w.Write(curve.Curve2);w.Write(curve.Curve3);return std::move(w.Bytes);
        };
        codec.Deserialize=[](std::span<const std::uint8_t> bytes)->std::shared_ptr<void>
        {
            Reader r(bytes);std::array<std::uint8_t,8> magic;r.Read(magic);Check(magic==Magic);
            auto curve=std::make_shared<Curve>();r.Read(curve->Version);r.Read(curve->HasCurve2);r.Read(curve->HasCurve3);r.Read(curve->Parameterization);
            r.Read(curve->Curve2);r.Read(curve->Curve3);Check(r.End());return curve;
        };
        return codec;
    }
    ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC("cam.intent.curve",iCAX::CAM::Intent::CIntentCurveResource,CurveCodec)
}
