#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class ObjectLogic final {
  detail::Scene& scene;
public:
  explicit ObjectLogic(detail::Scene& value) : scene(value) {}
  Map Update(const Map& request) {
    detail::CheckExpected(scene, request);
    return detail::UpdateObject(scene, request, false);
  }
  Map Remove(const Map& request) {
    detail::CheckExpected(scene, request);
    return detail::UpdateObject(scene, request, true);
  }
};
class CPlaneObjectSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneObjectSDO() : CSDO("PlaneObject") {
    ExposeMethod("Update", &detail::Dispatch<ObjectLogic, &ObjectLogic::Update>);
    ExposeMethod("Remove", &detail::Dispatch<ObjectLogic, &ObjectLogic::Remove>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneObjectSDO)
} // namespace iCAX::PlaneCAM
