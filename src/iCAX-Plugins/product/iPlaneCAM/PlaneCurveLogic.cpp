#include "PlaneDomainInternal.h"
#include "RenderInteraction/RenderInteractionComponents.h"
namespace iCAX::PlaneCAM::detail {
void UpdateGeometry(iCAX::Database::ITransaction& tx, const ID& id,
    const CPlaneObjectComponent& object, const Map& geometry, const Ref& reference, const Ref& preview) {
  tx.ModifyComponent(id, CPlaneObjectComponent::S_ClassName,
    {{"GeometryRef", Stored(reference)}, {"PreviewRef", Stored(preview)},
     {"GeometryVersion", Next(object.GetGeometryVersion())},
     {"ApproximationBoundMm", Number(geometry, "approximationBoundMm")},
     {"Width", Number(geometry, "width")}, {"Height", Number(geometry, "height")},
     {"HoleCount", static_cast<UInt>(Number(geometry, "holeCount"))}});
  tx.ModifyComponent(id, iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
    {{"GeometryResourceID", preview.URL}, {"GeometryResourceVersion", static_cast<UInt>(preview.nVersion)}});
}
Map ReadCurveGeometry(Scene& scene, const Map& request, const Text& kind) {
  const auto id = Id(request);
  const auto object = Require<CPlaneObjectComponent>(scene, id);
  if (object->GetKind() != kind) throw std::invalid_argument(
    kind == "drawing" ? "PLANE_DRAWING_REQUIRED" : "PLANE_SKETCH_CURVE_REQUIRED");
  if (Component<CPlaneArtworkComponent>(scene.Database().GetEntity(id))) return ReadArtworkGeometry(scene, id);
  const auto data = ReadMap(scene, object->GetGeometryRef());
  return {{"id", IdText(id)}, {"geometryVersion", std::to_string(object->GetGeometryVersion())},
    {"geometryRef", Wire(object->GetGeometryRef())}, {"curves", data.at("curves")},
    {"snapPoints", data.at("snapPoints")},
    {"dimensions", Geometry(scene)->DescribeDrawingDimensions(data, IdText(id))}};
}
Map PublishCurveCopies(Scene& scene, const ID& sourceID, const Array& geometries, bool array) {
  const auto source = Require<CPlaneObjectComponent>(scene, sourceID);
  const auto provenance = Component<CPlaneMachiningSourceComponent>(scene.Database().GetEntity(sourceID));
  EnsureEditable(scene, *source);
  if (source->GetKind() == "sketch-curves") RequireSketchCurveEditing(scene, sourceID);
  struct Prepared { ID Id; Text Name; Map Geometry; Ref Reference, PreviewRef; };
  std::vector<Prepared> prepared;
  const std::size_t first = array ? 1 : 0;
  for (std::size_t index = first; index < geometries.size(); ++index) {
    Prepared value; value.Id = iCAX::Data::GenerateNewUUID();
    value.Name = source->GetName() + " " + std::to_string(index + 1);
    value.Geometry = geometries[index].To<Map>();
    value.Geometry["approximationBoundMm"] = std::max(Number(value.Geometry, "approximationBoundMm"), source->GetApproximationBoundMm());
    value.Reference = PublishMap(scene, value.Geometry, value.Name + "几何", {Reference(source->GetGeometryRef())});
    value.PreviewRef = Preview(scene, value.Geometry, value.Name, value.Reference);
    prepared.push_back(std::move(value));
  }
  if (prepared.empty()) {
    auto reply = Receipt(scene, IdText(sourceID)); reply["ids"] = Array{IdText(sourceID)}; return reply;
  }
  Mutate(scene, array ? "阵列刀路或草图曲线" : "复制刀路或草图曲线", [&](auto& tx) {
    for (const auto& value : prepared) {
      tx.CreateEntity(value.Id);
      tx.AttachComponent(value.Id, CPlaneObjectComponent::S_ClassName,
        {{"Kind", source->GetKind()}, {"Name", value.Name}, {"Note", source->GetNote()},
         {"LayerID", source->GetLayerID()}, {"GeometryRef", Stored(value.Reference)},
         {"PreviewRef", Stored(value.PreviewRef)}, {"SourceRef", source->GetSourceRef()},
         {"SourceName", source->GetSourceName()}, {"SourceUnits", source->GetSourceUnits()},
         {"UnitSource", source->GetUnitSource()}, {"Width", Number(value.Geometry, "width")},
         {"Height", Number(value.Geometry, "height")},
         {"HoleCount", static_cast<UInt>(Number(value.Geometry, "holeCount"))},
         {"ApproximationBoundMm", Number(value.Geometry, "approximationBoundMm")}});
      if (source->GetKind() == "drawing") tx.AttachComponent(value.Id, CPlaneDrawingComponent::S_ClassName);
      else tx.AttachComponent(value.Id, CPlaneSketchCurveComponent::S_ClassName,
        {{"SketchID", Require<CPlaneSketchCurveComponent>(scene, sourceID)->GetSketchID()}});
      AttachRender(tx, value.Id, value.PreviewRef);
      ApplyLayerDisplay(scene, tx, value.Id, source->GetLayerID());
      CopyMachiningSource(scene, tx, sourceID, value.Id);
      if (provenance && provenance->GetReferenceOnly())
        tx.ModifyComponent(value.Id, iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName, {{"Selectable", false}});
    }
    Bump(scene, tx, false);
  });
  Array ids;
  if (array) ids.emplace_back(IdText(sourceID));
  for (const auto& value : prepared) ids.emplace_back(IdText(value.Id));
  auto reply = Receipt(scene, array || prepared.empty() ? IdText(sourceID) : IdText(prepared.front().Id));
  reply["ids"] = ids; return reply;
}
Map EditCurveObject(Scene& scene, const Map& request, const Text& kind) {
  CheckExpected(scene, request);
  const auto id = Id(request);
  const auto object = Require<CPlaneObjectComponent>(scene, id);
  if (object->GetKind() != kind) throw std::invalid_argument(
    kind == "drawing" ? "PLANE_DRAWING_REQUIRED" : "PLANE_SKETCH_CURVE_REQUIRED");
  EnsureEditable(scene, *object);
  if (Component<CPlaneArtworkComponent>(scene.Database().GetEntity(id))) return EditArtwork(scene, request);
  if (kind == "sketch-curves") RequireSketchCurveEditing(scene, id);
  const auto operation = String(request, "operation");
  if (operation == "dimensions" || operation == "nodes" || request.contains("expectedGeometryRef"))
    CheckExpectedGeometry(request, object->GetGeometryRef());
  if (operation == "delete") {
    if (kind == "drawing") { auto remove = request; remove["kind"] = kind; return UpdateObject(scene, remove, true); }
    Mutate(scene, "删除草图曲线", [&](auto& tx) {
      tx.ModifyComponent(id, CPlaneSketchCurveComponent::S_ClassName, {{"Deleted", true}});
      const bool deleted = true;
      ApplyLayerDisplay(scene, tx, id, object->GetLayerID(), nullptr, nullptr, nullptr, &deleted);
      Bump(scene, tx, false);
    });
    return Receipt(scene, IdText(id));
  }
  const auto source = ReadMap(scene, object->GetGeometryRef());
  const auto service = Geometry(scene);
  if (operation == "array-rectangular" || operation == "array-polar") {
    if (request.contains("async") && request.at("async").To<bool>()) {
      GeometryTaskSpec spec; spec.Kind = "curve-array"; spec.TargetID = id;
      spec.Inputs.push_back({id, object->GetGeometryRef(), object->GetGeometryVersion()});
      if (kind == "sketch-curves") spec.SketchID = Require<CPlaneSketchCurveComponent>(scene, id)->GetSketchID();
      return QueueGeometryTask(scene, std::move(spec), [service, source, request](auto token) {
        return Map{{"items", service->ArrayGeometry(source, request, [token] { return token.IsCancellationRequested(); })}};
      });
    }
    return PublishCurveCopies(scene, id, service->ArrayGeometry(source, request), true);
  }
  auto geometry = service->TransformGeometry(source, request);
  const double inheritedBound = object->GetApproximationBoundMm() *
      (operation == "scale" ? Number(request, "factor", 1) : 1.);
  geometry["approximationBoundMm"] = std::max(Number(geometry, "approximationBoundMm"), inheritedBound);
  if (operation == "copy") return PublishCurveCopies(scene, id, Array{geometry}, false);
  const auto reference = PublishMap(scene, geometry, "编辑后曲线", {Reference(object->GetGeometryRef())});
  const auto preview = Preview(scene, geometry, "曲线显示", reference);
  Mutate(scene, operation == "close" ? "闭合多段线" : operation == "dimensions" ? "修改曲线尺寸" : "编辑曲线", [&](auto& tx) {
    UpdateGeometry(tx, id, *object, geometry, reference, preview); Bump(scene, tx, false);
  });
  return Receipt(scene, IdText(id));
}
} // namespace iCAX::PlaneCAM::detail
