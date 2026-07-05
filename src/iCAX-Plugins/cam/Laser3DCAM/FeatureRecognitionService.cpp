#include "pch.h"
#include "FeatureRecognitionService.h"

#include "ProjectContext/IProjectContext.h"
#include "Resources/ResourceLibrary.h"
#include "Services/ServicesHelper.h"

#include <stdexcept>

namespace iCAX
{
    namespace CAM
    {
        class CFeatureRecognitionService final : public IFeatureRecognitionService
        {
            AUTO_REGIST_SERVICE(iCAX::CAM::IFeatureRecognitionService, CFeatureRecognitionService)

        public:
            CFeatureRecognitionService() = default;
            ~CFeatureRecognitionService() override = default;

            void OnLoad() override
            {
            }

            void OnUnload() override
            {
            }

            SFeatureRecognitionResult Recognize(
                IN iCAX::Project::IProjectContext& Project_,
                IN const SFeatureRecognitionRequest& Request_) override
            {
                if (Request_.BRep.BRepResourceID.empty())
                {
                    throw std::invalid_argument("Feature recognition requires BRepResourceID");
                }
                if (Request_.Definitions.empty())
                {
                    throw std::invalid_argument("Feature recognition requires at least one feature definition");
                }

                const auto _BRepVersion = Project_.Resources().GetVersion(Request_.BRep.BRepResourceID);
                if (_BRepVersion == 0)
                {
                    throw std::invalid_argument("Feature recognition BRep resource does not exist");
                }
                if (Request_.BRep.nExpectedDataVersion != 0 && Request_.BRep.nExpectedDataVersion != _BRepVersion)
                {
                    throw std::invalid_argument("Feature recognition BRep resource version mismatch");
                }

                SFeatureRecognitionResult _Result;
                _Result.nDataVersion = _BRepVersion;

                iCAX::Data::ObjectMap _Diagnostic;
                _Diagnostic["level"] = std::string("info");
                _Diagnostic["message"] = std::string("Feature recognition algorithm is not implemented yet");
                _Result.Diagnostics.emplace_back(_Diagnostic);
                return _Result;
            }
        };
    }
}
