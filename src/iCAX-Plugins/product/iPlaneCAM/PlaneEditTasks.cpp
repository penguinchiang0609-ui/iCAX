#include "PlaneSDODispatch.h"
#include <map>
namespace iCAX::PlaneCAM {
struct EditWork final {
  ID Id;
  detail::GeometryTaskSpec Input;
  Text Status = "Running", Error;
  ID ResultID;
  Array ResultIDs;
  iCAX::Tasks::CancellationTokenSource Cancellation;
  iCAX::Tasks::Task<Map> Work;
};
struct EditTaskRuntime final {
  std::map<Text, std::shared_ptr<EditWork>> Tasks;
  bool Stopped = false;
  ~EditTaskRuntime() {
    for (const auto& [_, task] : Tasks) task->Cancellation.Cancel();
    for (const auto& [_, task] : Tasks) if (task->Work.Valid()) task->Work.Wait();
  }
};
namespace detail {
namespace {
void RetireFinishedWork(EditTaskRuntime& runtime) {
  for (auto it = runtime.Tasks.begin(); it != runtime.Tasks.end();) {
    if (it->second->Status == "Running") { ++it; continue; }
    it->second->Work = {};
    it = runtime.Tasks.erase(it);
  }
}
std::shared_ptr<EditTaskRuntime> EditRuntime(Scene& scene) {
  const auto component = Require<CPlaneSceneComponent>(scene, scene.Database().GetMetaEntity()->GetID());
  if (!component->EditRuntime) throw std::runtime_error("PLANE_SCENE_NOT_INITIALIZED");
  return component->EditRuntime;
}
Map EditTaskState(const ID& id, const CPlaneEditTaskComponent& task) {
  return {{"taskId", IdText(id)}, {"kind", task.GetKind()}, {"id", IdText(task.GetTargetID())},
    {"sketchId", task.GetSketchID().is_nil() ? Text() : IdText(task.GetSketchID())},
    {"status", task.GetStatus()}, {"error", task.GetError()},
    {"resultId", task.GetResultID().is_nil() ? Text() : IdText(task.GetResultID())},
    {"resultIds", task.GetResultObjectIDs()}};
}
void SaveState(Scene& scene, const EditWork& task) {
  if (!scene.Database().GetEntity(task.Id)) return;
  Mutate(scene, "更新几何任务状态", [&](auto& tx) {
    tx.ModifyComponent(task.Id, CPlaneEditTaskComponent::S_ClassName,
      {{"Status", task.Status}, {"Error", task.Error}, {"ResultID", task.ResultID}, {"ResultObjectIDs", task.ResultIDs}});
  }, true);
}
bool Matches(Scene& scene, const GeometryTaskSpec& input) {
  if (input.RequireAbsentTarget && scene.Database().GetEntity(input.TargetID)) return false;
  if (!input.LayerID.is_nil()) {
    const auto layer = Component<CPlaneLayerComponent>(scene.Database().GetEntity(input.LayerID));
    if (!layer || layer->GetLocked() || scene.Database().GetComponentVersion(input.LayerID,
        CPlaneLayerComponent::S_ClassName) != input.LayerVersion) return false;
  }
  for (const auto& value : input.Inputs) {
    const auto object = Component<CPlaneObjectComponent>(scene.Database().GetEntity(value.ObjectID));
    if (!object || object->GetGeometryRef() != value.GeometryRef || object->GetGeometryVersion() != value.GeometryVersion)
      return false;
    const auto layer = Component<CPlaneLayerComponent>(scene.Database().GetEntity(value.LayerID));
    if (!layer || layer->GetLocked() || object->GetLayerID() != value.LayerID ||
        scene.Database().GetComponentVersion(value.LayerID, CPlaneLayerComponent::S_ClassName) != value.LayerVersion)
      return false;
  }
  if (!input.SketchID.is_nil()) {
    const auto sketch = Component<CPlaneSketchComponent>(scene.Database().GetEntity(input.SketchID));
    if (!sketch || sketch->GetStatus() != "Editing") return false;
    const auto owner = Component<CPlaneObjectComponent>(scene.Database().GetEntity(sketch->GetOwnerID()));
    if (!owner || owner->GetGeometryRef() != sketch->GetBaseGeometryRef()) return false;
    if (input.Kind == "sketch-complete") {
      if (owner->GetGeometryRef() != input.OwnerGeometryRef) return false;
      std::vector<Text> expected, current;
      for (const auto& value : input.Inputs) if (value.ObjectID != input.OwnerID) expected.push_back(IdText(value.ObjectID));
      for (const auto& id : SketchCurveIDs(scene, input.SketchID)) current.push_back(IdText(id));
      std::sort(expected.begin(), expected.end()); std::sort(current.begin(), current.end());
      if (expected != current) return false;
    } else {
      const auto curve = Component<CPlaneSketchCurveComponent>(scene.Database().GetEntity(input.TargetID));
      if (!curve || curve->GetDeleted() || curve->GetSketchID() != input.SketchID) return false;
    }
  }
  return true;
}
}
void InitializeEditTasks(Scene& scene) {
  const auto component = Require<CPlaneSceneComponent>(scene, scene.Database().GetMetaEntity()->GetID());
  if (!component->EditRuntime) component->EditRuntime = std::make_shared<EditTaskRuntime>();
  for (const auto& entity : Collect<CPlaneEditTaskComponent>(scene)) {
    const auto task = Component<CPlaneEditTaskComponent>(entity);
    if (task->GetStatus() == "Running" && !component->EditRuntime->Tasks.contains(IdText(entity->GetID())))
      Mutate(scene, "恢复中断的几何任务", [&](auto& tx) {
        tx.ModifyComponent(entity->GetID(), CPlaneEditTaskComponent::S_ClassName,
          {{"Status", Text("Interrupted")}, {"Error", Text("上次几何计算未完成，原对象未修改")}});
      }, true);
  }
}
Map QueueGeometryTask(Scene& scene, GeometryTaskSpec input, std::function<Map(iCAX::Tasks::CancellationToken)> work) {
  const auto runtime = EditRuntime(scene);
  if (runtime->Stopped || scene.IsStopRequested()) throw std::runtime_error("PLANE_SCENE_STOPPING");
  if (!input.LayerID.is_nil()) {
    if (Require<CPlaneLayerComponent>(scene, input.LayerID)->GetLocked())
      throw std::invalid_argument("PLANE_LAYER_LOCKED");
    input.LayerVersion = scene.Database().GetComponentVersion(input.LayerID, CPlaneLayerComponent::S_ClassName);
  }
  for (auto& value : input.Inputs) {
    const auto object = Require<CPlaneObjectComponent>(scene, value.ObjectID);
    EnsureEditable(scene, *object);value.LayerID = object->GetLayerID();
    value.LayerVersion = scene.Database().GetComponentVersion(value.LayerID, CPlaneLayerComponent::S_ClassName);
  }
  RetireFinishedWork(*runtime);
  for (const auto& [_, task] : runtime->Tasks)
    if (task->Status == "Running" && task->Input.TargetID == input.TargetID)
      throw std::invalid_argument("PLANE_GEOMETRY_TASK_ALREADY_RUNNING");
  auto task = std::make_shared<EditWork>(); task->Id = iCAX::Data::GenerateNewUUID(); task->Input = std::move(input);
  Mutate(scene, "启动几何计算任务", [&](auto& tx) {
    tx.CreateEntity(task->Id); tx.AttachComponent(task->Id, CPlaneEditTaskComponent::S_ClassName,
      {{"Kind", task->Input.Kind}, {"TargetID", task->Input.TargetID}, {"OwnerID", task->Input.OwnerID},
       {"SketchID", task->Input.SketchID}, {"OwnerGeometryRef", task->Input.OwnerGeometryRef}});
    for (const auto& value : task->Input.Inputs) {
      const auto id = iCAX::Data::GenerateNewUUID(); tx.CreateEntity(id);
      const auto object = Require<CPlaneObjectComponent>(scene, value.ObjectID);
      tx.AttachComponent(id, CPlaneTaskInputComponent::S_ClassName,
        {{"TaskID", task->Id}, {"ObjectID", value.ObjectID}, {"Kind", object->GetKind()},
         {"GeometryVersion", value.GeometryVersion}, {"GeometryRef", value.GeometryRef}});
    }
  }, true);
  try { task->Work = iCAX::Tasks::Run(std::move(work), task->Cancellation.Token()); }
  catch (const std::exception& error) {
    task->Status = "Failed"; task->Error = error.what(); SaveState(scene, *task); throw;
  }
  runtime->Tasks.emplace(IdText(task->Id), task);
  auto reply = EditTaskState(task->Id, *Require<CPlaneEditTaskComponent>(scene, task->Id));
  reply["revision"] = WireRevision(scene); return reply;
}
Map ReadGeometryTask(Scene& scene, const Map& request) {
  const auto id = Id(request, "taskId"); return EditTaskState(id, *Require<CPlaneEditTaskComponent>(scene, id));
}
Map CancelGeometryTask(Scene& scene, const Map& request) {
  const auto id = Id(request, "taskId"); Require<CPlaneEditTaskComponent>(scene, id);
  const auto runtime = EditRuntime(scene); const auto found = runtime->Tasks.find(IdText(id));
  if (found != runtime->Tasks.end() && found->second->Status == "Running") found->second->Cancellation.Cancel();
  return ReadGeometryTask(scene, request);
}
void PumpEditTasks(Scene& scene) {
  const auto runtime = EditRuntime(scene);
  if (runtime->Stopped || scene.IsStopRequested()) return;
  for (const auto& [_, task] : runtime->Tasks) {
    if (task->Status != "Running" || !task->Work.Valid() || !task->Work.IsCompleted()) continue;
    if (!scene.Database().GetEntity(task->Id)) { task->Cancellation.Cancel(); task->Status = "Cancelled"; continue; }
    try {
      const auto result = task->Work.Result();
      if (task->Cancellation.IsCancellationRequested()) task->Status = "Cancelled";
      else if (!Matches(scene, task->Input)) {
        task->Status = "Stale"; task->Error = "计算期间输入已修改，原对象未提交";
      } else {
        Map reply;
        if (task->Input.Kind == "sketch-complete") reply = PublishSketchCompletion(scene, task->Input.TargetID, result);
        else if (task->Input.Kind == "curve-array") reply = PublishCurveCopies(scene, task->Input.TargetID, result.at("items").To<Array>(), true);
        else if (task->Input.Kind.starts_with("artwork-")) reply = PublishArtworkTask(scene, task->Input, result);
        else if (task->Input.Kind.starts_with("machining-")) reply = PublishMachiningTask(scene, task->Input, result);
        else throw std::runtime_error("PLANE_GEOMETRY_TASK_UNSUPPORTED");
        task->Status = "Completed"; task->ResultID = Id(reply);
        task->ResultIDs = reply.contains("ids") ? reply.at("ids").To<Array>() : Array{IdText(task->ResultID)};
      }
    } catch (const iCAX::Tasks::TaskCanceledException&) { task->Status = "Cancelled"; }
    catch (const std::exception& error) {
      task->Status = task->Cancellation.IsCancellationRequested() || Text(error.what()).find("PLANE_GEOMETRY_CANCELLED") != Text::npos
        ? "Cancelled" : "Failed";
      task->Error = error.what();
    }
    SaveState(scene, *task);
  }
  RetireFinishedWork(*runtime);
}
void StopEditTasks(Scene& scene) {
  const auto component = Component<CPlaneSceneComponent>(scene.Database().GetMetaEntity());
  if (!component || !component->EditRuntime) return;
  const auto runtime = component->EditRuntime; runtime->Stopped = true;
  for (const auto& [_, task] : runtime->Tasks) task->Cancellation.Cancel();
  for (const auto& [_, task] : runtime->Tasks) if (task->Work.Valid()) {
    task->Work.Wait();
    if (task->Status == "Running") { task->Status = "Cancelled"; SaveState(scene, *task); }
  }
  RetireFinishedWork(*runtime);
  runtime->Stopped = false;
}
} // namespace detail
namespace {
class EditTaskLogic final {
  detail::Scene& scene;
public:
  explicit EditTaskLogic(detail::Scene& value) : scene(value) {}
  Map Read(const Map& request) { return detail::ReadGeometryTask(scene, request); }
  Map Cancel(const Map& request) { return detail::CancelGeometryTask(scene, request); }
};
class CPlaneTaskSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneTaskSDO() : CSDO("PlaneTask") {
    ExposeMethod("Read", &detail::Dispatch<EditTaskLogic, &EditTaskLogic::Read>);
    ExposeMethod("Cancel", &detail::Dispatch<EditTaskLogic, &EditTaskLogic::Cancel>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneTaskSDO)
} // namespace iCAX::PlaneCAM
