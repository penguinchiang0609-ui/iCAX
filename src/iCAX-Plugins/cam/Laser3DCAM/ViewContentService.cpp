#include "pch.h"

#include "MachineInstanceComponents.h"
#include "ViewDefinitions.h"
#include "WorkpieceComponents.h"

#include "RenderInteraction/RenderInteractionComponents.h"
#include "Services/ServicesHelper.h"
#include "View/ViewContentService.h"

namespace
{
    class CLaser3DCAMViewContentService final : public iCAX::View::CViewContentService
    {
        AUTO_REGIST_SERVICE(iCAX::View::IViewContentService, CLaser3DCAMViewContentService)

    public:
        CLaser3DCAMViewContentService()
        {
            using Where = iCAX::Database::CEntityWhereBuilder;
            const auto _AllRenderable = Where::Build(
                Where::Has<iCAX::RenderInteraction::CRenderInstanceComponent>());

            const bool _bRegistered =
                RegisterDefinition({
                    iCAX::CAM::Views::kMachine,
                    Where::Build(Where::All({
                        Where::Has<iCAX::CAM::CMachineElementComponent>(),
                        Where::Has<iCAX::RenderInteraction::CRenderInstanceComponent>(),
                    })),
                    nullptr,
                })
                && RegisterDefinition({
                    iCAX::CAM::Views::kWorkpiece,
                    Where::Build(Where::All({
                        Where::Has<iCAX::CAM::CWorkpieceComponent>(),
                        Where::Has<iCAX::RenderInteraction::CRenderInstanceComponent>(),
                    })),
                    nullptr,
                })
                && RegisterDefinition({
                    iCAX::CAM::Views::kMachining,
                    _AllRenderable,
                    nullptr,
                })
                && RegisterDefinition({
                    iCAX::CAM::Views::kGeneral,
                    _AllRenderable,
                    nullptr,
                });

            if (!_bRegistered)
            {
                throw std::logic_error("Laser3DCAM View definition is duplicated");
            }
        }
    };
}
