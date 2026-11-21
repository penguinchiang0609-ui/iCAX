#pragma once
#include "PlaneCAM.h"
#include "PlaneComponents.h"
#include "PlaneGeometry.h"
#include "GeometryServices/PlaneGeometryService.h"
#include "GeometryServices/PlaneArtworkService.h"
#include "ProjectFile/ProjectDocument.h"
#include "Resources/ResourceLibrary.h"
#include "Services/ServiceProvider.h"
#include "Task/Task.h"
#include <algorithm>
#include <functional>

namespace iCAX::PlaneCAM::detail {
using Scene = iCAX::Project::ISceneContext;
using Ref = iCAX::Resource::CResourceReference;
namespace N = iCAX::GeometryServices::PlaneNesting;

template<class T> std::shared_ptr<T> Component(const std::shared_ptr<iCAX::Database::IEntity>& entity) {
  return entity ? std::dynamic_pointer_cast<T>(entity->GetComponent(T::S_ClassName)) : nullptr;
}
template<class T> std::shared_ptr<T> Require(Scene& scene, const ID& id) {
  auto component = Component<T>(scene.Database().GetEntity(id));
  if (!component) throw std::invalid_argument("PLANE_OBJECT_NOT_FOUND");
  return component;
}
template<class T> auto Collect(Scene& scene) {
  return scene.Database().FilterEntities([](auto entity) { return static_cast<bool>(Component<T>(entity)); });
}
template<class T> Map Parameters(const T& p) {
  return {{"partGap", p.GetPartGap()}, {"stockMargin", p.GetStockMargin()},
    {"rotationStepDeg", p.GetRotationStepDeg()}, {"timeLimitSeconds", p.GetTimeLimitSeconds()},
    {"allowMirror", p.GetAllowMirror()}};
}
template<class F> void Mutate(Scene& scene, const Text& label, F action, bool baseline = false) {
  auto& repository = scene.Database();
  auto undo = repository.BeginUndoCommand(label);
  auto& transaction = repository.BeginTransaction(label);
  try { action(transaction); }
  catch (...) { repository.CancelTransaction(transaction); throw; }
  Text error;
  if (!repository.CommitTransaction(transaction, error)) throw std::runtime_error("PLANE_COMMIT_FAILED: " + error);
  if (baseline) undo->EndWithoutHistory(); else undo->End();
}
inline auto Geometry(Scene& scene) {
  return scene.Services().Resolve<iCAX::GeometryServices::IPlaneGeometryService>();
}
inline void RequireWorktableRequest(const Map& request) {
  if (request.contains("sketchId") || request.contains("ownerId"))
    throw std::invalid_argument("PLANE_ARTWORK_WORKTABLE_REQUIRED");
}

Text IdText(const ID&);
ID Id(const Map&, const Text& key = "id");
std::shared_ptr<CPlaneProjectComponent> Project(Scene&);
UInt Next(UInt);
Text WireRevision(Scene&);
void CheckExpected(Scene&, const Map&);
void Bump(Scene&, iCAX::Database::ITransaction&, bool nesting);
Map Receipt(Scene&, const Text& id = "");
Map Stored(const Ref&);
Ref Reference(const Map&);
Map Wire(const Map&);
void CheckExpectedGeometry(const Map& request, const Map& stored);
void EnsureEditable(Scene&, const CPlaneObjectComponent&);
void InitializeLayers(Scene&);
void SynchronizeLayerDisplay(Scene&);
void ApplyLayerDisplay(Scene&, iCAX::Database::ITransaction&, const ID& object,
                       const ID& layer, const Map* material = nullptr,
                       const bool* visible = nullptr, const bool* locked = nullptr,
                       const bool* deleted = nullptr, const Text* color = nullptr);
Ref PublishMap(Scene&, const Map&, const Text&, const std::vector<Ref>& deps = {});
Map ReadMap(Scene&, const Map&);
Ref Preview(Scene&, const Map&, const Text&, const Ref&);
void AttachRender(iCAX::Database::ITransaction&, const ID&, const Ref&);
UInt Quantity(const Map&, UInt fallback = 1);
std::pair<Text, UInt> ObjectProperties(const Text&, const Map&);
std::vector<std::shared_ptr<iCAX::Database::IEntity>> Objects(Scene&);
Text InputSignature(Scene&);
Map ObjectState(Scene&, const ID&, const CPlaneObjectComponent&);
Map ReadState(Scene&);
Map TaskState(Scene&, const ID&, const CPlaneTaskComponent&);
Map ResultState(Scene&, const ID&, const CPlaneResultComponent&);
Map CreateObject(Scene&, const Text& kind, const Map& request, Map geometry, const Map& source = {});
Map ImportObject(Scene&, const Text& kind, const Map& request);
Map UpdateObject(Scene&, const Map&, bool remove);
Map Run(Scene&);
Map CancelNesting(Scene&, const Map&);

// Sketch state and curve ownership live in EC. These helpers only coordinate
// domain relationships; geometry algorithms remain in the injected service.
Map SketchState(Scene&, const ID&, const CPlaneSketchComponent&);
void DisposeOwnedSketch(Scene&, iCAX::Database::ITransaction&, const ID& owner);
void RequireSketchCurveEditing(Scene&, const ID& curve);
void UpdateGeometry(iCAX::Database::ITransaction&, const ID&, const CPlaneObjectComponent&,
                    const Map&, const Ref& geometry, const Ref& preview);
Map EditCurveObject(Scene&, const Map&, const Text& requiredKind);
Map ReadCurveGeometry(Scene&, const Map&, const Text& requiredKind);
std::vector<ID> SketchCurveIDs(Scene&, const ID& sketch);
Map PublishSketchCompletion(Scene&, const ID& sketch, const Map& geometry);
Map PublishCurveCopies(Scene&, const ID& source, const Array& geometries, bool array);
struct GeometryTaskInput {
  ID ObjectID;
  Map GeometryRef;
  UInt GeometryVersion = 0;
  ID LayerID;
  UInt LayerVersion = 0;
};
struct GeometryTaskSpec {
  Text Kind;
  ID TargetID, OwnerID, SketchID;
  Map OwnerGeometryRef;
  ID LayerID;
  UInt LayerVersion = 0;
  bool RequireAbsentTarget = false;
  Map Request;
  std::vector<GeometryTaskInput> Inputs;
};
Map QueueGeometryTask(Scene&, GeometryTaskSpec, std::function<Map(iCAX::Tasks::CancellationToken)>);
Map ReadGeometryTask(Scene&, const Map&);
Map CancelGeometryTask(Scene&, const Map&);
void InitializeEditTasks(Scene&);
void PumpEditTasks(Scene&);
void StopEditTasks(Scene&);
Map ArtworkRow(Scene&, const ID&);
Map ArtworkMaterial(Scene&, const ID&, const Text& color);
Map ReadArtworkGeometry(Scene&, const ID&);
Map EditArtwork(Scene&, const Map&);
Map CreateText(Scene&, const Map&, bool update);
Map ImportImage(Scene&, const Map&);
Map UpdateImage(Scene&, const Map&);
Map VectorizeImage(Scene&, const Map&);
Map PublishArtworkTask(Scene&, const GeometryTaskSpec&, const Map&);
Map LoadMachiningPart(Scene&, const Map&);
Map LoadMachiningResult(Scene&, const Map&);
Map PublishMachiningTask(Scene&, const GeometryTaskSpec&, const Map&);
void CopyMachiningSource(Scene&, iCAX::Database::ITransaction&, const ID& source, const ID& target);
} // namespace iCAX::PlaneCAM::detail
