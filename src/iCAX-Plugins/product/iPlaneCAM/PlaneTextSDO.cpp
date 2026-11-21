#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class TextLogic final {
  detail::Scene& scene;
public:
  explicit TextLogic(detail::Scene& value) : scene(value) {}
  Map Create(const Map& request) { return detail::CreateText(scene, request, false); }
  Map Update(const Map& request) { return detail::CreateText(scene, request, true); }
};
class CPlaneTextSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneTextSDO() : CSDO("PlaneText") {
    ExposeMethod("Create", &detail::Dispatch<TextLogic, &TextLogic::Create>);
    ExposeMethod("Update", &detail::Dispatch<TextLogic, &TextLogic::Update>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneTextSDO)
} // namespace iCAX::PlaneCAM
