#include "PlaneSDODispatch.h"
namespace iCAX::PlaneCAM {
namespace {
class MachiningLogic final {
  detail::Scene& scene;
public:
  explicit MachiningLogic(detail::Scene& value) : scene(value) {}
  Map LoadPart(const Map& request) { return detail::LoadMachiningPart(scene, request); }
  Map LoadResult(const Map& request) { return detail::LoadMachiningResult(scene, request); }
};
class CPlaneMachiningSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneMachiningSDO() : CSDO("PlaneMachining") {
    ExposeMethod("LoadPart", &detail::Dispatch<MachiningLogic, &MachiningLogic::LoadPart>);
    ExposeMethod("LoadResult", &detail::Dispatch<MachiningLogic, &MachiningLogic::LoadResult>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneMachiningSDO)
} // namespace iCAX::PlaneCAM
