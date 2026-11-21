#include "PlaneSDODispatch.h"
#include "RenderInteraction/RenderInteractionComponents.h"
#include <set>
namespace iCAX::PlaneCAM::detail {
namespace {
std::shared_ptr<CPlaneObjectComponent> Owner(Scene& scene, const CPlaneSketchComponent& sketch, bool checkVersion = true) {
  auto owner = Require<CPlaneObjectComponent>(scene, sketch.GetOwnerID());
  if (owner->GetKind() != "parts" && owner->GetKind() != "stocks")
    throw std::invalid_argument("PLANE_SKETCH_OWNER_REQUIRED");
  if (checkVersion && owner->GetGeometryRef() != sketch.GetBaseGeometryRef())
    throw std::invalid_argument("PLANE_SKETCH_STALE_OWNER");
  return owner;
}
std::vector<ID> AllCurves(Scene& scene, const ID& sketch) {
  std::vector<ID> result;
  for (const auto& entity : Collect<CPlaneSketchCurveComponent>(scene))
    if (Component<CPlaneSketchCurveComponent>(entity)->GetSketchID() == sketch) result.push_back(entity->GetID());
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return IdText(a) < IdText(b); });
  return result;
}
std::vector<ID> Backups(Scene& scene, const ID& sketch) {
  std::vector<ID> result;
  for (const auto& entity : Collect<CPlaneSketchBackupComponent>(scene))
    if (Component<CPlaneSketchBackupComponent>(entity)->GetSketchID() == sketch) result.push_back(entity->GetID());
  return result;
}
void Backup(iCAX::Database::ITransaction& tx, const ID& sketchID, const ID& curveID, const CPlaneObjectComponent& curve) {
  const auto id = iCAX::Data::GenerateNewUUID(); tx.CreateEntity(id);
  tx.AttachComponent(id, CPlaneSketchBackupComponent::S_ClassName,
    {{"SketchID", sketchID}, {"CurveID", curveID}, {"Name", curve.GetName()}, {"Note", curve.GetNote()},
     {"LayerID", curve.GetLayerID()}, {"GeometryRef", curve.GetGeometryRef()}, {"PreviewRef", curve.GetPreviewRef()},
     {"GeometryVersion", curve.GetGeometryVersion()}, {"Width", curve.GetWidth()}, {"Height", curve.GetHeight()},
     {"ApproximationBoundMm", curve.GetApproximationBoundMm()}});
}
struct NewCurve { ID Id; Text Name; Map Geometry; Ref Reference, PreviewRef; };
NewCurve PrepareCurve(Scene& scene, Map geometry, const Text& name, const std::vector<Ref>& dependencies = {}) {
  NewCurve value; value.Id = iCAX::Data::GenerateNewUUID(); value.Name = name; value.Geometry = std::move(geometry);
  value.Reference = PublishMap(scene, value.Geometry, name + "几何", dependencies);
  value.PreviewRef = Preview(scene, value.Geometry, name, value.Reference); return value;
}
void AttachCurve(Scene& scene, iCAX::Database::ITransaction& tx, const NewCurve& curve, const ID& sketch, const ID& layer) {
  tx.CreateEntity(curve.Id);
  tx.AttachComponent(curve.Id, CPlaneObjectComponent::S_ClassName,
    {{"Kind", Text("sketch-curves")}, {"Name", curve.Name}, {"LayerID", layer},
     {"GeometryRef", Stored(curve.Reference)}, {"PreviewRef", Stored(curve.PreviewRef)},
     {"Width", Number(curve.Geometry, "width")}, {"Height", Number(curve.Geometry, "height")},
     {"ApproximationBoundMm", Number(curve.Geometry, "approximationBoundMm")}});
  tx.AttachComponent(curve.Id, CPlaneSketchCurveComponent::S_ClassName, {{"SketchID", sketch}});
  AttachRender(tx, curve.Id, curve.PreviewRef);
  ApplyLayerDisplay(scene, tx, curve.Id, layer);
}
void BackupPrepared(iCAX::Database::ITransaction& tx, const ID& sketchID, const NewCurve& curve, const ID& layer) {
  const auto id = iCAX::Data::GenerateNewUUID(); tx.CreateEntity(id);
  tx.AttachComponent(id, CPlaneSketchBackupComponent::S_ClassName,
    {{"SketchID", sketchID}, {"CurveID", curve.Id}, {"Name", curve.Name}, {"LayerID", layer},
     {"GeometryRef", Stored(curve.Reference)}, {"PreviewRef", Stored(curve.PreviewRef)}, {"GeometryVersion", 1ull},
     {"Width", Number(curve.Geometry, "width")}, {"Height", Number(curve.Geometry, "height")},
     {"ApproximationBoundMm", Number(curve.Geometry, "approximationBoundMm")}});
}
}
std::vector<ID> SketchCurveIDs(Scene& scene, const ID& sketch) {
  auto ids = AllCurves(scene, sketch);
  ids.erase(std::remove_if(ids.begin(), ids.end(), [&](const ID& id) {
    return Require<CPlaneSketchCurveComponent>(scene, id)->GetDeleted();
  }), ids.end()); return ids;
}
Map SketchState(Scene& scene, const ID& id, const CPlaneSketchComponent& sketch) {
  Array ids;
  for (const auto& curve : SketchCurveIDs(scene, id)) ids.emplace_back(IdText(curve));
  const auto owner = Owner(scene, sketch, false);
  return {{"id", IdText(id)}, {"ownerId", IdText(sketch.GetOwnerID())}, {"ownerKind", owner->GetKind()},
    {"ownerName", owner->GetName()}, {"status", sketch.GetStatus()},
    {"baseGeometryRef", Wire(sketch.GetBaseGeometryRef())}, {"curveIds", ids}};
}
void RequireSketchCurveEditing(Scene& scene, const ID& id) {
  const auto curve = Require<CPlaneSketchCurveComponent>(scene, id);
  if (curve->GetDeleted()) throw std::invalid_argument("PLANE_SKETCH_CURVE_REQUIRED");
  const auto sketch = Require<CPlaneSketchComponent>(scene, curve->GetSketchID());
  if (sketch->GetStatus() != "Editing") throw std::invalid_argument("PLANE_SKETCH_NOT_EDITING");
  EnsureEditable(scene, *Owner(scene, *sketch));
}
void DisposeOwnedSketch(Scene& scene, iCAX::Database::ITransaction& tx, const ID& ownerID) {
  const auto owner = Require<CPlaneObjectComponent>(scene, ownerID);
  if (owner->GetSketchID().is_nil()) return;
  const auto sketch = Require<CPlaneSketchComponent>(scene, owner->GetSketchID());
  if (sketch->GetStatus() == "Editing") throw std::invalid_argument("PLANE_SKETCH_BUSY");
  for (const auto& id : AllCurves(scene, owner->GetSketchID())) tx.DisposeEntity(id);
  for (const auto& id : Backups(scene, owner->GetSketchID())) tx.DisposeEntity(id);
  tx.DisposeEntity(owner->GetSketchID());
}
Map PublishSketchCompletion(Scene& scene, const ID& id, const Map& geometry) {
  const auto sketch = Require<CPlaneSketchComponent>(scene, id);
  if (sketch->GetStatus() != "Editing") throw std::invalid_argument("PLANE_SKETCH_NOT_EDITING");
  const auto owner = Owner(scene, *sketch); EnsureEditable(scene, *owner);
  for (const auto& curve : SketchCurveIDs(scene, id))
    EnsureEditable(scene, *Require<CPlaneObjectComponent>(scene, curve));
  const auto reference = PublishMap(scene, geometry, "草图完成轮廓", {Reference(owner->GetGeometryRef())});
  const auto preview = Preview(scene, geometry, owner->GetName(), reference);
  Mutate(scene, "完成草图编辑", [&](auto& tx) {
    UpdateGeometry(tx, sketch->GetOwnerID(), *owner, geometry, reference, preview);
    for (const auto& curve : AllCurves(scene, id))
      if (Require<CPlaneSketchCurveComponent>(scene, curve)->GetDeleted()) tx.DisposeEntity(curve);
    for (const auto& backup : Backups(scene, id)) tx.DisposeEntity(backup);
    tx.ModifyComponent(id, CPlaneSketchComponent::S_ClassName,
      {{"Status", Text("Ready")}, {"BaseGeometryRef", Stored(reference)}});
    Bump(scene, tx, true);
  });
  auto reply = Receipt(scene, IdText(sketch->GetOwnerID())); reply["sketchId"] = IdText(id); return reply;
}
} // namespace iCAX::PlaneCAM::detail

