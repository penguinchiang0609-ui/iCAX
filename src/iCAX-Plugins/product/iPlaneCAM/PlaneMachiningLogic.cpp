#include "PlaneDomainInternal.h"
#include "RenderInteraction/RenderInteractionComponents.h"
#include <set>

namespace iCAX::PlaneCAM::detail {
namespace {
Map SourceFields(const CPlaneMachiningSourceComponent& value) {
  return {{"SourceKind", value.GetSourceKind()}, {"SourceObjectID", value.GetSourceObjectID()},
    {"PartID", value.GetPartID()}, {"ResultID", value.GetResultID()}, {"ResultBoardID", value.GetResultBoardID()},
    {"PlacementID", value.GetPlacementID()}, {"InstanceIndex", value.GetInstanceIndex()},
    {"SourceGeometryRef", value.GetSourceGeometryRef()}, {"SourceGeometryVersion", value.GetSourceGeometryVersion()},
    {"SourceX", value.GetSourceX()}, {"SourceY", value.GetSourceY()}, {"SourceAngleDeg", value.GetSourceAngleDeg()},
    {"SourceMirror", value.GetSourceMirror()}, {"ReferenceOnly", value.GetReferenceOnly()}};
}
ID DestinationLayer(Scene& scene, const Map& request) {
  const auto id = request.contains("layerId") ? Id(request, "layerId") : Project(scene)->GetDefaultLayerID();
  if (Require<CPlaneLayerComponent>(scene, id)->GetLocked()) throw std::invalid_argument("PLANE_LAYER_LOCKED");
  return id;
}
Map Source(const ID& id, const CPlaneObjectComponent& object, const Text& kind) {
  return {{"SourceKind", kind}, {"SourceObjectID", id}, {"SourceGeometryRef", object.GetGeometryRef()},
    {"SourceGeometryVersion", object.GetGeometryVersion()}};
}
} // namespace
void CopyMachiningSource(Scene& scene, iCAX::Database::ITransaction& tx, const ID& source, const ID& target) {
  const auto value = Component<CPlaneMachiningSourceComponent>(scene.Database().GetEntity(source));
  if (value) tx.AttachComponent(target, CPlaneMachiningSourceComponent::S_ClassName, SourceFields(*value));
}
Map PublishMachiningTask(Scene& scene, const GeometryTaskSpec& task, const Map& result) {
  struct Prepared { ID Id; Text Name; Map Geometry, Source; Ref GeometryRef, PreviewRef; bool Reference = false; };
  const auto items = result.at("items").To<Array>();
  if (items.empty() || items.size() > 10001) throw std::invalid_argument("PLANE_MACHINING_COPY_LIMIT");
  if (Require<CPlaneLayerComponent>(scene, task.LayerID)->GetLocked()) throw std::invalid_argument("PLANE_LAYER_LOCKED");
  std::vector<Prepared> values;
  // Validate every destination and source document before publishing resources.
  for (const auto& item : items) {
    const auto value = item.To<Map>();Prepared prepared;
    prepared.Id = values.empty() && task.RequireAbsentTarget ? task.TargetID : iCAX::Data::GenerateNewUUID();
    prepared.Name = String(value, "name", "加工副本");ObjectProperties("drawing", {{"name", prepared.Name}});
    prepared.Geometry = value.at("geometry").To<Map>();prepared.Source = value.at("source").To<Map>();
    if (!prepared.Geometry.contains("curves") || !prepared.Geometry.contains("paths") ||
        prepared.Geometry.at("curves").To<Array>().empty()) throw std::invalid_argument("PLANE_MACHINING_EMPTY_GEOMETRY");
    prepared.Reference = prepared.Source.contains("ReferenceOnly") && prepared.Source.at("ReferenceOnly").To<bool>();
    Number(prepared.Geometry, "width");Number(prepared.Geometry, "height");values.push_back(std::move(prepared));
  }
  for (auto& value : values) {
    std::vector<Ref> dependencies;
    if (value.Source.contains("SourceGeometryRef")) dependencies.push_back(Reference(value.Source.at("SourceGeometryRef").To<Map>()));
    value.GeometryRef = PublishMap(scene, value.Geometry, value.Name + "独立加工几何", dependencies);
    value.PreviewRef = Preview(scene, value.Geometry, value.Name, value.GeometryRef);
  }
  Mutate(scene, task.Kind == "artwork-vectorize" ? "参考图片转矢量" : "加载独立加工副本", [&](auto& tx) {
    for (const auto& value : values) {
      tx.CreateEntity(value.Id);
      tx.AttachComponent(value.Id, CPlaneObjectComponent::S_ClassName,
        {{"Kind", Text("drawing")}, {"Name", value.Name}, {"LayerID", task.LayerID},
         {"GeometryRef", Stored(value.GeometryRef)}, {"PreviewRef", Stored(value.PreviewRef)},
         {"Width", Number(value.Geometry, "width")}, {"Height", Number(value.Geometry, "height")},
         {"HoleCount", static_cast<UInt>(Number(value.Geometry, "holeCount"))},
         {"ApproximationBoundMm", Number(value.Geometry, "approximationBoundMm")}});
      tx.AttachComponent(value.Id, CPlaneDrawingComponent::S_ClassName);
      tx.AttachComponent(value.Id, CPlaneMachiningSourceComponent::S_ClassName, value.Source);
      AttachRender(tx, value.Id, value.PreviewRef);ApplyLayerDisplay(scene, tx, value.Id, task.LayerID);
      if (value.Reference) tx.ModifyComponent(value.Id, iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
          {{"Selectable", false}});
    }
    Bump(scene, tx, false);
  });
  Array ids;
  for (const auto& value : values) ids.emplace_back(IdText(value.Id));
  auto reply = Receipt(scene, IdText(values.front().Id));reply["ids"] = ids;return reply;
}
Map LoadMachiningPart(Scene& scene, const Map& request) {
  CheckExpected(scene, request);RequireWorktableRequest(request);const auto id = Id(request, "partId");
  Require<CPlanePartComponent>(scene, id);const auto object = Require<CPlaneObjectComponent>(scene, id);
  if (object->GetKind() != "parts") throw std::invalid_argument("PLANE_PART_REQUIRED");
  EnsureEditable(scene, *object);if (request.contains("expectedGeometryRef")) CheckExpectedGeometry(request, object->GetGeometryRef());
  const double x = Number(request, "x"), y = Number(request, "y");
  const auto sourceGeometry = ReadMap(scene, object->GetGeometryRef());
  auto source = Source(id, *object, "part");source["PartID"] = id;
  GeometryTaskSpec task;task.Kind = "machining-part";task.TargetID = iCAX::Data::GenerateNewUUID();
  task.RequireAbsentTarget = true;task.LayerID = DestinationLayer(scene, request);
  task.Inputs.push_back({id, object->GetGeometryRef(), object->GetGeometryVersion()});
  const auto service = Geometry(scene);
  const auto name = object->GetName() + " 加工副本";
  return QueueGeometryTask(scene, std::move(task), [service, sourceGeometry, source, name, x, y](auto token) {
    token.ThrowIfCancellationRequested();auto geometry = service->TransformGeometry(sourceGeometry,
      {{"operation", Text("move")}, {"dx", x}, {"dy", y}});
    geometry["approximationBoundMm"] = std::max(Number(sourceGeometry, "approximationBoundMm"), Number(geometry, "approximationBoundMm"));
    token.ThrowIfCancellationRequested();
    return Map{{"items", Array{Map{{"name", name}, {"geometry", geometry}, {"source", source}}}}};
  });
}
Map LoadMachiningResult(Scene& scene, const Map& request) {
  CheckExpected(scene, request);RequireWorktableRequest(request);const auto id = Id(request, "boardId");
  const auto board = Require<CPlaneResultBoardComponent>(scene, id);
  const auto object = Require<CPlaneObjectComponent>(scene, id);
  if (object->GetKind() != "result-board") throw std::invalid_argument("PLANE_RESULT_BOARD_REQUIRED");
  const auto result = Require<CPlaneResultComponent>(scene, board->GetResultID());
  if (!result->GetValid()) throw std::invalid_argument("PLANE_MACHINING_RESULT_INVALID");
  EnsureEditable(scene, *object);
  if (request.contains("expectedGeometryRef")) CheckExpectedGeometry(request, object->GetGeometryRef());
  GeometryTaskSpec task;task.Kind = "machining-board";task.TargetID = iCAX::Data::GenerateNewUUID();
  task.RequireAbsentTarget = true;task.LayerID = DestinationLayer(scene, request);task.OwnerID = id;
  task.Inputs.push_back({id, object->GetGeometryRef(), object->GetGeometryVersion()});
  auto backgroundSource = Source(id, *object, "result-board");
  backgroundSource["ResultID"] = board->GetResultID();backgroundSource["ResultBoardID"] = id;backgroundSource["ReferenceOnly"] = true;
  Array items{Map{{"name", object->GetName() + " 板材参考"}, {"geometry", ReadMap(scene, object->GetGeometryRef())}, {"source", backgroundSource}}};
  auto placements = Collect<CPlanePlacementComponent>(scene);
  std::sort(placements.begin(), placements.end(), [](auto a, auto b) { return IdText(a->GetID()) < IdText(b->GetID()); });
  for (const auto& entity : placements) {
    const auto placement = Component<CPlanePlacementComponent>(entity);
    if (placement->GetResultBoardID() != id) continue;
    const auto placed = Require<CPlaneObjectComponent>(scene, entity->GetID());EnsureEditable(scene, *placed);
    task.Inputs.push_back({entity->GetID(), placed->GetGeometryRef(), placed->GetGeometryVersion()});
    auto source = Source(entity->GetID(), *placed, "result-placement");
    source["PartID"] = placement->GetPartID();source["ResultID"] = placement->GetResultID();
    source["ResultBoardID"] = id;source["PlacementID"] = entity->GetID();source["InstanceIndex"] = placement->GetInstanceIndex();
    source["SourceX"] = placement->GetX();source["SourceY"] = placement->GetY();
    source["SourceAngleDeg"] = placement->GetAngleDeg();source["SourceMirror"] = placement->GetMirror();
    auto part = Component<CPlaneObjectComponent>(scene.Database().GetEntity(placement->GetPartID()));
    items.emplace_back(Map{{"name", (part ? part->GetName() : Text("排入零件")) + " 加工副本"},
      {"geometry", ReadMap(scene, placed->GetGeometryRef())}, {"source", source}});
  }
  if (items.size() < 2) throw std::invalid_argument("PLANE_MACHINING_BOARD_EMPTY");
  const auto service = Geometry(scene);const double x = Number(request, "x"), y = Number(request, "y");
  return QueueGeometryTask(scene, std::move(task), [service, items, x, y](auto token) mutable {
    for (auto& item : items) {
      token.ThrowIfCancellationRequested();auto value = item.To<Map>();
      auto geometry = value.at("geometry").To<Map>();const auto source = value.at("source").To<Map>();
      const double approximation = Number(geometry, "approximationBoundMm");
      if (String(source, "SourceKind") == "result-placement") {
        geometry = service->TransformGeometry(geometry, {{"operation", Text("rotate")}, {"angleDeg", Number(source, "SourceAngleDeg")}});
        if (source.at("SourceMirror").To<bool>()) geometry = service->TransformGeometry(geometry,
          {{"operation", Text("mirror")}, {"axis", Text("x")}});
        geometry = service->TransformGeometry(geometry, {{"operation", Text("move")},
          {"dx", Number(source, "SourceX")}, {"dy", Number(source, "SourceY")}});
      }
      if (x != 0 || y != 0) geometry = service->TransformGeometry(geometry,
        {{"operation", Text("move")}, {"dx", x}, {"dy", y}});
      geometry["approximationBoundMm"] = std::max(approximation, Number(geometry, "approximationBoundMm"));
      value["geometry"] = geometry;item = std::move(value);
    }
    return Map{{"items", std::move(items)}};
  });
}
} // namespace iCAX::PlaneCAM::detail
