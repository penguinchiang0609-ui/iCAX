#include "pch.h"
#include "Facades.h"
#include "CameraViewFacadeImplement.h"
#include "FacadeSupport.h"

#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"

namespace
{
    class CCameraViewFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CCameraViewFacade()
            : CFacade("CameraView")
        {
            ExposeMethod("Fit", &iCAX::CAM::Facades::HandleFitCameraView);
            ExposeMethod("SetStandard", &iCAX::CAM::Facades::HandleSetStandardCameraView);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessFacadeType<CCameraViewFacade>);
}

ICAX_REGISTER_FACADE(CCameraViewFacade)

namespace iCAX
{
namespace CAM
{
namespace Facades
{
using namespace Internal;

iCAX::Interaction::CFacadeResult HandleFitCameraView(
    IN const iCAX::Interaction::CFacadeCall &,
    IN iCAX::Application::IApplicationContext &,
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

iCAX::Interaction::CFacadeResult HandleSetStandardCameraView(
    IN const iCAX::Interaction::CFacadeCall& Request_,
    IN iCAX::Application::IApplicationContext&,
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
} // namespace Facades
} // namespace CAM
} // namespace iCAX
