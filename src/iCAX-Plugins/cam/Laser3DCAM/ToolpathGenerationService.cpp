#include "pch.h"
#include "ToolpathGenerationService.h"

#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "ProjectContext/IProjectContext.h"
#include "Services/ServicesHelper.h"
#include "ToolpathComponents.h"

#include <stdexcept>

namespace iCAX
{
    namespace CAM
    {
        class CToolpathGenerationService final : public IToolpathGenerationService
        {
            AUTO_REGIST_SERVICE(iCAX::CAM::IToolpathGenerationService, CToolpathGenerationService)

        public:
            CToolpathGenerationService() = default;
            ~CToolpathGenerationService() override = default;

            void OnLoad() override
            {
            }

            void OnUnload() override
            {
            }

            SGeneratedToolpath Generate(
                IN iCAX::Project::IProjectContext& Project_,
                IN const SToolpathGenerationRequest& Request_) override
            {
                if (Request_.WorkpieceEntityID.is_nil())
                {
                    throw std::invalid_argument("Toolpath generation requires WorkpieceEntityID");
                }
                if (Request_.Feature.Kind.empty())
                {
                    throw std::invalid_argument("Toolpath generation requires feature kind");
                }

                auto _pWorkpieceEntity = Project_.Database().GetEntity(Request_.WorkpieceEntityID);
                if (!_pWorkpieceEntity || !_pWorkpieceEntity->GetComponent<CLaserWorkpieceComponent>())
                {
                    throw std::invalid_argument("Toolpath generation workpiece does not exist");
                }

                SGeneratedToolpath _Toolpath;
                _Toolpath.Name = Request_.Feature.Kind + " " + std::to_string(Request_.Feature.nCandidateID);
                _Toolpath.Operation = Request_.Feature.Kind;
                _Toolpath.Role = Request_.Feature.Role;
                _Toolpath.CurveSegments = Request_.Feature.PreviewCurves;

                _Toolpath.SourceFeature["kind"] = Request_.Feature.Kind;
                _Toolpath.SourceFeature["role"] = Request_.Feature.Role;
                _Toolpath.SourceFeature["candidateId"] = Request_.Feature.nCandidateID;
                _Toolpath.SourceFeature["parameters"] = Request_.Feature.Parameters;

                if (_Toolpath.CurveSegments.empty())
                {
                    iCAX::Data::ObjectMap _Diagnostic;
                    _Diagnostic["level"] = std::string("warning");
                    _Diagnostic["message"] = std::string("Toolpath generation has no preview curve; algorithm must create curve segments later");
                    _Toolpath.Diagnostics.emplace_back(_Diagnostic);
                }

                return _Toolpath;
            }
        };
    }
}
