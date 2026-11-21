#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class DrawingLogic final {
  detail::Scene& scene;
public:
  explicit DrawingLogic(detail::Scene& value) : scene(value) {}
  Map Import(const Map& request) {
    detail::CheckExpected(scene, request);
    return detail::ImportObject(scene, "drawing", request);
  }
  Map Create(const Map& request) {
    detail::CheckExpected(scene, request);
    return detail::CreateObject(scene, "drawing", request, detail::Geometry(scene)->CreateGeometry(request));
  }
  Map Edit(const Map& request) { return detail::EditCurveObject(scene, request, "drawing"); }
  Map Read(const Map& request) { return detail::ReadCurveGeometry(scene, request, "drawing"); }
};
class CPlaneDrawingSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneDrawingSDO() : CSDO("PlaneDrawing") {
    ExposeMethod("Import", &detail::Dispatch<DrawingLogic, &DrawingLogic::Import>);
    ExposeMethod("Create", &detail::Dispatch<DrawingLogic, &DrawingLogic::Create>);
    ExposeMethod("Edit", &detail::Dispatch<DrawingLogic, &DrawingLogic::Edit>);
    ExposeMethod("ReadGeometry", &detail::Dispatch<DrawingLogic, &DrawingLogic::Read>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneDrawingSDO)
} // namespace iCAX::PlaneCAM
