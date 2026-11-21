#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class NestingLogic final {
  detail::Scene& scene;
public:
  explicit NestingLogic(detail::Scene& value) : scene(value) {}
  Map SetParameters(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request);
    auto parameters = Parameters(*Project(scene));
    for (const auto* key : {"partGap", "stockMargin", "rotationStepDeg", "timeLimitSeconds"})
      if (request.contains(key)) parameters[key] = Number(request, key);
    if (request.contains("allowMirror")) parameters["allowMirror"] = request.at("allowMirror").To<bool>();
    if (Number(parameters, "partGap") < 0 || Number(parameters, "stockMargin") < 0 ||
        Number(parameters, "rotationStepDeg") < 1 || Number(parameters, "rotationStepDeg") > 360 ||
        Number(parameters, "timeLimitSeconds") <= 0 || Number(parameters, "timeLimitSeconds") > 120)
      throw std::invalid_argument("PLANE_INVALID_NESTING_PARAMETERS");
    Mutate(scene, "修改排样参数", [&](auto& transaction) {
      transaction.ModifyComponent(scene.Database().GetMetaEntity()->GetID(), CPlaneProjectComponent::S_ClassName,
        {{"PartGap", parameters.at("partGap")}, {"StockMargin", parameters.at("stockMargin")},
         {"RotationStepDeg", parameters.at("rotationStepDeg")}, {"TimeLimitSeconds", parameters.at("timeLimitSeconds")},
         {"AllowMirror", parameters.at("allowMirror")}});
      Bump(scene, transaction, true);
    });
    return Receipt(scene);
  }
  Map Run(const Map& request) { detail::CheckExpected(scene, request); return detail::Run(scene); }
  Map Cancel(const Map& request) { detail::CheckExpected(scene, request); return detail::CancelNesting(scene, request); }
  Map ReadTask(const Map& request) {
    const auto id = detail::Id(request, "taskId");
    return detail::TaskState(scene, id, *detail::Require<CPlaneTaskComponent>(scene, id));
  }
  Map ReadReport(const Map& request) {
    const auto id = detail::Id(request, "resultId");
    return detail::ResultState(scene, id, *detail::Require<CPlaneResultComponent>(scene, id));
  }
};
class CPlaneNestingSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneNestingSDO() : CSDO("PlaneNesting") {
    ExposeMethod("SetParameters", &detail::Dispatch<NestingLogic, &NestingLogic::SetParameters>);
    ExposeMethod("Run", &detail::Dispatch<NestingLogic, &NestingLogic::Run>);
    ExposeMethod("Cancel", &detail::Dispatch<NestingLogic, &NestingLogic::Cancel>);
    ExposeMethod("ReadTask", &detail::Dispatch<NestingLogic, &NestingLogic::ReadTask>);
    ExposeMethod("ReadReport", &detail::Dispatch<NestingLogic, &NestingLogic::ReadReport>);
  }
};
class CPlaneLayoutSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneLayoutSDO() : CSDO("PlaneLayout") {
    ExposeMethod("ReadReport", &detail::Dispatch<NestingLogic, &NestingLogic::ReadReport>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneNestingSDO)
ICAX_REGISTER_SDO(CPlaneLayoutSDO)
} // namespace iCAX::PlaneCAM
