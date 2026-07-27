#include "pch.h"
#include "SDO.h"
#include "CameraViewSDOImplement.h"
#include "SDOSupport.h"

#include "SDO/SDORegistrationCatalog.h"
#include "SDO/SDO.h"

namespace
{
    class CCameraViewSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CCameraViewSDO()
            : CSDO("CameraView")
        {
            ExposeMethod("Fit", &iCAX::CAM::SDO::HandleFitCameraView);
            ExposeMethod("SetStandard", &iCAX::CAM::SDO::HandleSetStandardCameraView);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CCameraViewSDO>);
}

ICAX_REGISTER_SDO(CCameraViewSDO)

namespace iCAX
{
namespace CAM
{
namespace SDO
{
using namespace Internal;

iCAX::Interaction::CInvocationResult HandleFitCameraView(
    IN const iCAX::Interaction::CInvocation &,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *pProjectContext_,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Project = _RequireProjectContext(pProjectContext_);
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _FitInfo = _FitActiveCameraToRenderableBounds(_Project, _Scene);
    ObjectMap _Payload;
    _Payload["fitView"] = _FitInfo;
    return _MakeResponse(Variant(_Payload));
}

iCAX::Interaction::CInvocationResult HandleSetStandardCameraView(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Project = _RequireProjectContext(pProjectContext_);
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _ViewName = _GetOptionalString(_Payload, "view", "iso");
    auto _ViewInfo = _SetActiveCameraToStandardView(_Project, _Scene, _ViewName);
    ObjectMap _Result;
    _Result["cameraView"] = _ViewInfo;
    return _MakeResponse(Variant(_Result));
}
} // namespace SDO
} // namespace CAM
} // namespace iCAX
