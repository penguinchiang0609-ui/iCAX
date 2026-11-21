#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class ImageLogic final {
  detail::Scene& scene;
public:
  explicit ImageLogic(detail::Scene& value) : scene(value) {}
  Map Import(const Map& request) { return detail::ImportImage(scene, request); }
  Map Update(const Map& request) { return detail::UpdateImage(scene, request); }
  Map Vectorize(const Map& request) { return detail::VectorizeImage(scene, request); }
};
class CPlaneImageSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneImageSDO() : CSDO("PlaneImage") {
    ExposeMethod("Import", &detail::Dispatch<ImageLogic, &ImageLogic::Import>);
    ExposeMethod("Update", &detail::Dispatch<ImageLogic, &ImageLogic::Update>);
    ExposeMethod("Vectorize", &detail::Dispatch<ImageLogic, &ImageLogic::Vectorize>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneImageSDO)
} // namespace iCAX::PlaneCAM
