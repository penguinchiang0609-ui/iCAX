#include "PlaneSDODispatch.h"
#include "RenderData/RenderResource.h"
#include "RenderInteraction/RenderInteractionComponents.h"
#include "Resources/FlatBufferResource.h"
#include <array>
#include <cctype>
#include <set>

namespace iCAX::PlaneCAM::detail {
namespace {
constexpr std::array<const char*, 8> layerColors{
  "#22C55E", "#06B6D4", "#3B82F6", "#F59E0B",
  "#14B8A6", "#84CC16", "#8B5CF6", "#EAB308"};
Text Color(const Map& request, const Text& fallback) {
  auto color = String(request, "color", fallback);
  if (color.size() != 7 || color.front() != '#' ||
      !std::all_of(color.begin() + 1, color.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }))
    throw std::invalid_argument("PLANE_INVALID_LAYER_COLOR");
  std::transform(color.begin() + 1, color.end(), color.begin() + 1,
    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return color;
}
Map LayerMaterial(Scene& scene, const Text& color, const Text& name) {
  iCAX::Render::SRenderMaterialData material;
  material.nDataVersion = 1;
  material.nColorRGBA = (static_cast<std::uint32_t>(std::stoul(color.substr(1), nullptr, 16)) << 8) | 0xffu;
  material.nAmbientRGBA = material.nColorRGBA;
  material.nLineWidth = 1.f;
  auto payload = std::make_shared<iCAX::Resource::CFlatBufferResource>(iCAX::Render::MakeRenderMaterialResource(material));
  auto& library = scene.Resources();
  const auto url = library.AllocateResourceURL();
  iCAX::Resource::CResourceInfo info;
  info.Name = name + "显示材质";
  info.ResourceTypeID = iCAX::Resource::CFlatBufferResource::kResourceTypeName;
  info.FlatBufferIdentifier = iCAX::Render::kRenderMaterialIdentifier;
  info.MediaType = "application/vnd.icax.flatbuffer";
  info.nVersion = info.nSchemaVersion = info.nMinimumReaderVersion = 1;
  info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
  library.Set<iCAX::Resource::CFlatBufferResource>(url, std::move(payload), info);
  return Stored({url, library.GetVersion(url)});
}
UInt NextLayerOrder(Scene& scene) {
  UInt order = 0;
  for (const auto& entity : Collect<CPlaneLayerComponent>(scene))
    order = std::max(order, Next(Component<CPlaneLayerComponent>(entity)->GetOrder()));
  return order;
}
}
void ApplyLayerDisplay(Scene& scene, iCAX::Database::ITransaction& tx, const ID& object,
    const ID& layerID, const Map* material, const bool* visible, const bool* locked, const bool* deleted, const Text* color) {
  const auto layer = Require<CPlaneLayerComponent>(scene, layerID);
  const auto entity = scene.Database().GetEntity(object);
  const auto artwork = Component<CPlaneArtworkComponent>(entity);
  const auto artworkMaterial = artwork ? ArtworkMaterial(scene, object, color ? *color : layer->GetColor()) : Map{};
  const auto reference = Reference(artwork ? artworkMaterial : material ? *material : layer->GetMaterialRef());
  if (artwork && artworkMaterial != artwork->GetMaterialRef())
    tx.ModifyComponent(object, CPlaneArtworkComponent::S_ClassName,
      {{"MaterialRef", artworkMaterial}, {"MaterialColor", Component<CPlaneTextComponent>(entity)
          ? color ? *color : layer->GetColor() : Text("#FFFFFF")}});
  const auto curve = Component<CPlaneSketchCurveComponent>(entity);
  const auto source = Component<CPlaneMachiningSourceComponent>(entity);
  const bool tombstone = deleted ? *deleted : curve && curve->GetDeleted();
  tx.ModifyComponent(object, iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
    {{"MaterialResourceID", reference.URL}, {"MaterialResourceVersion", static_cast<UInt>(reference.nVersion)},
     {"Visible", !tombstone && (visible ? *visible : layer->GetVisible())},
     {"Selectable", !tombstone && !(source && source->GetReferenceOnly()) && !(locked ? *locked : layer->GetLocked())}});
}
void SynchronizeLayerDisplay(Scene& scene) {
  std::vector<std::pair<ID, ID>> changed;
  for (const auto& entity : Objects(scene)) {
    const auto object = Component<CPlaneObjectComponent>(entity);
    const auto layer = Require<CPlaneLayerComponent>(scene, object->GetLayerID());
    const auto render = Component<iCAX::RenderInteraction::CRenderInstanceComponent>(entity);
    if (!render || layer->GetMaterialRef().empty()) continue;
    const auto artwork = Component<CPlaneArtworkComponent>(entity);
    const auto reference = Reference(artwork ? artwork->GetMaterialRef() : layer->GetMaterialRef());
    const auto curve = Component<CPlaneSketchCurveComponent>(entity);
    const bool deleted = curve && curve->GetDeleted();
    const auto source = Component<CPlaneMachiningSourceComponent>(entity);
    const bool selectable = !deleted && !layer->GetLocked() && !(source && source->GetReferenceOnly());
    const bool artworkColorChanged = artwork && artwork->GetMaterialColor() !=
        (Component<CPlaneTextComponent>(entity) ? layer->GetColor() : Text("#FFFFFF"));
    if (render->GetMaterialResourceID() != reference.URL || render->GetMaterialResourceVersion() != reference.nVersion ||
        render->GetVisible() != (!deleted && layer->GetVisible()) || render->GetSelectable() != selectable || artworkColorChanged)
      changed.emplace_back(entity->GetID(), object->GetLayerID());
  }
  if (changed.empty()) return;
  Mutate(scene, "同步图层显示投影", [&](auto& tx) {
    for (const auto& [object, layer] : changed) ApplyLayerDisplay(scene, tx, object, layer);
  }, true);
}
void InitializeLayers(Scene& scene) {
  const auto project = Project(scene);
  auto layers = Collect<CPlaneLayerComponent>(scene);
  std::sort(layers.begin(), layers.end(), [&](const auto& a, const auto& b) {
    if (a->GetID() == project->GetDefaultLayerID()) return b->GetID() != a->GetID();
    if (b->GetID() == project->GetDefaultLayerID()) return false;
    return IdText(a->GetID()) < IdText(b->GetID());
  });
  const bool migrate = project->GetLayerCatalogVersion() < 1;
  struct Prepared { ID Id; Text Name, Color; UInt Order; Map Material; bool Create; };
  std::vector<Prepared> prepared;
  std::set<Text> names;
  for (const auto& entity : layers) names.insert(Component<CPlaneLayerComponent>(entity)->GetName());
  for (std::size_t index = 0; index < layers.size(); ++index) {
    const auto layer = Component<CPlaneLayerComponent>(layers[index]);
    if (!migrate && !layer->GetMaterialRef().empty()) continue;
    const auto color = migrate ? Text(layerColors[index % layerColors.size()]) : layer->GetColor();
    prepared.push_back({layers[index]->GetID(), layer->GetName(), color,
      migrate ? static_cast<UInt>(index) : layer->GetOrder(), LayerMaterial(scene, color, layer->GetName()), false});
  }
  if (migrate) for (std::size_t index = layers.size(); index < layerColors.size(); ++index) {
    std::size_t number = index + 1;
    while (names.contains("图层 " + std::to_string(number))) ++number;
    auto name = "图层 " + std::to_string(number); names.insert(name);
    const auto color = Text(layerColors[index]);
    prepared.push_back({iCAX::Data::GenerateNewUUID(), name, color, static_cast<UInt>(index), LayerMaterial(scene, color, name), true});
  }
  if (!prepared.empty() || migrate) Mutate(scene, "初始化彩色图层", [&](auto& tx) {
    for (const auto& value : prepared) {
      const iCAX::Data::PropertySet fields{{"Name", value.Name}, {"Color", value.Color},
        {"Order", value.Order}, {"MaterialRef", value.Material}};
      if (value.Create) { tx.CreateEntity(value.Id);tx.AttachComponent(value.Id, CPlaneLayerComponent::S_ClassName, fields); }
      else {
        tx.ModifyComponent(value.Id, CPlaneLayerComponent::S_ClassName, fields);
        for (const auto& object : Objects(scene)) if (Component<CPlaneObjectComponent>(object)->GetLayerID() == value.Id)
          ApplyLayerDisplay(scene, tx, object->GetID(), value.Id, &value.Material);
      }
    }
    if (migrate) tx.ModifyComponent(scene.Database().GetMetaEntity()->GetID(), CPlaneProjectComponent::S_ClassName,
      {{"LayerCatalogVersion", 1ull}});
  }, true);
  SynchronizeLayerDisplay(scene);
}
} // namespace iCAX::PlaneCAM::detail

