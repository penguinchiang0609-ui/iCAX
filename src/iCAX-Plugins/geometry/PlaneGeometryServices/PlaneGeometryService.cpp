#include "PlaneGeometryAlgorithms.h"
#include "Services/ServicesHelper.h"

namespace {
namespace G = iCAX::GeometryServices;
namespace A = G::PlaneGeometryDetail;
class Service final : public G::IPlaneGeometryService {
  AUTO_REGIST_SERVICE(G::IPlaneGeometryService, Service)
public:
  void OnLoad() override {}
  void OnUnload() override {}
  Map CreateGeometry(const Map &request) const override { return A::CreateGeometry(request); }
  Map BuildGeometry(const Array &curves, bool material, const std::function<bool()> &cancelled) const override { return A::BuildGeometry(curves, material, cancelled); }
  Map ImportGeometry(const iCAX::CAD::SCadExchangeDocument &source, bool material, const std::string &units) const override {
    return A::ImportGeometry(source, material, units);
  }
  Map TransformGeometry(Map geometry, const Map &request) const override { return A::TransformGeometry(std::move(geometry), request); }
  Array ArrayGeometry(const Map &geometry, const Map &request, const std::function<bool()> &cancelled) const override { return A::ArrayGeometry(geometry, request, cancelled); }
  Map DescribeDrawingClosure(const Map &geometry) const override { return A::DescribeDrawingClosure(geometry); }
  Array DescribeDrawingDimensions(const Map &geometry, const std::string &id) const override { return A::DescribeDrawingDimensions(geometry, id); }
  Map PlaceGeometry(Map geometry, const G::PlaneNesting::Placement &pose) const override { return A::PlaceGeometry(std::move(geometry), pose); }
};
}

extern "C" __declspec(dllexport) iCAX::GeometryServices::IPlaneGeometryService *CreatePlaneGeometryService() { return new Service; }
extern "C" __declspec(dllexport) void DestroyPlaneGeometryService(iCAX::GeometryServices::IPlaneGeometryService *service) { delete service; }