namespace iCAX::PlaneCAM {
namespace {
class SketchLogic final {
  detail::Scene& scene;
public:
  explicit SketchLogic(detail::Scene& value) : scene(value) {}
  Map Open(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request); const auto ownerID = Id(request, "ownerId");
    const auto owner = Require<CPlaneObjectComponent>(scene, ownerID);
    if (owner->GetKind() != "parts" && owner->GetKind() != "stocks")
      throw std::invalid_argument("PLANE_SKETCH_OWNER_REQUIRED");
    EnsureEditable(scene, *owner);
    auto id = owner->GetSketchID();
    auto existing = id.is_nil() ? nullptr : Component<CPlaneSketchComponent>(scene.Database().GetEntity(id));
    if (existing && existing->GetOwnerID() != ownerID) throw std::invalid_argument("PLANE_SKETCH_OWNER_REQUIRED");
    if (existing && existing->GetStatus() == "Editing") {
      Owner(scene, *existing);
      auto reply = Receipt(scene, IdText(id)); reply["sketchId"] = IdText(id); reply["ownerId"] = IdText(ownerID); return reply;
    }
    const bool seed = !existing || existing->GetBaseGeometryRef() != owner->GetGeometryRef();
    if (id.is_nil()) id = iCAX::Data::GenerateNewUUID();
    std::vector<NewCurve> prepared;
    if (seed) {
      const auto geometry = ReadMap(scene, owner->GetGeometryRef()); std::size_t index = 0;
      for (const auto& curve : geometry.at("curves").To<Array>())
        prepared.push_back(PrepareCurve(scene, Geometry(scene)->BuildGeometry(Array{curve}, false),
          "轮廓 " + std::to_string(++index), {Reference(owner->GetGeometryRef())}));
    }
    Mutate(scene, "进入草图编辑", [&](auto& tx) {
      if (!existing) {
        tx.CreateEntity(id); tx.AttachComponent(id, CPlaneSketchComponent::S_ClassName,
          {{"OwnerID", ownerID}, {"Status", Text("Editing")}, {"BaseGeometryRef", owner->GetGeometryRef()}});
        tx.ModifyComponent(ownerID, CPlaneObjectComponent::S_ClassName, {{"SketchID", id}});
      } else {
        tx.ModifyComponent(id, CPlaneSketchComponent::S_ClassName,
          {{"Status", Text("Editing")}, {"BaseGeometryRef", owner->GetGeometryRef()}});
        for (const auto& backup : Backups(scene, id)) tx.DisposeEntity(backup);
      }
      if (seed) {
        if (existing) for (const auto& curve : AllCurves(scene, id)) tx.DisposeEntity(curve);
        for (const auto& curve : prepared) {
          AttachCurve(scene, tx, curve, id, owner->GetLayerID()); BackupPrepared(tx, id, curve, owner->GetLayerID());
        }
      } else for (const auto& curve : SketchCurveIDs(scene, id))
        Backup(tx, id, curve, *Require<CPlaneObjectComponent>(scene, curve));
      Bump(scene, tx, false);
    });
    auto reply = Receipt(scene, IdText(id)); reply["sketchId"] = IdText(id); reply["ownerId"] = IdText(ownerID); return reply;
  }
  Map Create(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request); const auto id = Id(request, "sketchId");
    const auto sketch = Require<CPlaneSketchComponent>(scene, id);
    if (sketch->GetStatus() != "Editing") throw std::invalid_argument("PLANE_SKETCH_NOT_EDITING");
    const auto owner = Owner(scene, *sketch); EnsureEditable(scene, *owner);
    const auto layerID = Project(scene)->GetDefaultLayerID();
    if (Require<CPlaneLayerComponent>(scene, layerID)->GetLocked()) throw std::invalid_argument("PLANE_LAYER_LOCKED");
    const auto name = String(request, "name", "草图曲线");
    if (name.empty() || name.size() > 1024) throw std::invalid_argument("PLANE_INVALID_NAME");
    const auto curve = PrepareCurve(scene, Geometry(scene)->CreateGeometry(request), name);
    Mutate(scene, "添加草图曲线", [&](auto& tx) { AttachCurve(scene, tx, curve, id, layerID); Bump(scene, tx, false); });
    auto reply = Receipt(scene, IdText(curve.Id)); reply["sketchId"] = IdText(id); return reply;
  }
  Map Edit(const Map& request) { return detail::EditCurveObject(scene, request, "sketch-curves"); }
  Map Remove(const Map& request) { auto remove = request; remove["operation"] = Text("delete"); return Edit(remove); }
  Map Read(const Map& request) { return detail::ReadCurveGeometry(scene, request, "sketch-curves"); }
  Map Complete(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request); const auto id = Id(request);
    const auto sketch = Require<CPlaneSketchComponent>(scene, id);
    if (sketch->GetStatus() != "Editing") throw std::invalid_argument("PLANE_SKETCH_NOT_EDITING");
    const auto owner = Owner(scene, *sketch); EnsureEditable(scene, *owner);
    Array curves;
    GeometryTaskSpec spec; spec.Kind = "sketch-complete"; spec.TargetID = id;
    spec.OwnerID = sketch->GetOwnerID(); spec.SketchID = id; spec.OwnerGeometryRef = owner->GetGeometryRef();
    spec.Inputs.push_back({sketch->GetOwnerID(), owner->GetGeometryRef(), owner->GetGeometryVersion()});
    for (const auto& curveID : SketchCurveIDs(scene, id)) {
      const auto curve = Require<CPlaneObjectComponent>(scene, curveID); EnsureEditable(scene, *curve);
      const auto values = ReadMap(scene, curve->GetGeometryRef()).at("curves").To<Array>();
      curves.insert(curves.end(), values.begin(), values.end());
      spec.Inputs.push_back({curveID, curve->GetGeometryRef(), curve->GetGeometryVersion()});
    }
    const auto service = Geometry(scene);
    if (request.contains("async") && request.at("async").To<bool>())
      return QueueGeometryTask(scene, std::move(spec), [service, curves](auto token) {
        return service->BuildGeometry(curves, true, [token] { return token.IsCancellationRequested(); });
      });
    return PublishSketchCompletion(scene, id, service->BuildGeometry(curves, true));
  }
  Map Cancel(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request); const auto id = Id(request);
    const auto sketch = Require<CPlaneSketchComponent>(scene, id);
    if (sketch->GetStatus() != "Editing") throw std::invalid_argument("PLANE_SKETCH_NOT_EDITING");
    const auto ownerID = sketch->GetOwnerID(); std::set<Text> saved;
    const auto backups = Backups(scene, id);
    for (const auto& backup : backups) saved.insert(IdText(Require<CPlaneSketchBackupComponent>(scene, backup)->GetCurveID()));
    Mutate(scene, "取消草图编辑", [&](auto& tx) {
      for (const auto& curve : AllCurves(scene, id)) if (!saved.contains(IdText(curve))) tx.DisposeEntity(curve);
      for (const auto& backupID : backups) {
        const auto backup = Require<CPlaneSketchBackupComponent>(scene, backupID); const auto curve = backup->GetCurveID();
        tx.ModifyComponent(curve, CPlaneObjectComponent::S_ClassName,
          {{"Name", backup->GetName()}, {"Note", backup->GetNote()}, {"LayerID", backup->GetLayerID()},
           {"GeometryRef", backup->GetGeometryRef()}, {"PreviewRef", backup->GetPreviewRef()},
           {"GeometryVersion", backup->GetGeometryVersion()}, {"Width", backup->GetWidth()},
           {"Height", backup->GetHeight()}, {"ApproximationBoundMm", backup->GetApproximationBoundMm()}});
        tx.ModifyComponent(curve, CPlaneSketchCurveComponent::S_ClassName, {{"Deleted", false}});
        const auto preview = Reference(backup->GetPreviewRef());
        tx.ModifyComponent(curve, iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
          {{"GeometryResourceID", preview.URL}, {"GeometryResourceVersion", static_cast<UInt>(preview.nVersion)}});
        const bool deleted = false;
        ApplyLayerDisplay(scene, tx, curve, backup->GetLayerID(), nullptr, nullptr, nullptr, &deleted);
        tx.DisposeEntity(backupID);
      }
      tx.ModifyComponent(id, CPlaneSketchComponent::S_ClassName, {{"Status", Text("Ready")}});
      Bump(scene, tx, false);
    });
    auto reply = Receipt(scene, IdText(ownerID)); reply["sketchId"] = IdText(id); return reply;
  }
};
class CPlaneSketchSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneSketchSDO() : CSDO("PlaneSketch") {
    ExposeMethod("Open", &detail::Dispatch<SketchLogic, &SketchLogic::Open>);
    ExposeMethod("Create", &detail::Dispatch<SketchLogic, &SketchLogic::Create>);
    ExposeMethod("Edit", &detail::Dispatch<SketchLogic, &SketchLogic::Edit>);
    ExposeMethod("Remove", &detail::Dispatch<SketchLogic, &SketchLogic::Remove>);
    ExposeMethod("ReadGeometry", &detail::Dispatch<SketchLogic, &SketchLogic::Read>);
    ExposeMethod("Complete", &detail::Dispatch<SketchLogic, &SketchLogic::Complete>);
    ExposeMethod("Cancel", &detail::Dispatch<SketchLogic, &SketchLogic::Cancel>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneSketchSDO)
} // namespace iCAX::PlaneCAM
