#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class PartLogic final {
  detail::Scene& scene;
public:
  explicit PartLogic(detail::Scene& value) : scene(value) {}
  Map Import(const Map& request) {
    detail::CheckExpected(scene, request);
    return detail::ImportObject(scene, "parts", request);
  }
};
class CPlanePartSDO final : public iCAX::Interaction::CSDO {
public:
  CPlanePartSDO() : CSDO("PlanePart") {
    ExposeMethod("Import", &detail::Dispatch<PartLogic, &PartLogic::Import>);
  }
};
}
ICAX_REGISTER_SDO(CPlanePartSDO)
} // namespace iCAX::PlaneCAM
