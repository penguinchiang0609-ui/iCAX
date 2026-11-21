#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "PlaneCAM.h"
#include "PlaneComponents.h"
namespace {
class CPlaneSceneBehaviour final : public iCAX::Behaviour::CBehaviourBase {
  AUTO_REGIST_BEHAVIOUR(CPlaneSceneBehaviour)
public:
  std::string GetComponentClass() const override {
    return iCAX::PlaneCAM::CPlaneSceneComponent::S_ClassName;
  }
  iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override {
    iCAX::Behaviour::CBehaviourSchedule s;
    s.ExecutionOrder = -9000;
    return s;
  }

protected:
  void OnAwake(iCAX::Database::CComponentBase &,
               const iCAX::Application::IApplicationContext &,
               const iCAX::Product::IProductContext &,
               iCAX::Project::IProjectContext &,
               iCAX::Project::ISceneContext &scene) override {
    iCAX::PlaneCAM::InitializeScene(scene);
  }
  void OnStart(iCAX::Database::CComponentBase &component,
               const iCAX::Application::IApplicationContext &,
               const iCAX::Product::IProductContext &,
               iCAX::Project::IProjectContext &,
               iCAX::Project::ISceneContext &scene) override {
    if (!static_cast<iCAX::PlaneCAM::CPlaneSceneComponent &>(component).Runtime)
      iCAX::PlaneCAM::InitializeScene(scene);
  }
  void OnEnable(iCAX::Database::CComponentBase &component,
                const iCAX::Application::IApplicationContext &,
                const iCAX::Product::IProductContext &,
                iCAX::Project::IProjectContext &,
                iCAX::Project::ISceneContext &scene) override {
    if (!static_cast<iCAX::PlaneCAM::CPlaneSceneComponent &>(component).Runtime)
      iCAX::PlaneCAM::InitializeScene(scene);
  }
  void OnUpdate(iCAX::Database::CComponentBase &component,
                const iCAX::Application::IApplicationContext &,
                const iCAX::Product::IProductContext &,
                iCAX::Project::IProjectContext &,
                iCAX::Project::ISceneContext &scene, const double &,
                const double &) override {
    if (scene.IsStopRequested())
      return;
    if (!static_cast<iCAX::PlaneCAM::CPlaneSceneComponent &>(component).Runtime)
      iCAX::PlaneCAM::InitializeScene(scene);
    iCAX::PlaneCAM::PumpTasks(scene);
  }
  void OnDisable(iCAX::Database::CComponentBase &component,
                 const iCAX::Application::IApplicationContext &,
                 const iCAX::Product::IProductContext &,
                 iCAX::Project::IProjectContext &,
                 iCAX::Project::ISceneContext &scene) override {
    iCAX::PlaneCAM::StopTasks(
        scene,
        static_cast<iCAX::PlaneCAM::CPlaneSceneComponent &>(component).Runtime);
  }
  void OnDestroyImmediate(iCAX::Database::CComponentBase &component,
                          const iCAX::Application::IApplicationContext &,
                          const iCAX::Product::IProductContext &,
                          iCAX::Project::IProjectContext &,
                          iCAX::Project::ISceneContext &scene) override {
    iCAX::PlaneCAM::StopTasks(
        scene,
        static_cast<iCAX::PlaneCAM::CPlaneSceneComponent &>(component).Runtime);
  }
};
} // namespace
