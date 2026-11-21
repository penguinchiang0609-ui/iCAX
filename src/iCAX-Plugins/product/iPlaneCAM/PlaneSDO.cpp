#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class SceneLogic final {
  detail::Scene& scene;
public:
  explicit SceneLogic(detail::Scene& value) : scene(value) {}
  Map Read(const Map&) { return detail::ReadState(scene); }
};
class CPlaneSceneSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneSceneSDO() : CSDO("PlaneScene") {
    ExposeMethod("ReadState", &detail::Dispatch<SceneLogic, &SceneLogic::Read>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneSceneSDO)
} // namespace iCAX::PlaneCAM
