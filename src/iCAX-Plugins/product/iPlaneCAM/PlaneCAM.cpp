#include "PlaneCAM.h"
#include "PlaneDomainInternal.h"
#include "Data/VariantSerializer.h"
#include "PlaneComponents.h"
#include "PlaneGeometry.h"
#include "ProjectFile/ProjectDocument.h"
#include "RenderData/RenderResource.h"
#include "RenderInteraction/RenderInteractionComponents.h"
#include "Resources/BinaryResource.h"
#include "Resources/FlatBufferResource.h"
#include "Resources/ResourceLibrary.h"
#include "Services/ServiceProvider.h"
#include "Task/Task.h"
#include "Transform/TransformComponent.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>

namespace iCAX::PlaneCAM {
namespace N = iCAX::GeometryServices::PlaneNesting;
using Scene = iCAX::Project::ISceneContext;
using Ref = iCAX::Resource::CResourceReference;
struct TaskRecord {
  Text Id, Status = "Running", Error, ResultID;
  UInt InputRevision = 0;
  N::Request Input;
  std::map<Text, Map> Geometries;
  std::map<Text, Ref> GeometryRefs;
  std::vector<Ref> Dependencies;
  iCAX::Tasks::CancellationTokenSource Cancellation;
  iCAX::Tasks::Task<N::Result> Work;
};
struct SceneRuntime {
  std::map<Text, std::shared_ptr<TaskRecord>> Tasks;
  bool Stopped = false;
  ~SceneRuntime() {
    // Repository baseline restoration does not emit component Add events, and
    // Scene shutdown need not emit Disable. Keep worker lifetime bounded by
    // this transient component state without accessing a destroyed Scene.
    for (const auto &[_, task] : Tasks)
      task->Cancellation.Cancel();
    for (const auto &[_, task] : Tasks)
      if (task->Work.Valid())
        task->Work.Wait();
  }
};
namespace detail {
Text IdText(const ID &id) { return iCAX::Data::to_string(id); }
ID Id(const Map &p, const Text &key) {
  const auto id = ID::from_string(String(p, key));
  if (!id || id->is_nil())
    throw std::invalid_argument("PLANE_INVALID_ID: " + key);
  return *id;
}
std::shared_ptr<CPlaneProjectComponent> Project(Scene &s) {
  return Require<CPlaneProjectComponent>(s,
                                         s.Database().GetMetaEntity()->GetID());
}
std::shared_ptr<SceneRuntime> Runtime(Scene &s) {
  auto c =
      Require<CPlaneSceneComponent>(s, s.Database().GetMetaEntity()->GetID());
  if (!c->Runtime)
    throw std::runtime_error("PLANE_SCENE_NOT_INITIALIZED");
  return c->Runtime;
}
UInt Next(UInt v) {
  if (v == std::numeric_limits<UInt>::max())
    throw std::overflow_error("PLANE_REVISION_EXHAUSTED");
  return v + 1;
}
Text WireRevision(Scene &s) {
  return std::to_string(
      s.Database().GetComponentVersion(s.Database().GetMetaEntity()->GetID(),
                                       CPlaneProjectComponent::S_ClassName));
}
void CheckExpected(Scene &s, const Map &p) {
  if (p.contains("expectedRevision") &&
      String(p, "expectedRevision") != WireRevision(s))
    throw std::invalid_argument("PLANE_STALE_REVISION");
}
void Bump(Scene &s, iCAX::Database::ITransaction &tx, bool nesting) {
  auto p = Project(s);
  iCAX::Data::PropertySet values{{"Revision", Next(p->GetRevision())}};
  if (nesting)
    values.emplace("NestingRevision", Next(p->GetNestingRevision()));
  tx.ModifyComponent(s.Database().GetMetaEntity()->GetID(),
                     CPlaneProjectComponent::S_ClassName, values);
}
Map Receipt(Scene &s, const Text &id) {
  Map r{{"revision", WireRevision(s)}, {"status", Text("Applied")}};
  if (!id.empty())
    r["id"] = id;
  return r;
}
Map Stored(const Ref &ref) {
  return iCAX::ProjectFile::MakeResourceReferenceValue({ref.URL, ref.nVersion})
      .To<Map>();
}
Ref Reference(const Map &ref) {
  const auto r = iCAX::ProjectFile::TryGetResourceReferenceValue(ref);
  if (!r)
    throw std::runtime_error("PLANE_RESOURCE_REFERENCE_INVALID");
  return {r->URL, r->nVersion};
}
Map Wire(const Map &ref) {
  if (ref.empty())
    return {};
  const auto r = Reference(ref);
  return {{"url", r.URL}, {"version", std::to_string(r.nVersion)}};
}
void CheckExpectedGeometry(const Map &request, const Map &stored) {
  Map expected;
  try {
    expected = request.at("expectedGeometryRef").To<Map>();
    if (expected.at("url").To<Text>().empty() ||
        expected.at("version").To<Text>().empty())
      throw std::invalid_argument("empty geometry reference");
  } catch (const std::exception &) {
    throw std::invalid_argument("PLANE_DIMENSION_GEOMETRY_REF_REQUIRED");
  }
  const auto actual = Wire(stored);
  // Compare the wire strings without narrowing a 64-bit version through double.
  // URLs distinguish separate history branches that reuse GeometryVersion.
  if (expected.at("url") != actual.at("url") ||
      expected.at("version") != actual.at("version"))
    throw std::invalid_argument("PLANE_STALE_GEOMETRY");
}
template <class T>
Ref Publish(Scene &s, std::shared_ptr<T> value, const Text &type,
            const Text &name, const std::vector<Ref> &deps = {}) {
  auto &lib = s.Resources();
  const auto url = lib.AllocateResourceURL();
  iCAX::Resource::CResourceInfo info;
  info.Name = name;
  info.ResourceTypeID = type;
  info.nVersion = 1;
  info.nSchemaVersion = 1;
  info.nMinimumReaderVersion = 1;
  info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
  info.Dependencies = deps;
  if (type == iCAX::Resource::CFlatBufferResource::kResourceTypeName) {
    info.FlatBufferIdentifier = "ICRG";
    info.MediaType = "application/vnd.icax.flatbuffer";
  }
  lib.Set<T>(url, std::move(value), info);
  return {url, lib.GetVersion(url)};
}
Ref PublishMap(Scene &s, const Map &map, const Text &name,
               const std::vector<Ref> &deps) {
  auto data = std::make_shared<iCAX::Resource::CBinaryResource>();
  const auto text = iCAX::Data::VariantSerializer::Serialize(map);
  data->Content.assign(text.begin(), text.end());
  data->DisplayName = name;
  data->FileExtension = ".icaxplane";
  data->Metadata = {{"schema", "icax.plane"}, {"schemaVersion", "1"}};
  return Publish(s, std::move(data), "resource.binary", name, deps);
}
Map ReadMap(Scene &s, const Map &ref) {
  const auto r = Reference(ref);
  const auto data =
      s.Resources().Get<iCAX::Resource::CBinaryResource>(r.URL, r.nVersion);
  if (!data)
    throw std::runtime_error("PLANE_RESOURCE_MISSING");
  return iCAX::Data::VariantSerializer::Deserialize(
             Text(data->Content.begin(), data->Content.end()))
      .To<Map>();
}
Ref Preview(Scene &s, const Map &geometry, const Text &name,
            const Ref &source) {
  iCAX::Render::SRenderPolylineData lines;
  lines.nDataVersion = 1;
  bool first = true;
  for (const auto &value : geometry.at("paths").To<Array>()) {
    const auto path = value.To<Map>();
    const auto ring = ToRing(path.at("points").To<Array>());
    iCAX::Render::SRenderPolylineRangeData range;
    range.nFirstPoint = static_cast<std::uint32_t>(lines.Points.size());
    range.nPointCount = static_cast<std::uint32_t>(ring.size());
    for (auto p : ring) {
      iCAX::Render::SFloat3 v{static_cast<float>(p.X), static_cast<float>(p.Y),
                              0};
      lines.Points.push_back(v);
      if (first) {
        lines.Bounds.Min = lines.Bounds.Max = v;
        first = false;
      } else {
        lines.Bounds.Min.x = std::min(lines.Bounds.Min.x, v.x);
        lines.Bounds.Min.y = std::min(lines.Bounds.Min.y, v.y);
        lines.Bounds.Max.x = std::max(lines.Bounds.Max.x, v.x);
        lines.Bounds.Max.y = std::max(lines.Bounds.Max.y, v.y);
      }
    }
    lines.Ranges.push_back(range);
  }
  return Publish(s,
                 std::make_shared<iCAX::Resource::CFlatBufferResource>(
                     iCAX::Render::MakeRenderGeometryResource(lines)),
                 iCAX::Resource::CFlatBufferResource::kResourceTypeName, name,
                 {source});
}
void AttachRender(iCAX::Database::ITransaction &tx, const ID &id,
                  const Ref &preview) {
  tx.AttachComponent(id, iCAX::Transform::CTransformComponent::S_ClassName);
  tx.AttachComponent(
      id, iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
      {{"GeometryResourceID", preview.URL},
       {"GeometryResourceVersion", static_cast<UInt>(preview.nVersion)},
       {"GeometryKind", 2ull}});
}
UInt Quantity(const Map &p, UInt fallback) {
  if (!p.contains("quantity"))
    return fallback;
  const double q = Number(p, "quantity");
  if (q < 1 || q > 10000 || q != std::floor(q))
    throw std::invalid_argument("PLANE_QUANTITY_MUST_BE_1_TO_10000");
  return static_cast<UInt>(q);
}
std::pair<Text, UInt> ObjectProperties(const Text &kind, const Map &request) {
  const auto name = String(request, "name",
                           kind == "drawing" ? "底图图形"
                           : kind == "parts" ? "零件"
                                             : "板材");
  if (name.empty() || name.size() > 1024)
    throw std::invalid_argument("PLANE_INVALID_NAME");
  return {name, kind == "drawing" ? 1 : Quantity(request)};
}
std::vector<std::shared_ptr<iCAX::Database::IEntity>> Objects(Scene &s) {
  auto entities = s.Database().FilterEntities([](auto e) {
    return static_cast<bool>(Component<CPlaneObjectComponent>(e));
  });
  std::sort(entities.begin(), entities.end(), [](const auto &a, const auto &b) {
    return IdText(a->GetID()) < IdText(b->GetID());
  });
  return entities;
}
Text InputSignature(Scene &s) {
  Array inputs;
  for (const auto &entity : Objects(s)) {
    const auto c = Component<CPlaneObjectComponent>(entity);
    if (c->GetKind() == "parts" || c->GetKind() == "stocks")
      inputs.emplace_back(
          Map{{"id", entity->GetID()},
              {"kind", c->GetKind()},
              {"quantity", c->GetQuantity()},
              {"geometryVersion", c->GetGeometryVersion()},
              {"geometryRef", c->GetGeometryRef()},
              {"approximationBoundMm", c->GetApproximationBoundMm()}});
  }
  // A transient calculation key built from typed ECS facts. It is neither a
  // persisted business object nor a replacement for Repository versions.
  return iCAX::Data::VariantSerializer::Serialize(
      Map{{"parameters", Parameters(*Project(s))}, {"inputs", inputs}});
}
bool ResultInputMatches(Scene &s, const CPlaneResultComponent &r) {
  if (r.GetTaskID().is_nil() || Parameters(r) != Parameters(*Project(s)))
    return false;
  std::map<Text, std::shared_ptr<CPlaneTaskInputComponent>> snapshots;
  for (const auto &entity : Collect<CPlaneTaskInputComponent>(s)) {
    const auto input = Component<CPlaneTaskInputComponent>(entity);
    if (input->GetTaskID() == r.GetTaskID())
      snapshots.emplace(IdText(input->GetObjectID()), input);
  }
  std::size_t count = 0;
  for (const auto &entity : Objects(s)) {
    const auto source = Component<CPlaneObjectComponent>(entity);
    if (source->GetKind() != "parts" && source->GetKind() != "stocks")
      continue;
    ++count;
    const auto found = snapshots.find(IdText(entity->GetID()));
    if (found == snapshots.end())
      return false;
    const auto &input = *found->second;
    if (input.GetKind() != source->GetKind() ||
        input.GetQuantity() != source->GetQuantity() ||
        input.GetGeometryVersion() != source->GetGeometryVersion() ||
        input.GetGeometryRef() != source->GetGeometryRef() ||
        input.GetApproximationBoundMm() != source->GetApproximationBoundMm())
      return false;
  }
  return count == snapshots.size();
}
Map ObjectState(Scene &s, const ID &id, const CPlaneObjectComponent &c) {
  const auto layer = Require<CPlaneLayerComponent>(s, c.GetLayerID());
  Map row{{"id", IdText(id)},
          {"name", c.GetName()},
          {"note", c.GetNote()},
          {"sourceName", c.GetSourceName()},
          {"quantity", c.GetQuantity()},
          {"geometryVersion", std::to_string(c.GetGeometryVersion())},
          {"entityIds", Array{IdText(id)}},
          {"units", c.GetUnits()},
          {"sourceUnits", c.GetSourceUnits()},
          {"unitSource", c.GetUnitSource()},
          {"approximationBoundMm", c.GetApproximationBoundMm()},
          {"width", c.GetWidth()},
          {"height", c.GetHeight()},
          {"holeCount", c.GetHoleCount()},
          {"layerId", IdText(c.GetLayerID())},
          {"layerName", layer->GetName()},
          {"layerColor", layer->GetColor()},
          {"materialRef", Wire(layer->GetMaterialRef())},
          {"sketchId", c.GetSketchID().is_nil() ? Text() : IdText(c.GetSketchID())},
          {"visible", layer->GetVisible()},
          {"locked", layer->GetLocked()},
          {"geometryRef", Wire(c.GetGeometryRef())},
          {"previewRef", Wire(c.GetPreviewRef())}};
  row["contentType"] = Text("curve");
  row["referenceOnly"] = false;
  if (!c.GetSourceRef().empty()) row["sourceRef"] = Wire(c.GetSourceRef());
  const auto entity = s.Database().GetEntity(id);
  if (Component<CPlaneArtworkComponent>(entity)) {
    for (const auto& [key, value] : ArtworkRow(s, id)) row[key] = value;
  } else if (c.GetKind() == "drawing" || c.GetKind() == "sketch-curves")
    for (const auto &[key, value] :
         Geometry(s)->DescribeDrawingClosure(ReadMap(s, c.GetGeometryRef())))
      row[key] = value;
  const auto source = Component<CPlaneMachiningSourceComponent>(entity);
  if (source) {
    const auto sourceId = [](const ID& value) { return value.is_nil() ? Text() : IdText(value); };
    Map link{{"kind", source->GetSourceKind()}, {"sourceObjectId", sourceId(source->GetSourceObjectID())},
      {"partId", sourceId(source->GetPartID())}, {"resultId", sourceId(source->GetResultID())},
      {"boardId", sourceId(source->GetResultBoardID())}, {"placementId", sourceId(source->GetPlacementID())},
      {"instanceIndex", source->GetInstanceIndex()}, {"geometryRef", Wire(source->GetSourceGeometryRef())},
      {"geometryVersion", std::to_string(source->GetSourceGeometryVersion())},
      {"x", source->GetSourceX()}, {"y", source->GetSourceY()}, {"angleDeg", source->GetSourceAngleDeg()},
      {"mirror", source->GetSourceMirror()}, {"reference", source->GetReferenceOnly()}};
    row["machiningSource"] = link;row["sourceKind"] = source->GetSourceKind();
    row["sourceObjectId"] = sourceId(source->GetSourceObjectID());
    row["referenceOnly"] = source->GetReferenceOnly();
    if (source->GetReferenceOnly()) row["reference"] = true;
  }
  return row;
}
Map TaskState(Scene &s, const ID &id, const CPlaneTaskComponent &t) {
  const bool undone =
      !t.GetResultID().is_nil() && !s.Database().GetEntity(t.GetResultID());
  return {{"taskId", IdText(id)},
          {"status", undone ? Text("Undone") : t.GetStatus()},
          {"error", t.GetError()},
          {"parameters", Parameters(t)},
          {"calculationPartGap", t.GetCalculationPartGap()},
          {"calculationStockMargin", t.GetCalculationStockMargin()},
          {"resultId", t.GetResultID().is_nil() || undone
                           ? Text()
                           : IdText(t.GetResultID())}};
}
void SetTaskState(Scene &s, TaskRecord &t) {
  const auto id = ID::from_string(t.Id);
  if (!id || !s.Database().GetEntity(*id))
    return;
  Mutate(
      s, "更新排样任务状态",
      [&](auto &tx) {
        tx.ModifyComponent(
            *id, CPlaneTaskComponent::S_ClassName,
            {{"Status", t.Status},
             {"Error", t.Error},
             {"ResultID",
              t.ResultID.empty() ? ID() : *ID::from_string(t.ResultID)}});
      },
      true);
}
Map ResultState(Scene &s, const ID &id, const CPlaneResultComponent &r) {
  Array boards, unplaced;
  auto boardEntities = Collect<CPlaneResultBoardComponent>(s);
  std::sort(boardEntities.begin(), boardEntities.end(), [](auto a, auto b) {
    return Component<CPlaneResultBoardComponent>(a)->GetSequence() <
           Component<CPlaneResultBoardComponent>(b)->GetSequence();
  });
  for (const auto &e : boardEntities) {
    auto board = Component<CPlaneResultBoardComponent>(e);
    if (board->GetResultID() != id)
      continue;
    auto object = Require<CPlaneObjectComponent>(s, e->GetID());
    auto row = ObjectState(s, e->GetID(), *object);
    row["stockId"] = IdText(board->GetStockID());
    row["physicalStockId"] = board->GetPhysicalStockID();
    row["physicalIndex"] = board->GetPhysicalIndex();
    row["placed"] = board->GetPlacedCount();
    row["utilization"] = board->GetUtilization();
    Array placements;
    for (const auto &pe : Collect<CPlanePlacementComponent>(s)) {
      auto pose = Component<CPlanePlacementComponent>(pe);
      if (pose->GetResultBoardID() != e->GetID())
        continue;
      placements.emplace_back(Map{{"id", IdText(pe->GetID())},
                                  {"partId", IdText(pose->GetPartID())},
                                  {"entityIds", Array{IdText(pe->GetID())}},
                                  {"instanceIndex", pose->GetInstanceIndex()},
                                  {"x", pose->GetX()},
                                  {"y", pose->GetY()},
                                  {"angleDeg", pose->GetAngleDeg()},
                                  {"flip", pose->GetMirror()}});
    }
    row["placements"] = placements;
    boards.emplace_back(std::move(row));
  }
  for (const auto &e : Collect<CPlaneUnplacedComponent>(s)) {
    auto u = Component<CPlaneUnplacedComponent>(e);
    if (u->GetResultID() == id)
      unplaced.emplace_back(Map{{"partId", IdText(u->GetPartID())},
                                {"instanceIndex", u->GetInstanceIndex()},
                                {"reason", u->GetReason()}});
  }
  return {{"id", IdText(id)},
          {"name", r.GetName()},
          {"note", r.GetNote()},
          {"status", r.GetStatus()},
          {"stale", !ResultInputMatches(s, r)},
          {"required", r.GetRequiredCount()},
          {"placed", r.GetPlacedCount()},
          {"unplacedCount", r.GetUnplacedCount()},
          {"unplaced", unplaced},
          {"boardCount", r.GetUsedStockCount()},
          {"utilization", r.GetUtilization()},
          {"stockNetArea", r.GetStockNetArea()},
          {"placedMaterialArea", r.GetPlacedMaterialArea()},
          {"elapsedMs", r.GetElapsedMs()},
          {"valid", r.GetValid()},
          {"geometryStatus", Text(r.GetValid() ? "Valid" : "Invalid")},
          {"validationScope", r.GetValidationScope()},
          {"calculationPartGap", r.GetCalculationPartGap()},
          {"calculationStockMargin", r.GetCalculationStockMargin()},
          {"providerVersion", r.GetProviderVersion()},
          {"stopReason", r.GetStopReason()},
          {"diagnostics", r.GetDiagnostics()},
          {"boards", boards},
          {"parameters", Parameters(r)}};
}
Map Capabilities() {
  return {
      {"enabledMethods",
       Array{Text("PlaneDrawing.Import"), Text("PlaneDrawing.Create"),
             Text("PlaneDrawing.Edit"), Text("PlaneDrawing.ReadGeometry"),
             Text("PlanePart.Import"), Text("PlaneStock.Import"),
             Text("PlaneStock.CreateRectangle"), Text("PlaneObject.Update"),
             Text("PlaneObject.Remove"), Text("PlaneNesting.SetParameters"),
             Text("PlaneNesting.Run"), Text("PlaneNesting.Cancel"),
             Text("PlaneNesting.ReadTask"), Text("PlaneNesting.ReadReport"),
             Text("PlaneSketch.Open"), Text("PlaneSketch.Create"), Text("PlaneSketch.Edit"),
             Text("PlaneSketch.Remove"), Text("PlaneSketch.ReadGeometry"), Text("PlaneSketch.Complete"),
             Text("PlaneSketch.Cancel"), Text("PlaneLayer.Create"), Text("PlaneLayer.Update"),
             Text("PlaneLayer.Assign"), Text("PlaneLayer.Remove"), Text("PlaneLayer.SetCurrent"), Text("PlaneTask.Read"),
             Text("PlaneTask.Cancel"), Text("PlaneLayout.ReadReport"),
             Text("PlaneText.Create"), Text("PlaneText.Update"), Text("PlaneImage.Import"),
             Text("PlaneImage.Update"), Text("PlaneImage.Vectorize"), Text("PlaneMachining.LoadPart"), Text("PlaneMachining.LoadResult")}},
      {"drawingPrimitives", Array{Text("line"), Text("polyline"), Text("rect"),
                                  Text("circle"), Text("arc"), Text("ellipse"), Text("ellipseArc"),
                                  Text("bezier"), Text("bspline"), Text("triangle"), Text("polygon"), Text("star")}},
      {"drawingPrimitiveOptions", Map{{"polygon", Map{{"sides", Map{{"default", 6}, {"min", 3}, {"max", 64}}}}},
                                       {"star", Map{{"tips", Map{{"default", 5}, {"min", 3}, {"max", 64}}}}}}},
      {"polylineSegmentTypes", Array{Text("line"), Text("arc"), Text("ellipseArc"),
                                     Text("bezier"), Text("bspline")}},
      {"drawingEdits", Array{Text("move"), Text("rotate"), Text("scale"),
                             Text("delete"), Text("close"), Text("dimensions"), Text("copy"),
                             Text("mirror"), Text("array-rectangular"), Text("array-polar"), Text("nodes")}}};
}
Map ReadState(Scene &s) {
  Map model{{"drawings", Array{}},
            {"parts", Array{}},
            {"stocks", Array{}},
            {"results", Array{}},
            {"layers", Array{}},
            {"sketches", Array{}}, {"sketchCurves", Array{}},
            {"currentLayerId", IdText(Project(s)->GetDefaultLayerID())},
            {"nestingParameters", Parameters(*Project(s))},
            {"capabilities", Capabilities()}};
  Array layers;
  auto layerEntities = Collect<CPlaneLayerComponent>(s);
  std::sort(layerEntities.begin(), layerEntities.end(), [](const auto& a, const auto& b) {
    const auto left = Component<CPlaneLayerComponent>(a), right = Component<CPlaneLayerComponent>(b);
    return left->GetOrder() != right->GetOrder() ? left->GetOrder() < right->GetOrder() : IdText(a->GetID()) < IdText(b->GetID());
  });
  for (const auto &e : layerEntities) {
    auto layer = Component<CPlaneLayerComponent>(e);
    layers.emplace_back(Map{{"id", IdText(e->GetID())},
                            {"name", layer->GetName()},
                            {"color", layer->GetColor()},
                            {"order", layer->GetOrder()},
                            {"isCurrent", e->GetID() == Project(s)->GetDefaultLayerID()},
                            {"default", e->GetID() == Project(s)->GetDefaultLayerID()},
                            {"materialRef", Wire(layer->GetMaterialRef())},
                            {"visible", layer->GetVisible()},
                            {"locked", layer->GetLocked()}});
  }
  model["layers"] = layers;
  Array sketches;
  for (const auto& entity : Collect<CPlaneSketchComponent>(s))
    sketches.emplace_back(SketchState(s, entity->GetID(), *Component<CPlaneSketchComponent>(entity)));
  model["sketches"] = sketches;
  for (const auto &e : Objects(s)) {
    auto c = Component<CPlaneObjectComponent>(e);
    const auto kind = c->GetKind();
    if (kind == "sketch-curves") {
      auto curve = Require<CPlaneSketchCurveComponent>(s, e->GetID());
      if (!curve->GetDeleted()) {
        auto rows = model.at("sketchCurves").To<Array>();
        auto row = ObjectState(s, e->GetID(), *c);
        row["sketchId"] = IdText(curve->GetSketchID());
        rows.emplace_back(row); model["sketchCurves"] = rows;
      }
      continue;
    }
    if (kind != "drawing" && kind != "parts" && kind != "stocks")
      continue;
    const auto key = kind == "drawing" ? "drawings" : kind;
    auto rows = model.at(key).To<Array>();
    rows.emplace_back(ObjectState(s, e->GetID(), *c));
    model[key] = rows;
  }
  auto results = s.Database().FilterEntities([](auto e) {
    return static_cast<bool>(Component<CPlaneResultComponent>(e));
  });
  std::sort(results.begin(), results.end(), [](auto a, auto b) {
    return IdText(a->GetID()) < IdText(b->GetID());
  });
  Array out;
  for (const auto &e : results)
    out.emplace_back(
        ResultState(s, e->GetID(), *Component<CPlaneResultComponent>(e)));
  model["results"] = out;
  Array tasks;
  for (const auto &e : Collect<CPlaneTaskComponent>(s))
    tasks.emplace_back(
        TaskState(s, e->GetID(), *Component<CPlaneTaskComponent>(e)));
  return {
      {"iplaneCam", model}, {"revision", WireRevision(s)}, {"tasks", tasks}};
}
Map CreateObject(Scene &s, const Text &kind, const Map &request, Map geometry,
                 const Map &source) {
  if (Require<CPlaneLayerComponent>(s, Project(s)->GetDefaultLayerID())->GetLocked())
    throw std::invalid_argument("PLANE_OBJECT_LOCKED");
  const auto id = iCAX::Data::GenerateNewUUID();
  const auto [name, quantity] = ObjectProperties(kind, request);
  std::vector<Ref> deps;
  if (!source.empty())
    deps.push_back(Reference(source));
  const auto geom = PublishMap(s, geometry, name + "几何", deps),
             preview = Preview(s, geometry, name, geom);
  Mutate(s, "添加平面对象", [&](auto &tx) {
    tx.CreateEntity(id);
    tx.AttachComponent(
        id, CPlaneObjectComponent::S_ClassName,
        {{"Kind", kind},
         {"Name", name},
         {"SourceName", String(request, "sourceName")},
         {"Quantity", quantity},
         {"Width", Number(geometry, "width")},
         {"Height", Number(geometry, "height")},
         {"HoleCount", static_cast<UInt>(Number(geometry, "holeCount"))},
         {"SourceUnits", String(geometry, "sourceUnits", "mm")},
         {"UnitSource", String(geometry, "unitSource", "Native")},
         {"ApproximationBoundMm", Number(geometry, "approximationBoundMm")},
         {"LayerID", Project(s)->GetDefaultLayerID()},
         {"GeometryRef", Stored(geom)},
         {"SourceRef", source},
         {"PreviewRef", Stored(preview)}});
    tx.AttachComponent(id,
                       kind == "drawing" ? CPlaneDrawingComponent::S_ClassName
                       : kind == "parts" ? CPlanePartComponent::S_ClassName
                                         : CPlaneStockComponent::S_ClassName);
    AttachRender(tx, id, preview);
    ApplyLayerDisplay(s, tx, id, Project(s)->GetDefaultLayerID());
    Bump(s, tx, kind != "drawing");
  });
  return Receipt(s, IdText(id));
}
Map ImportObject(Scene &s, const Text &kind, const Map &request) {
  if (Require<CPlaneLayerComponent>(s, Project(s)->GetDefaultLayerID())->GetLocked())
    throw std::invalid_argument("PLANE_OBJECT_LOCKED");
  const auto text = String(request, "sourcePath");
  const auto path =
      std::filesystem::path(std::u8string(text.begin(), text.end()));
  if (!path.is_absolute() ||
      path.extension() != L".dxf" && path.extension() != L".DXF" ||
      !std::filesystem::is_regular_file(path) ||
      std::filesystem::file_size(path) > 64 * 1024 * 1024)
    throw std::invalid_argument("PLANE_ABSOLUTE_DXF_FILE_REQUIRED");
  auto p = request;
  const auto fileName = path.filename().u8string();
  p["sourceName"] = Text(fileName.begin(), fileName.end());
  if (!p.contains("name")) {
    const auto stem = path.stem().u8string();
    p["name"] = Text(stem.begin(), stem.end());
  }
  // Validate all user properties before publishing any Embedded resource.
  ObjectProperties(kind, p);
  std::ifstream file(path, std::ios::binary);
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  if (bytes.empty())
    throw std::invalid_argument("PLANE_EMPTY_SOURCE_FILE");
  const auto service =
      s.Services().Resolve<iCAX::CAD::ICadDocumentImportService>();
  const auto exchange = service->Load(path);
  const auto geometry =
      Geometry(s)->ImportGeometry(exchange, kind != "drawing", String(request, "units"));
  std::ifstream check(path, std::ios::binary);
  const std::vector<std::uint8_t> current(
      (std::istreambuf_iterator<char>(check)), {});
  if (current != bytes)
    throw std::invalid_argument("PLANE_SOURCE_CHANGED_DURING_IMPORT");
  auto binary = std::make_shared<iCAX::Resource::CBinaryResource>();
  binary->SourcePath = text;
  binary->DisplayName = text;
  binary->FileExtension = ".dxf";
  binary->Content = std::move(bytes);
  const auto source = Publish(s, binary, "resource.binary", "原始DXF");
  return CreateObject(s, kind, p, geometry, Stored(source));
}
Map UpdateObject(Scene &s, const Map &p, bool remove) {
  const auto id = Id(p);
  const auto kind = String(p, "kind");
  auto entity = s.Database().GetEntity(id);
  auto object = Component<CPlaneObjectComponent>(entity);
  auto result = Component<CPlaneResultComponent>(entity);
  if (!object && !result ||
      object && object->GetKind() != kind &&
          !(kind == "results" && object->GetKind() == "result-board") ||
      result && kind != "results")
    throw std::invalid_argument("PLANE_OBJECT_KIND_MISMATCH");
  if (object) {
    EnsureEditable(s, *object);
    if (object->GetKind() == "sketch-curves") throw std::invalid_argument("PLANE_SKETCH_CURVE_REQUIRED");
  }
  const bool input = kind == "parts" || kind == "stocks";
  iCAX::Data::PropertySet fields;
  for (const auto *key : {"name", "note"})
    if (p.contains(key)) {
      const auto value = String(p, key);
      if (value.size() > 4096 || Text(key) == "name" && value.empty())
        throw std::invalid_argument("PLANE_INVALID_PROPERTY");
      fields.emplace(Text(key) == "name" ? "Name" : "Note", value);
    }
  if (p.contains("quantity")) {
    if (!input)
      throw std::invalid_argument("PLANE_QUANTITY_NOT_APPLICABLE");
    fields.emplace("Quantity", Quantity(p));
  }
  Mutate(s, remove ? "删除平面对象" : "修改平面参数", [&](auto &tx) {
    if (remove) {
      if (object && object->GetKind() == "result-board")
        throw std::invalid_argument("PLANE_REMOVE_RESULT_PLAN_REQUIRED");
      if (result) {
        for (const auto &e : Collect<CPlaneResultBoardComponent>(s))
          if (Component<CPlaneResultBoardComponent>(e)->GetResultID() == id)
            tx.DisposeEntity(e->GetID());
        for (const auto &e : Collect<CPlanePlacementComponent>(s))
          if (Component<CPlanePlacementComponent>(e)->GetResultID() == id)
            tx.DisposeEntity(e->GetID());
        for (const auto &e : Collect<CPlaneUnplacedComponent>(s))
          if (Component<CPlaneUnplacedComponent>(e)->GetResultID() == id)
            tx.DisposeEntity(e->GetID());
      }
      if (input) DisposeOwnedSketch(s, tx, id);
      tx.DisposeEntity(id);
    } else
      tx.ModifyComponent(id,
                         result ? CPlaneResultComponent::S_ClassName
                                : CPlaneObjectComponent::S_ClassName,
                         fields);
    Bump(s, tx, input);
  });
  return Receipt(s, IdText(id));
}
N::Request NestingInput(Scene &s, TaskRecord &task) {
  const auto parameters = Parameters(*Project(s));
  N::Request request;
  request.InputKey = InputSignature(s);
  request.PartGap = Number(parameters, "partGap");
  request.StockMargin = Number(parameters, "stockMargin");
  request.TimeLimitSeconds = Number(parameters, "timeLimitSeconds", 1);
  const double step = Number(parameters, "rotationStepDeg", 90);
  std::size_t total = 0;
  double partBound = 0, stockBound = 0;
  for (const auto &e : Objects(s)) {
    auto c = Component<CPlaneObjectComponent>(e);
    if (c->GetKind() != "parts" && c->GetKind() != "stocks")
      continue;
    auto geometry = ReadMap(s, c->GetGeometryRef());
    const auto id = IdText(e->GetID());
    task.Geometries[id] = geometry;
    task.GeometryRefs[id] = Reference(c->GetGeometryRef());
    task.Dependencies.push_back(Reference(c->GetGeometryRef()));
    if (!geometry.contains("outer"))
      throw std::invalid_argument("PLANE_MATERIAL_MISSING_OUTER");
    std::vector<N::Ring> holes;
    for (const auto &h : geometry.at("holes").To<Array>())
      holes.push_back(ToRing(h.To<Array>()));
    total += static_cast<std::size_t>(c->GetQuantity());
    if (total > 10000)
      throw std::invalid_argument("PLANE_TASK_QUANTITY_LIMIT");
    if (c->GetKind() == "parts") {
      partBound = std::max(partBound, c->GetApproximationBoundMm());
      N::Part part;
      part.Id = id;
      part.Outer = ToRing(geometry.at("outer").To<Array>());
      part.Holes = holes;
      part.Quantity = static_cast<int>(c->GetQuantity());
      part.AllowMirror = parameters.at("allowMirror").To<bool>();
      for (double a = 0; a < 360 - 1e-8; a += step)
        part.AnglesDeg.push_back(a);
      request.Parts.push_back(std::move(part));
    } else {
      stockBound = std::max(stockBound, c->GetApproximationBoundMm());
      for (UInt i = 0; i < c->GetQuantity(); ++i)
        request.Stocks.push_back({id + ":" + std::to_string(i + 1),
                                  ToRing(geometry.at("outer").To<Array>()),
                                  holes});
    }
  }
  if (request.Parts.empty() || request.Stocks.empty())
    throw std::invalid_argument("PLANE_PARTS_AND_STOCKS_REQUIRED");
  request.PartGap += 2 * partBound + 1.1e-6;
  request.StockMargin += partBound + stockBound + 1.1e-6;
  return request;
}
Map Run(Scene &s) {
  auto runtime = Runtime(s);
  if (runtime->Stopped)
    throw std::runtime_error("PLANE_SCENE_STOPPING");
  for (const auto &[_, t] : runtime->Tasks)
    if (t->Status == "Running")
      throw std::invalid_argument("PLANE_TASK_ALREADY_RUNNING");
  while (runtime->Tasks.size() >= 32) {
    auto it = std::find_if(
        runtime->Tasks.begin(), runtime->Tasks.end(),
        [](const auto &pair) { return pair.second->Status != "Running"; });
    if (it == runtime->Tasks.end())
      break;
    runtime->Tasks.erase(it);
  }
  auto task = std::make_shared<TaskRecord>();
  task->Id = IdText(iCAX::Data::GenerateNewUUID());
  task->InputRevision = Project(s)->GetNestingRevision();
  task->Input = NestingInput(s, *task);
  const auto service =
      s.Services().Resolve<iCAX::GeometryServices::IPlaneNestingService>();
  auto input = task->Input;
  Array inputs;
  for (const auto &[id, _] : task->Geometries)
    inputs.emplace_back(*ID::from_string(id));
  const auto id = *ID::from_string(task->Id);
  Mutate(
      s, "启动排样任务",
      [&](auto &tx) {
        tx.CreateEntity(id);
        tx.AttachComponent(
            id, CPlaneTaskComponent::S_ClassName,
            {{"InputRevision", task->InputRevision},
             {"InputObjectIDs", inputs},
             {"PartGap", Project(s)->GetPartGap()},
             {"StockMargin", Project(s)->GetStockMargin()},
             {"RotationStepDeg", Project(s)->GetRotationStepDeg()},
             {"TimeLimitSeconds", Project(s)->GetTimeLimitSeconds()},
             {"AllowMirror", Project(s)->GetAllowMirror()},
             {"CalculationPartGap", task->Input.PartGap},
             {"CalculationStockMargin", task->Input.StockMargin}});
        for (const auto &input : inputs) {
          const auto objectID = input.To<ID>();
          const auto source = Require<CPlaneObjectComponent>(s, objectID);
          const auto snapshotID = iCAX::Data::GenerateNewUUID();
          tx.CreateEntity(snapshotID);
          tx.AttachComponent(
              snapshotID, CPlaneTaskInputComponent::S_ClassName,
              {{"TaskID", id},
               {"ObjectID", objectID},
               {"Kind", source->GetKind()},
               {"Quantity", source->GetQuantity()},
               {"GeometryVersion", source->GetGeometryVersion()},
               {"ApproximationBoundMm", source->GetApproximationBoundMm()},
               {"GeometryRef", source->GetGeometryRef()}});
        }
      },
      true);
  try {
    task->Work = iCAX::Tasks::Run(
        [service,
         input = std::move(input)](iCAX::Tasks::CancellationToken token) {
          return service->Solve(
              input, [token]() { return token.IsCancellationRequested(); });
        },
        task->Cancellation.Token());
  } catch (const std::exception &e) {
    task->Status = "Failed";
    task->Error = e.what();
    SetTaskState(s, *task);
    throw;
  }
  runtime->Tasks.emplace(task->Id, task);
  return TaskState(s, id, *Require<CPlaneTaskComponent>(s, id));
}
void PublishResult(Scene &s, TaskRecord &task, const N::Result &answer) {
  const auto resultID = iCAX::Data::GenerateNewUUID();
  struct Visual {
    ID Id;
    Text Kind, Name, Role;
    Ref Geometry, Preview;
    iCAX::Data::PropertySet Properties;
    double Width = 0, Height = 0;
    UInt HoleCount = 0;
  };
  std::vector<Visual> visuals;
  UInt sequence = 0;
  for (const auto &usage : answer.Boards) {
    const auto pos = usage.PhysicalStockId.rfind(':');
    if (pos == Text::npos)
      throw std::runtime_error("PLANE_INVALID_PHYSICAL_STOCK_ID");
    const auto stockID = usage.PhysicalStockId.substr(0, pos);
    if (!task.Geometries.contains(stockID))
      throw std::runtime_error("PLANE_UNKNOWN_RESULT_STOCK");
    const auto boardID = iCAX::Data::GenerateNewUUID();
    const auto &stockGeometry = task.Geometries.at(stockID);
    const auto boardGeometry = task.GeometryRefs.at(stockID);
    const auto boardPreview =
        Preview(s, stockGeometry, "结果板材", boardGeometry);
    Visual board{
        boardID,
        "result-board",
        Text("板 ") + std::to_string(++sequence),
        CPlaneResultBoardComponent::S_ClassName,
        boardGeometry,
        boardPreview,
        {{"ResultID", resultID},
         {"StockID", *ID::from_string(stockID)},
         {"PhysicalStockID", usage.PhysicalStockId},
         {"PhysicalIndex", static_cast<UInt>(std::stoull(
                               usage.PhysicalStockId.substr(pos + 1)))},
         {"Sequence", sequence},
         {"PlacedCount", static_cast<UInt>(usage.PlacedCount)},
         {"StockNetArea", usage.StockNetArea},
         {"PlacedMaterialArea", usage.PlacedMaterialArea},
         {"Utilization", usage.Utilization}}};
    board.Width = Number(stockGeometry, "width");
    board.Height = Number(stockGeometry, "height");
    board.HoleCount = static_cast<UInt>(Number(stockGeometry, "holeCount"));
    visuals.push_back(std::move(board));
    for (const auto &pose : answer.Placements)
      if (pose.PhysicalStockId == usage.PhysicalStockId) {
        if (!task.Geometries.contains(pose.PartId))
          throw std::runtime_error("PLANE_UNKNOWN_RESULT_PART");
        const auto id = iCAX::Data::GenerateNewUUID();
        const auto geometry =
                       Geometry(s)->PlaceGeometry(task.Geometries.at(pose.PartId), pose),
                   source = task.Geometries.at(pose.PartId);
        const auto gr = task.GeometryRefs.at(pose.PartId),
                   pr = Preview(s, geometry, "排入零件", gr);
        Visual placed{id,
                      "result-placement",
                      "排入零件",
                      CPlanePlacementComponent::S_ClassName,
                      gr,
                      pr,
                      {{"ResultID", resultID},
                       {"ResultBoardID", boardID},
                       {"PartID", *ID::from_string(pose.PartId)},
                       {"InstanceIndex", static_cast<UInt>(pose.InstanceIndex)},
                       {"X", pose.X},
                       {"Y", pose.Y},
                       {"AngleDeg", pose.AngleDeg},
                       {"Mirror", pose.Flip}}};
        placed.Width = Number(source, "width");
        placed.Height = Number(source, "height");
        placed.HoleCount = static_cast<UInt>(Number(source, "holeCount"));
        visuals.push_back(std::move(placed));
      }
  }
  const Text status = answer.UnplacedParts.empty() ? "Completed" : "Partial";
  Array diagnostics;
  for (const auto &d : answer.Checks.Diagnostics)
    diagnostics.emplace_back(d);
  const auto parameters = Project(s);
  Mutate(s, "提交排样结果", [&](auto &tx) {
    tx.CreateEntity(resultID);
    tx.AttachComponent(
        resultID, CPlaneResultComponent::S_ClassName,
        {{"Name", Text("排样方案")},
         {"InputRevision", task.InputRevision},
         {"TaskID", *ID::from_string(task.Id)},
         {"Status", status},
         {"RequiredCount", static_cast<UInt>(answer.Statistics.RequiredCount)},
         {"PlacedCount", static_cast<UInt>(answer.Statistics.PlacedCount)},
         {"UnplacedCount", static_cast<UInt>(answer.Statistics.UnplacedCount)},
         {"UsedStockCount",
          static_cast<UInt>(answer.Statistics.UsedStockCount)},
         {"StockNetArea", answer.Statistics.StockNetArea},
         {"PlacedMaterialArea", answer.Statistics.PlacedMaterialArea},
         {"Utilization", answer.Statistics.Utilization},
         {"ElapsedMs", answer.Statistics.ElapsedMs},
         {"Valid", answer.Valid},
         {"DemandConserved", answer.Checks.DemandConserved},
         {"StockConserved", answer.Checks.StockConserved},
         {"OrientationsValid", answer.Checks.OrientationsValid},
         {"ValidationScope", answer.Checks.Scope},
         {"ProviderVersion", answer.ProviderVersion},
         {"StopReason", answer.StopReason},
         {"Diagnostics", diagnostics},
         {"PartGap", parameters->GetPartGap()},
         {"StockMargin", parameters->GetStockMargin()},
         {"RotationStepDeg", parameters->GetRotationStepDeg()},
         {"TimeLimitSeconds", parameters->GetTimeLimitSeconds()},
         {"AllowMirror", parameters->GetAllowMirror()}});
    tx.ModifyComponent(resultID, CPlaneResultComponent::S_ClassName,
                       {{"CalculationPartGap", task.Input.PartGap},
                        {"CalculationStockMargin", task.Input.StockMargin}});
    for (const auto &v : visuals) {
      tx.CreateEntity(v.Id);
      tx.AttachComponent(v.Id, CPlaneObjectComponent::S_ClassName,
                         {{"Kind", v.Kind},
                          {"Name", v.Name},
                          {"Width", v.Width},
                          {"Height", v.Height},
                          {"HoleCount", v.HoleCount},
                          {"LayerID", parameters->GetDefaultLayerID()},
                          {"GeometryRef", Stored(v.Geometry)},
                          {"PreviewRef", Stored(v.Preview)}});
      tx.AttachComponent(v.Id, v.Role, v.Properties);
      AttachRender(tx, v.Id, v.Preview);
      ApplyLayerDisplay(s, tx, v.Id, parameters->GetDefaultLayerID());
    }
    for (const auto &u : answer.UnplacedParts) {
      const auto id = iCAX::Data::GenerateNewUUID();
      tx.CreateEntity(id);
      tx.AttachComponent(id, CPlaneUnplacedComponent::S_ClassName,
                         {{"ResultID", resultID},
                          {"PartID", *ID::from_string(u.PartId)},
                          {"InstanceIndex", static_cast<UInt>(u.InstanceIndex)},
                          {"Reason", u.Reason}});
    }
    Bump(s, tx, false);
  });
  task.ResultID = IdText(resultID);
  task.Status = status;
}
Map CancelNesting(Scene& s, const Map& p) {
  const auto runtime = Runtime(s); const auto id = Id(p, "taskId");
  const auto task = Require<CPlaneTaskComponent>(s, id);
  if (runtime->Tasks.contains(IdText(id)) && task->GetStatus() == "Running")
    runtime->Tasks.at(IdText(id))->Cancellation.Cancel();
  return TaskState(s, id, *task);
}
void EnsureEditable(Scene& s, const CPlaneObjectComponent& object) {
  if (Require<CPlaneLayerComponent>(s, object.GetLayerID())->GetLocked())
    throw std::invalid_argument("PLANE_OBJECT_LOCKED");
}
} // namespace detail
using namespace detail;
void InitializeScene(Scene &s) {
  const auto meta = s.Database().GetMetaEntity();
  if (!Component<CPlaneSceneComponent>(meta) ||
      !Component<CPlaneProjectComponent>(meta))
    Mutate(
        s, "初始化平面产品",
        [&](auto &tx) {
          if (!Component<CPlaneSceneComponent>(meta))
            tx.AttachComponent(meta->GetID(),
                               CPlaneSceneComponent::S_ClassName);
          if (!Component<CPlaneProjectComponent>(meta)) {
            const auto layer = iCAX::Data::GenerateNewUUID();
            tx.CreateEntity(layer);
            tx.AttachComponent(layer, CPlaneLayerComponent::S_ClassName, {{"Name", Text("图层 1")}});
            tx.AttachComponent(meta->GetID(),
                               CPlaneProjectComponent::S_ClassName,
                               {{"DefaultLayerID", layer}});
          }
        },
        true);
  InitializeLayers(s);
  auto root = Require<CPlaneSceneComponent>(s, meta->GetID());
  if (!root->Runtime)
    root->Runtime = std::make_shared<SceneRuntime>();
  InitializeEditTasks(s);
  // GeometryKind is a shared nonpersistent render hint. Restore it from this
  // product's formal geometry contract after loading the persistent objects.
  for (const auto &entity : Objects(s)) {
    const auto render =
        Component<iCAX::RenderInteraction::CRenderInstanceComponent>(entity);
    const UInt geometryKind = Component<CPlaneArtworkComponent>(entity) ? 1ull : 2ull;
    if (render && render->GetGeometryKind() != geometryKind)
      Mutate(
          s, "恢复二维显示类型",
          [&](auto &tx) {
            tx.ModifyComponent(
                entity->GetID(),
                iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
                {{"GeometryKind", geometryKind}});
          },
          true);
  }
  for (const auto &e : Collect<CPlaneTaskComponent>(s))
    if (Component<CPlaneTaskComponent>(e)->GetStatus() == "Running" &&
        !root->Runtime->Tasks.contains(IdText(e->GetID())))
      Mutate(
          s, "恢复中断的排样任务",
          [&](auto &tx) {
            tx.ModifyComponent(
                e->GetID(), CPlaneTaskComponent::S_ClassName,
                {{"Status", Text("Interrupted")},
                 {"Error", Text("上次运行未完成，尚未提交结果")}});
          },
          true);
}
void PumpTasks(Scene &s) {
  auto runtime = Runtime(s);
  if (runtime->Stopped || s.IsStopRequested())
    return;
  SynchronizeLayerDisplay(s);
  PumpEditTasks(s);
  for (const auto &[_, t] : runtime->Tasks) {
    if (t->Status != "Running" || !t->Work.Valid() || !t->Work.IsCompleted())
      continue;
    try {
      auto result = t->Work.Result();
      if (t->Cancellation.IsCancellationRequested())
        t->Status = "Cancelled";
      else if (t->Input.InputKey != InputSignature(s)) {
        t->Status = "Stale";
        t->Error = "排样输入已修改，结果未提交";
      } else if (!result.Valid || result.State == N::Status::Cancelled ||
                 result.State == N::Status::Failed ||
                 result.State == N::Status::InvalidInput ||
                 result.State == N::Status::Unsupported ||
                 result.State == N::Status::ResourceLimit) {
        t->Status = "Failed";
        t->Error =
            result.Diagnostic.empty() ? result.StopReason : result.Diagnostic;
      } else
        PublishResult(s, *t, result);
    } catch (const iCAX::Tasks::TaskCanceledException &) {
      t->Status = "Cancelled";
    } catch (const std::exception &e) {
      t->Status = "Failed";
      t->Error = e.what();
    }
    SetTaskState(s, *t);
  }
}
void StopTasks(Scene &s) {
  const auto component =
      Component<CPlaneSceneComponent>(s.Database().GetMetaEntity());
  if (component)
    StopTasks(s, component->Runtime);
}
void StopTasks(Scene &s, const std::shared_ptr<SceneRuntime> &runtime) {
  StopEditTasks(s);
  if (!runtime)
    return;
  runtime->Stopped = true;
  for (const auto &[_, t] : runtime->Tasks)
    t->Cancellation.Cancel();
  for (const auto &[_, t] : runtime->Tasks)
    if (t->Work.Valid()) {
      t->Work.Wait();
      if (t->Status == "Running") {
        t->Status = "Cancelled";
        SetTaskState(s, *t);
      }
    }
  runtime->Stopped = false;
}
} // namespace iCAX::PlaneCAM