namespace iCAX::PlaneCAM {
namespace {
class LayerLogic final {
  detail::Scene& scene;
  Text Name(const Map& request) const {
    const auto value = String(request, "name");
    if (value.empty() || value.size() > 1024 || value.find_first_not_of(" \t\r\n") == Text::npos)
      throw std::invalid_argument("PLANE_LAYER_NAME_REQUIRED");
    for (const auto& entity : detail::Collect<CPlaneLayerComponent>(scene)) {
      if (request.contains("id") && detail::Id(request) == entity->GetID()) continue;
      if (detail::Component<CPlaneLayerComponent>(entity)->GetName() == value)
        throw std::invalid_argument("PLANE_LAYER_NAME_EXISTS");
    }
    return value;
  }
public:
  explicit LayerLogic(detail::Scene& value) : scene(value) {}
  Map Create(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request);const auto name = Name(request);
    const auto order = NextLayerOrder(scene);
    const auto color = Color(request, layerColors[static_cast<std::size_t>(order % layerColors.size())]);
    const auto material = LayerMaterial(scene, color, name);
    const auto id = iCAX::Data::GenerateNewUUID();
    Mutate(scene, "创建图层", [&](auto& tx) {
      tx.CreateEntity(id);tx.AttachComponent(id, CPlaneLayerComponent::S_ClassName,
        {{"Name", name}, {"Color", color}, {"Order", order}, {"MaterialRef", material}});
      Bump(scene, tx, false);
    });
    return Receipt(scene, IdText(id));
  }
  Map Update(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request);const auto id = Id(request);
    const auto layer = Require<CPlaneLayerComponent>(scene, id);
    iCAX::Data::PropertySet fields;
    if (request.contains("name")) fields.emplace("Name", Name(request));
    const auto color = Color(request, layer->GetColor());
    const bool colorChanged = color != layer->GetColor();
    if (request.contains("color")) fields.emplace("Color", color);
    bool visible = layer->GetVisible(), locked = layer->GetLocked();
    for (const auto* key : {"visible", "locked"}) if (request.contains(key)) {
      if (!std::holds_alternative<bool>(request.at(key).m_Value))
        throw std::invalid_argument("PLANE_BOOLEAN_VALUE_REQUIRED");
      const bool value = request.at(key).To<bool>();
      fields.emplace(Text(key) == "visible" ? "Visible" : "Locked", value);
      if (Text(key) == "visible") visible = value; else locked = value;
    }
    const auto material = colorChanged ? LayerMaterial(scene, color, fields.contains("Name") ? fields.at("Name").To<Text>() : layer->GetName()) : layer->GetMaterialRef();
    if (colorChanged) fields.emplace("MaterialRef", material);
    Mutate(scene, "修改图层", [&](auto& tx) {
      tx.ModifyComponent(id, CPlaneLayerComponent::S_ClassName, fields);
      for (const auto& object : Objects(scene)) if (Component<CPlaneObjectComponent>(object)->GetLayerID() == id)
        ApplyLayerDisplay(scene, tx, object->GetID(), id, &material, &visible, &locked, nullptr, &color);
      Bump(scene, tx, false);
    });
    return Receipt(scene, IdText(id));
  }
  Map SetCurrent(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request);const auto id = Id(request);
    if (Require<CPlaneLayerComponent>(scene, id)->GetLocked()) throw std::invalid_argument("PLANE_LAYER_LOCKED");
    Mutate(scene, "设置当前图层", [&](auto& tx) {
      tx.ModifyComponent(scene.Database().GetMetaEntity()->GetID(), CPlaneProjectComponent::S_ClassName, {{"DefaultLayerID", id}});
      Bump(scene, tx, false);
    });
    return Receipt(scene, IdText(id));
  }
  Map Assign(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request);const auto layer = Id(request, "layerId");
    if (Require<CPlaneLayerComponent>(scene, layer)->GetLocked()) throw std::invalid_argument("PLANE_LAYER_LOCKED");
    if (!request.contains("ids")) throw std::invalid_argument("PLANE_OBJECTS_REQUIRED");
    const auto values = request.at("ids").To<Array>();
    if (values.empty()) throw std::invalid_argument("PLANE_OBJECTS_REQUIRED");
    std::vector<ID> ids;std::set<Text> unique;
    for (const auto& value : values) {
      const auto id = Id(Map{{"id", value}});
      if (!unique.insert(IdText(id)).second) continue;
      const auto object = Require<CPlaneObjectComponent>(scene, id);
      if (object->GetKind() != "drawing" && object->GetKind() != "parts" && object->GetKind() != "stocks" && object->GetKind() != "sketch-curves")
        throw std::invalid_argument("PLANE_OBJECT_KIND_MISMATCH");
      EnsureEditable(scene, *object);
      if (object->GetKind() == "sketch-curves") RequireSketchCurveEditing(scene, id);
      ids.push_back(id);
    }
    Mutate(scene, "设置对象图层", [&](auto& tx) {
      for (const auto& id : ids) {
        tx.ModifyComponent(id, CPlaneObjectComponent::S_ClassName, {{"LayerID", layer}});
        ApplyLayerDisplay(scene, tx, id, layer);
      }
      Bump(scene, tx, false);
    });
    return Receipt(scene);
  }
  Map Remove(const Map& request) {
    using namespace detail;
    CheckExpected(scene, request);const auto id = Id(request);
    const auto layer = Require<CPlaneLayerComponent>(scene, id);
    if (layer->GetLocked()) throw std::invalid_argument("PLANE_LAYER_LOCKED");
    if (Collect<CPlaneLayerComponent>(scene).size() <= 1) throw std::invalid_argument("PLANE_LAST_LAYER_REQUIRED");
    std::vector<ID> objects, backups;
    for (const auto& entity : Objects(scene)) if (Component<CPlaneObjectComponent>(entity)->GetLayerID() == id) objects.push_back(entity->GetID());
    for (const auto& entity : Collect<CPlaneSketchBackupComponent>(scene)) if (Component<CPlaneSketchBackupComponent>(entity)->GetLayerID() == id) backups.push_back(entity->GetID());
    const bool current = Project(scene)->GetDefaultLayerID() == id;
    ID target;
    if (request.contains("targetLayerId")) {
      target = Id(request, "targetLayerId");
      if (target == id) throw std::invalid_argument("PLANE_LAYER_DELETE_TARGET_REQUIRED");
      if (Require<CPlaneLayerComponent>(scene, target)->GetLocked()) throw std::invalid_argument("PLANE_LAYER_LOCKED");
    } else if (current || !objects.empty() || !backups.empty()) throw std::invalid_argument("PLANE_LAYER_IN_USE");
    Mutate(scene, "删除并迁移图层", [&](auto& tx) {
      for (const auto& object : objects) {
        tx.ModifyComponent(object, CPlaneObjectComponent::S_ClassName, {{"LayerID", target}});
        ApplyLayerDisplay(scene, tx, object, target);
      }
      for (const auto& backup : backups) tx.ModifyComponent(backup, CPlaneSketchBackupComponent::S_ClassName, {{"LayerID", target}});
      if (current) tx.ModifyComponent(scene.Database().GetMetaEntity()->GetID(), CPlaneProjectComponent::S_ClassName, {{"DefaultLayerID", target}});
      tx.DisposeEntity(id);Bump(scene, tx, false);
    });
    return Receipt(scene, IdText(id));
  }
};
class CPlaneLayerSDO final : public iCAX::Interaction::CSDO {
public:
  CPlaneLayerSDO() : CSDO("PlaneLayer") {
    ExposeMethod("Create", &detail::Dispatch<LayerLogic, &LayerLogic::Create>);
    ExposeMethod("Update", &detail::Dispatch<LayerLogic, &LayerLogic::Update>);
    ExposeMethod("SetCurrent", &detail::Dispatch<LayerLogic, &LayerLogic::SetCurrent>);
    ExposeMethod("Assign", &detail::Dispatch<LayerLogic, &LayerLogic::Assign>);
    ExposeMethod("Remove", &detail::Dispatch<LayerLogic, &LayerLogic::Remove>);
  }
};
}
ICAX_REGISTER_SDO(CPlaneLayerSDO)
} // namespace iCAX::PlaneCAM
