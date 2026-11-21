#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class StockLogic final {
  detail::Scene& scene;
public:
  explicit StockLogic(detail::Scene& value) : scene(value) {}
  Map Import(const Map& request) {
    detail::CheckExpected(scene, request);
    return detail::ImportObject(scene, "stocks", request);
  }
  Map CreateRectangle(const Map& request) {
    detail::CheckExpected(scene, request);
    auto rectangle = request; rectangle["primitive"] = Text("rect");
    rectangle["points"] = Array{iCAX::Data::Variant(Array{0., 0.})};
    auto geometry = detail::Geometry(scene)->CreateGeometry(rectangle);
    const auto curve = geometry.at("curves").To<Array>()[0].To<Map>();
    geometry["outer"] = curve.at("points"); geometry["holes"] = Array{};
    geometry["holeCount"] = 0ull;
    return detail::CreateObject(scene, "stocks", request, geometry);
  }
};
class CPlaneStockSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneStockSDO() : CSDO("PlaneStock") {
    ExposeMethod("Import", &detail::Dispatch<StockLogic, &StockLogic::Import>);
    ExposeMethod("CreateRectangle", &detail::Dispatch<StockLogic, &StockLogic::CreateRectangle>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneStockSDO)
} // namespace iCAX::PlaneCAM
