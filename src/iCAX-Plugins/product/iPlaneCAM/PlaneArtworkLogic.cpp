#include "PlaneDomainInternal.h"
#include "RenderData/RenderResource.h"
#include "RenderInteraction/RenderInteractionComponents.h"
#include "Resources/BinaryResource.h"
#include "Resources/FlatBufferResource.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>

namespace iCAX::PlaneCAM::detail {
namespace {
using Bitmap = iCAX::GeometryServices::PlaneArtworkBitmap;
struct Pose { double X = 0, Y = 0, Angle = 0, SX = 1, SY = 1; };
Pose ReadPose(const CPlaneArtworkComponent& value) {
  return {value.GetX(), value.GetY(), value.GetAngleDeg(), value.GetScaleX(), value.GetScaleY()};
}
Pose PoseOptions(const Map& request, Pose value = {}) {
  value.X = Number(request, "x", value.X);value.Y = Number(request, "y", value.Y);
  value.Angle = Number(request, "angleDeg", value.Angle);
  return value;
}
void ValidatePose(const Pose& value, double width, double height) {
  for (double n : {value.X, value.Y, value.Angle, value.SX, value.SY, width, height})
    if (!std::isfinite(n) || std::abs(n) > 1e9) throw std::invalid_argument("PLANE_ARTWORK_INVALID_PLACEMENT");
  if (!(width > 0) || !(height > 0) || value.SX == 0 || value.SY == 0 ||
      std::abs(width * value.SX) > 1e9 || std::abs(height * value.SY) > 1e9)
    throw std::invalid_argument("PLANE_ARTWORK_INVALID_SIZE");
  const double a = std::remainder(value.Angle, 360.) * std::numbers::pi / 180.;
  for (const auto& point : std::array<std::array<double, 2>, 4>{{{0, 0}, {width, 0}, {width, height}, {0, height}}}) {
    const double x = point[0] * value.SX, y = point[1] * value.SY;
    if (std::abs(value.X + x * std::cos(a) - y * std::sin(a)) > 1e9 ||
        std::abs(value.Y + x * std::sin(a) + y * std::cos(a)) > 1e9)
      throw std::invalid_argument("PLANE_ARTWORK_INVALID_PLACEMENT");
  }
}
Map PoseMap(const Pose& value) {
  return {{"x", value.X}, {"y", value.Y}, {"angleDeg", value.Angle}, {"scaleX", value.SX}, {"scaleY", value.SY}};
}
Pose FromPoseMap(const Map& value) {
  auto pose = PoseOptions(value);pose.SX = Number(value, "scaleX", 1);pose.SY = Number(value, "scaleY", 1);return pose;
}
iCAX::Data::PropertySet PoseFields(const Pose& value) {
  return {{"X", value.X}, {"Y", value.Y}, {"AngleDeg", value.Angle}, {"ScaleX", value.SX}, {"ScaleY", value.SY}};
}
N::Point Placed(const Pose& pose, double x, double y) {
  const double a = std::remainder(pose.Angle, 360.) * std::numbers::pi / 180.;x *= pose.SX;y *= pose.SY;
  return {pose.X + x * std::cos(a) - y * std::sin(a), pose.Y + x * std::sin(a) + y * std::cos(a)};
}
Ref PublishBinary(Scene& scene, const std::vector<std::uint8_t>& bytes, const Text& name,
                  const Text& extension, const Text& media) {
  auto value = std::make_shared<iCAX::Resource::CBinaryResource>();
  value->Content = bytes;value->DisplayName = name;value->FileExtension = extension;
  auto& library = scene.Resources();const auto url = library.AllocateResourceURL();
  iCAX::Resource::CResourceInfo info;info.Name = name;info.ResourceTypeID = "resource.binary";
  info.MediaType = media;info.nVersion = info.nSchemaVersion = info.nMinimumReaderVersion = 1;
  info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
  library.Set(url, value, info);return {url, library.GetVersion(url)};
}
Ref PublishFlat(Scene& scene, iCAX::Resource::CFlatBufferResource value, const Text& name,
               const Text& identifier, std::vector<Ref> dependencies) {
  auto& library = scene.Resources();const auto url = library.AllocateResourceURL();
  iCAX::Resource::CResourceInfo info;info.Name = name;
  info.ResourceTypeID = iCAX::Resource::CFlatBufferResource::kResourceTypeName;
  info.FlatBufferIdentifier = identifier;info.MediaType = "application/vnd.icax.flatbuffer";
  info.nVersion = info.nSchemaVersion = info.nMinimumReaderVersion = 1;
  info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;info.Dependencies = std::move(dependencies);
  library.Set(url, std::make_shared<iCAX::Resource::CFlatBufferResource>(std::move(value)), info);
  return {url, library.GetVersion(url)};
}
Ref Material(Scene& scene, const Ref& image, const Text& color) {
  iCAX::Render::SRenderMaterialData value;value.nDataVersion = 1;
  value.nColorRGBA = (static_cast<std::uint32_t>(std::stoul(color.substr(1), nullptr, 16)) << 8) | 0xffu;
  value.nAmbientRGBA = value.nColorRGBA;
  value.nMaterialFlags = iCAX::Render::kRenderStyleFlagLightingDisabled |
                        iCAX::Render::kRenderStyleFlagHasBaseColorTexture;
  value.strBaseColorTextureResourceID = image.URL;
  return PublishFlat(scene, iCAX::Render::MakeRenderMaterialResource(value), "文字或图片显示材质", "ICRM", {image});
}
Ref Quad(Scene& scene, const Pose& pose, double width, double height, const Ref& descriptor, bool text) {
  iCAX::Render::SRenderMeshData mesh;mesh.nDataVersion = 1;
  mesh.nFlags = iCAX::Render::kRenderMeshFlagHasTextureCoordinates;
  for (const auto p : {Placed(pose, 0, 0), Placed(pose, width, 0), Placed(pose, width, height), Placed(pose, 0, height)}) {
    if (!std::isfinite(p.X) || !std::isfinite(p.Y) || std::abs(p.X) > 1e9 || std::abs(p.Y) > 1e9)
      throw std::invalid_argument("PLANE_ARTWORK_INVALID_PLACEMENT");
    mesh.Positions.push_back({static_cast<float>(p.X), static_cast<float>(p.Y), text ? .001f : -.001f});
  }
  mesh.TextureCoordinates = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};mesh.Indices = {0, 1, 2, 0, 2, 3};
  return PublishFlat(scene, iCAX::Render::MakeRenderGeometryResource(mesh), "文字或参考图片平面", "ICRG", {descriptor});
}
Map Descriptor(const Text& kind, const Map& image, const Map& texture, const Pose& pose, double width, double height) {
  auto value = PoseMap(pose);value["schemaVersion"] = 1;value["units"] = Text("mm");value["contentType"] = kind;
  value["bitmapRef"] = image;value["textureRef"] = texture;
  value["baseWidthMm"] = width;value["baseHeightMm"] = height;
  value["width"] = std::abs(width * pose.SX);value["height"] = std::abs(height * pose.SY);return value;
}
Map BitmapMap(Bitmap value) {
  return {{"png", Text(value.EncodedPng.begin(), value.EncodedPng.end())},
    {"pixelWidth", static_cast<UInt>(value.PixelWidth)}, {"pixelHeight", static_cast<UInt>(value.PixelHeight)},
    {"widthMm", value.WidthMm}, {"heightMm", value.HeightMm}};
}
std::vector<std::uint8_t> Bytes(const Map& result, const Text& key) {
  const auto bytes = result.at(key).To<Text>();return {bytes.begin(), bytes.end()};
}
std::vector<std::uint8_t> BitmapBytes(Scene& scene, const CPlaneArtworkComponent& value) {
  const auto ref = Reference(value.GetBitmapRef());
  const auto resource = scene.Resources().Get<iCAX::Resource::CBinaryResource>(ref.URL, ref.nVersion);
  if (!resource) throw std::runtime_error("PLANE_ARTWORK_BITMAP_MISSING");
  return resource->Content;
}
ID TargetLayer(Scene& scene, const Map& request) {
  const auto id = request.contains("layerId") ? Id(request, "layerId") : Project(scene)->GetDefaultLayerID();
  if (Require<CPlaneLayerComponent>(scene, id)->GetLocked()) throw std::invalid_argument("PLANE_LAYER_LOCKED");
  return id;
}
void ValidateText(const Map& request) {
  const auto text = String(request, "text"), family = String(request, "fontFamily", "Microsoft YaHei");
  const auto size = Number(request, "fontSizeMm", 10);
  if (text.empty() || text.size() > 16384 || text.find('\0') != Text::npos)
    throw std::invalid_argument("PLANE_TEXT_CONTENT_REQUIRED");
  if (family.empty() || family.size() > 256 || family.find('\0') != Text::npos || !(size > 0) || size > 10000)
    throw std::invalid_argument("PLANE_TEXT_INVALID_FONT");
}
void ApplyArtworkRender(Scene& scene, iCAX::Database::ITransaction& tx, const ID& id,
                        const ID& layer, const Ref& preview, const Ref& material, bool attach) {
  if (attach) AttachRender(tx, id, preview);
  const auto display = Require<CPlaneLayerComponent>(scene, layer);
  tx.ModifyComponent(id, iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
    {{"GeometryResourceID", preview.URL}, {"GeometryResourceVersion", static_cast<UInt>(preview.nVersion)},
     {"GeometryKind", 1ull}, {"MaterialResourceID", material.URL},
     {"MaterialResourceVersion", static_cast<UInt>(material.nVersion)},
     {"Visible", display->GetVisible()}, {"Selectable", !display->GetLocked()}});
}
Map PublishBitmap(Scene& scene, const GeometryTaskSpec& task, const Map& result) {
  const bool update = task.Kind == "artwork-text-update", text = task.Kind != "artwork-image-import";
  const auto entity = update ? scene.Database().GetEntity(task.TargetID) : nullptr;
  const auto old = Component<CPlaneObjectComponent>(entity);
  const auto oldArtwork = Component<CPlaneArtworkComponent>(entity);
  const auto kind = Text(text ? "text" : "image"), name = update ? old->GetName() : String(task.Request, "name", text ? "文字" : "参考图片");
  auto pose = task.Request.contains("pose") ? FromPoseMap(task.Request.at("pose").To<Map>()) : PoseOptions(task.Request);
  const double width = Number(result, "widthMm"), height = Number(result, "heightMm");
  if (!text) {
    if (task.Request.contains("widthMm")) pose.SX = Number(task.Request, "widthMm") / width;
    if (task.Request.contains("heightMm")) pose.SY = Number(task.Request, "heightMm") / height;
    if (task.Request.contains("widthMm") && !task.Request.contains("heightMm")) pose.SY = pose.SX;
    if (task.Request.contains("heightMm") && !task.Request.contains("widthMm")) pose.SX = pose.SY;
  }
  ValidatePose(pose, width, height);
  if (!text && (!(pose.SX > 0) || !(pose.SY > 0))) throw std::invalid_argument("PLANE_ARTWORK_INVALID_SIZE");
  const auto layer = task.LayerID;
  const auto png = Bytes(result, "png");
  if (png.empty()) throw std::invalid_argument("PLANE_ARTWORK_EMPTY_BITMAP");
  const auto bitmap = PublishBinary(scene, png, name + ".png", ".png", "image/png");
  const auto texture = PublishFlat(scene, iCAX::Render::MakeRenderTextureResource(png,
      static_cast<std::uint32_t>(Number(result, "pixelWidth")), static_cast<std::uint32_t>(Number(result, "pixelHeight"))),
      name + "显示纹理", "ICRT", {bitmap});
  Map source;
  if (result.contains("sourceBytes")) source = Stored(PublishBinary(scene, Bytes(result, "sourceBytes"),
      String(task.Request, "sourceName"), String(task.Request, "sourceExtension"), "application/octet-stream"));
  const auto color = text ? Require<CPlaneLayerComponent>(scene, layer)->GetColor() : Text("#FFFFFF");
  const auto material = Material(scene, texture, color);
  const auto descriptor = Descriptor(kind, Stored(bitmap), Stored(texture), pose, width, height);
  const auto geometry = PublishMap(scene, descriptor, name + "对象数据", {bitmap, texture});
  const auto preview = Quad(scene, pose, width, height, geometry, text);
  Mutate(scene, update ? "编辑文字内容" : text ? "添加文字" : "导入参考图片", [&](auto& tx) {
    auto fields = PoseFields(pose);
    fields.insert({{"BitmapRef", Stored(bitmap)}, {"TextureRef", Stored(texture)}, {"MaterialRef", Stored(material)}, {"MaterialColor", color},
      {"PixelWidth", result.at("pixelWidth")}, {"PixelHeight", result.at("pixelHeight")},
      {"BaseWidthMm", width}, {"BaseHeightMm", height}});
    const iCAX::Data::PropertySet textFields{{"TextValue", String(task.Request, "text")},
      {"FontFamily", String(task.Request, "fontFamily", "Microsoft YaHei")}, {"FontSizeMm", Number(task.Request, "fontSizeMm", 10)}};
    if (update) {
      tx.ModifyComponent(task.TargetID, CPlaneArtworkComponent::S_ClassName, fields);
      tx.ModifyComponent(task.TargetID, CPlaneTextComponent::S_ClassName, textFields);
      tx.ModifyComponent(task.TargetID, CPlaneObjectComponent::S_ClassName,
        {{"GeometryRef", Stored(geometry)}, {"PreviewRef", Stored(preview)},
         {"GeometryVersion", Next(old->GetGeometryVersion())},
         {"Width", std::abs(width * pose.SX)}, {"Height", std::abs(height * pose.SY)}});
    } else {
      tx.CreateEntity(task.TargetID);
      tx.AttachComponent(task.TargetID, CPlaneObjectComponent::S_ClassName,
        {{"Kind", Text("drawing")}, {"Name", name}, {"LayerID", layer},
         {"SourceName", String(task.Request, "sourceName")}, {"SourceRef", source},
         {"GeometryRef", Stored(geometry)}, {"PreviewRef", Stored(preview)},
         {"Width", std::abs(width * pose.SX)}, {"Height", std::abs(height * pose.SY)}});
      tx.AttachComponent(task.TargetID, CPlaneDrawingComponent::S_ClassName);
      tx.AttachComponent(task.TargetID, CPlaneArtworkComponent::S_ClassName, fields);
      if (text) tx.AttachComponent(task.TargetID, CPlaneTextComponent::S_ClassName, textFields);
      else tx.AttachComponent(task.TargetID, CPlaneImageComponent::S_ClassName, {{"SourceName", String(task.Request, "sourceName")}});
    }
    ApplyArtworkRender(scene, tx, task.TargetID, layer, preview, material, !update);Bump(scene, tx, false);
  });
  auto reply = Receipt(scene, IdText(task.TargetID));reply["ids"] = Array{IdText(task.TargetID)};return reply;
}
Map ChangePose(Scene& scene, const ID& sourceID, const Pose& pose, bool copy) {
  const auto entity = scene.Database().GetEntity(sourceID);
  const auto object = Require<CPlaneObjectComponent>(scene, sourceID);
  const auto artwork = Require<CPlaneArtworkComponent>(scene, sourceID);
  EnsureEditable(scene, *object);ValidatePose(pose, artwork->GetBaseWidthMm(), artwork->GetBaseHeightMm());
  const bool text = static_cast<bool>(Component<CPlaneTextComponent>(entity));
  const auto target = copy ? iCAX::Data::GenerateNewUUID() : sourceID;
  const auto name = object->GetName() + (copy ? " 副本" : "");
  const auto descriptor = Descriptor(text ? "text" : "image", artwork->GetBitmapRef(), artwork->GetTextureRef(), pose,
                                      artwork->GetBaseWidthMm(), artwork->GetBaseHeightMm());
  const auto geometry = PublishMap(scene, descriptor, name + "对象数据",
                                  {Reference(artwork->GetBitmapRef()), Reference(artwork->GetTextureRef()), Reference(object->GetGeometryRef())});
  const auto preview = Quad(scene, pose, artwork->GetBaseWidthMm(), artwork->GetBaseHeightMm(), geometry, text);
  Mutate(scene, copy ? "复制文字或图片" : "放置文字或图片", [&](auto& tx) {
    auto fields = PoseFields(pose);
    if (copy) {
      tx.CreateEntity(target);
      tx.AttachComponent(target, CPlaneObjectComponent::S_ClassName,
        {{"Kind", Text("drawing")}, {"Name", name}, {"Note", object->GetNote()},
         {"LayerID", object->GetLayerID()}, {"SourceRef", object->GetSourceRef()}, {"SourceName", object->GetSourceName()},
         {"GeometryRef", Stored(geometry)}, {"PreviewRef", Stored(preview)},
         {"Width", std::abs(artwork->GetBaseWidthMm() * pose.SX)}, {"Height", std::abs(artwork->GetBaseHeightMm() * pose.SY)}});
      tx.AttachComponent(target, CPlaneDrawingComponent::S_ClassName);
      fields.insert({{"BitmapRef", artwork->GetBitmapRef()}, {"TextureRef", artwork->GetTextureRef()}, {"MaterialRef", artwork->GetMaterialRef()},
        {"MaterialColor", artwork->GetMaterialColor()}, {"PixelWidth", artwork->GetPixelWidth()}, {"PixelHeight", artwork->GetPixelHeight()},
        {"BaseWidthMm", artwork->GetBaseWidthMm()}, {"BaseHeightMm", artwork->GetBaseHeightMm()}});
      tx.AttachComponent(target, CPlaneArtworkComponent::S_ClassName, fields);
      if (text) {
        const auto value = Component<CPlaneTextComponent>(entity);
        tx.AttachComponent(target, CPlaneTextComponent::S_ClassName,
          {{"TextValue", value->GetTextValue()}, {"FontFamily", value->GetFontFamily()}, {"FontSizeMm", value->GetFontSizeMm()}});
      } else tx.AttachComponent(target, CPlaneImageComponent::S_ClassName,
          {{"SourceName", Require<CPlaneImageComponent>(scene, sourceID)->GetSourceName()}});
      CopyMachiningSource(scene, tx, sourceID, target);
    } else {
      tx.ModifyComponent(target, CPlaneArtworkComponent::S_ClassName, fields);
      tx.ModifyComponent(target, CPlaneObjectComponent::S_ClassName,
        {{"GeometryRef", Stored(geometry)}, {"PreviewRef", Stored(preview)}, {"GeometryVersion", Next(object->GetGeometryVersion())},
         {"Width", std::abs(artwork->GetBaseWidthMm() * pose.SX)}, {"Height", std::abs(artwork->GetBaseHeightMm() * pose.SY)}});
    }
    ApplyArtworkRender(scene, tx, target, object->GetLayerID(), preview, Reference(artwork->GetMaterialRef()), copy);
    Bump(scene, tx, false);
  });
  return Receipt(scene, IdText(target));
}
} // namespace

Map ArtworkRow(Scene& scene, const ID& id) {
  const auto entity = scene.Database().GetEntity(id);
  const auto artwork = Component<CPlaneArtworkComponent>(entity);
  if (!artwork) return {};
  const auto text = Component<CPlaneTextComponent>(entity);
  Map result{{"contentType", Text(text ? "text" : "image")}, {"reference", true},
    {"imageRef", Wire(artwork->GetBitmapRef())}, {"pixelWidth", artwork->GetPixelWidth()}, {"pixelHeight", artwork->GetPixelHeight()},
    {"textureRef", Wire(artwork->GetTextureRef())},
    {"baseWidthMm", artwork->GetBaseWidthMm()}, {"baseHeightMm", artwork->GetBaseHeightMm()},
    {"x", artwork->GetX()}, {"y", artwork->GetY()}, {"angleDeg", artwork->GetAngleDeg()},
    {"scaleX", artwork->GetScaleX()}, {"scaleY", artwork->GetScaleY()}, {"materialRef", Wire(artwork->GetMaterialRef())},
    {"isPolyline", false}, {"canClosePolyline", false}, {"closed", false},
    {"drawingEdits", Array{Text("move"), Text("rotate"), Text("scale"), Text("copy"), Text("mirror"), Text("delete")}}};
  if (text) { result["text"] = text->GetTextValue();result["fontFamily"] = text->GetFontFamily();result["fontSizeMm"] = text->GetFontSizeMm(); }
  return result;
}
Map ArtworkMaterial(Scene& scene, const ID& id, const Text& color) {
  const auto entity = scene.Database().GetEntity(id);
  const auto value = Component<CPlaneArtworkComponent>(entity);
  if (!value) return {};
  const auto effective = Component<CPlaneTextComponent>(entity) ? color : Text("#FFFFFF");
  return value->GetMaterialColor() == effective ? value->GetMaterialRef() : Stored(Material(scene, Reference(value->GetTextureRef()), effective));
}
Map ReadArtworkGeometry(Scene& scene, const ID& id) {
  const auto object = Require<CPlaneObjectComponent>(scene, id);
  const auto artwork = Require<CPlaneArtworkComponent>(scene, id);
  const auto pose = ReadPose(*artwork);const double width = artwork->GetBaseWidthMm(), height = artwork->GetBaseHeightMm();
  const auto a = Placed(pose, 0, 0), b = Placed(pose, width, 0), c = Placed(pose, 0, height);
  const auto point = [](N::Point p) { return Array{p.X, p.Y, 0.}; };
  const auto dimension = [&](const Text& key, double value, N::Point end) {
    return Map{{"id", IdText(id) + ":" + key}, {"entityId", IdText(id)}, {"parameter", key}, {"key", key},
      {"label", Text(key == "width" ? "W" : "H")}, {"kind", Text("bounds")}, {"value", value},
      {"editable", false}, {"start", point(a)}, {"end", point(end)}, {"offsetDirection", Array{0., -1., 0.}}};
  };
  auto result = ArtworkRow(scene, id);result["id"] = IdText(id);result["geometryVersion"] = std::to_string(object->GetGeometryVersion());
  result["geometryRef"] = Wire(object->GetGeometryRef());result["curves"] = Array{};result["snapPoints"] = Array{};
  result["dimensions"] = Array{dimension("width", std::abs(width * pose.SX), b), dimension("height", std::abs(height * pose.SY), c)};
  return result;
}
Map CreateText(Scene& scene, const Map& request, bool update) {
  CheckExpected(scene, request);RequireWorktableRequest(request);Map options = request;GeometryTaskSpec task;
  task.Kind = update ? "artwork-text-update" : "artwork-text-create";
  task.TargetID = update ? Id(request) : iCAX::Data::GenerateNewUUID();task.RequireAbsentTarget = !update;
  Pose pose;
  if (update) {
    const auto text = Require<CPlaneTextComponent>(scene, task.TargetID);
    const auto artwork = Require<CPlaneArtworkComponent>(scene, task.TargetID);
    const auto object = Require<CPlaneObjectComponent>(scene, task.TargetID);EnsureEditable(scene, *object);
    if (request.contains("expectedGeometryRef")) CheckExpectedGeometry(request, object->GetGeometryRef());
    if (!options.contains("text")) options["text"] = text->GetTextValue();
    if (!options.contains("fontFamily")) options["fontFamily"] = text->GetFontFamily();
    if (!options.contains("fontSizeMm")) options["fontSizeMm"] = text->GetFontSizeMm();
    pose = ReadPose(*artwork);task.LayerID = object->GetLayerID();
    task.Inputs.push_back({task.TargetID, object->GetGeometryRef(), object->GetGeometryVersion()});
  } else { task.LayerID = TargetLayer(scene, request);ObjectProperties("drawing", {{"name", String(request, "name", "文字")}}); }
  options["pose"] = PoseMap(PoseOptions(request, pose));ValidateText(options);ValidatePose(FromPoseMap(options.at("pose").To<Map>()), 1, 1);
  task.Request = options;const auto service = scene.Services().Resolve<iCAX::GeometryServices::IPlaneArtworkService>();
  return QueueGeometryTask(scene, std::move(task), [service, options](auto token) {
    token.ThrowIfCancellationRequested();auto result = BitmapMap(service->RasterizeText(options));token.ThrowIfCancellationRequested();return result;
  });
}
Map ImportImage(Scene& scene, const Map& request) {
  CheckExpected(scene, request);RequireWorktableRequest(request);const auto layer = TargetLayer(scene, request);
  const auto text = String(request, "sourcePath");const auto path = std::filesystem::path(std::u8string(text.begin(), text.end()));
  if (!path.is_absolute() || !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 64 * 1024 * 1024)
    throw std::invalid_argument("PLANE_ABSOLUTE_IMAGE_FILE_REQUIRED");
  std::ifstream file(path, std::ios::binary);std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  if (bytes.empty()) throw std::invalid_argument("PLANE_EMPTY_SOURCE_FILE");
  auto options = request;const auto filename = path.filename().u8string(), extension = path.extension().u8string(), stem = path.stem().u8string();
  options["sourceName"] = Text(filename.begin(), filename.end());options["sourceExtension"] = Text(extension.begin(), extension.end());
  if (!options.contains("name")) options["name"] = Text(stem.begin(), stem.end());
  ObjectProperties("drawing", options);ValidatePose(PoseOptions(options), 1, 1);
  for (const auto* key : {"widthMm", "heightMm"}) if (options.contains(key) && !(Number(options, key) > 0))
    throw std::invalid_argument("PLANE_ARTWORK_INVALID_SIZE");
  GeometryTaskSpec task;task.Kind = "artwork-image-import";task.TargetID = iCAX::Data::GenerateNewUUID();
  task.RequireAbsentTarget = true;task.LayerID = layer;task.Request = options;
  const auto service = scene.Services().Resolve<iCAX::GeometryServices::IPlaneArtworkService>();
  return QueueGeometryTask(scene, std::move(task), [service, bytes = std::move(bytes)](auto token) {
    token.ThrowIfCancellationRequested();auto result = BitmapMap(service->NormalizeImage(bytes));
    result["sourceBytes"] = Text(bytes.begin(), bytes.end());token.ThrowIfCancellationRequested();return result;
  });
}
Map UpdateImage(Scene& scene, const Map& request) {
  CheckExpected(scene, request);RequireWorktableRequest(request);const auto id = Id(request);Require<CPlaneImageComponent>(scene, id);
  const auto value = Require<CPlaneArtworkComponent>(scene, id);
  const auto object = Require<CPlaneObjectComponent>(scene, id);
  EnsureEditable(scene, *object);if (request.contains("expectedGeometryRef")) CheckExpectedGeometry(request, object->GetGeometryRef());
  auto pose = PoseOptions(request, ReadPose(*value));
  const double width = value->GetBaseWidthMm(), height = value->GetBaseHeightMm();
  if (request.contains("widthMm")) {
    const double desired = Number(request, "widthMm");if (!(desired > 0)) throw std::invalid_argument("PLANE_ARTWORK_INVALID_SIZE");
    const double factor = desired / std::abs(width * pose.SX);pose.SX *= factor;if (!request.contains("heightMm")) pose.SY *= factor;
  }
  if (request.contains("heightMm")) {
    const double desired = Number(request, "heightMm");if (!(desired > 0)) throw std::invalid_argument("PLANE_ARTWORK_INVALID_SIZE");
    const double factor = desired / std::abs(height * pose.SY);pose.SY *= factor;if (!request.contains("widthMm")) pose.SX *= factor;
  }
  return ChangePose(scene, id, pose, false);
}
Map EditArtwork(Scene& scene, const Map& request) {
  const auto id = Id(request);
  const auto object = Require<CPlaneObjectComponent>(scene, id);
  EnsureEditable(scene, *object);if (request.contains("expectedGeometryRef")) CheckExpectedGeometry(request, object->GetGeometryRef());
  const auto operation = String(request, "operation");
  if (operation == "delete") { auto args = request;args["kind"] = Text("drawing");return UpdateObject(scene, args, true); }
  auto pose = ReadPose(*Require<CPlaneArtworkComponent>(scene, id));
  const double cx = Number(request, "cx"), cy = Number(request, "cy");
  if (operation == "move" || operation == "copy") { pose.X += Number(request, "dx");pose.Y += Number(request, "dy"); }
  else if (operation == "rotate") {
    const double degrees = Number(request, "angleDeg"), angle = degrees * std::numbers::pi / 180., x = pose.X - cx, y = pose.Y - cy;
    pose.X = cx + x * std::cos(angle) - y * std::sin(angle);pose.Y = cy + x * std::sin(angle) + y * std::cos(angle);pose.Angle += degrees;
  } else if (operation == "scale") {
    const double factor = Number(request, "factor", 1);if (!(factor > 0)) throw std::invalid_argument("PLANE_INVALID_SCALE");
    pose.X = cx + factor * (pose.X - cx);pose.Y = cy + factor * (pose.Y - cy);pose.SX *= factor;pose.SY *= factor;
  } else if (operation == "mirror") {
    const auto axis = String(request, "axis", "x");if (axis != "x" && axis != "y") throw std::invalid_argument("PLANE_INVALID_MIRROR_AXIS");
    if (axis == "x") { pose.Y = 2 * cy - pose.Y;pose.Angle = -pose.Angle; }
    else { pose.X = 2 * cx - pose.X;pose.Angle = 180 - pose.Angle; }
    pose.SY = -pose.SY;
  } else throw std::invalid_argument("PLANE_ARTWORK_EDIT_UNSUPPORTED");
  return ChangePose(scene, id, pose, operation == "copy");
}
Map VectorizeImage(Scene& scene, const Map& request) {
  CheckExpected(scene, request);RequireWorktableRequest(request);const auto id = Id(request);Require<CPlaneImageComponent>(scene, id);
  const auto object = Require<CPlaneObjectComponent>(scene, id);
  const auto artwork = Require<CPlaneArtworkComponent>(scene, id);
  EnsureEditable(scene, *object);if (request.contains("expectedGeometryRef")) CheckExpectedGeometry(request, object->GetGeometryRef());
  const auto pose = ReadPose(*artwork);auto options = request;
  options["widthMm"] = std::abs(artwork->GetBaseWidthMm() * pose.SX);options["heightMm"] = std::abs(artwork->GetBaseHeightMm() * pose.SY);
  GeometryTaskSpec task;task.Kind = "artwork-vectorize";task.TargetID = id;task.LayerID = object->GetLayerID();task.Request = request;
  task.Request["name"] = object->GetName() + " 转矢量";
  task.Inputs.push_back({id, object->GetGeometryRef(), object->GetGeometryVersion()});
  const auto png = BitmapBytes(scene, *artwork);
  const auto source = object->GetGeometryRef();
  task.Request["sourceGeometryRef"] = source;task.Request["sourceGeometryVersion"] = object->GetGeometryVersion();
  const auto service = scene.Services().Resolve<iCAX::GeometryServices::IPlaneArtworkService>();
  const auto geometry = Geometry(scene);
  return QueueGeometryTask(scene, std::move(task), [service, geometry, png, options, pose](auto token) {
    auto value = service->Vectorize(png, options, [token] { return token.IsCancellationRequested(); });
    const double approximation = Number(value, "approximationBoundMm");
    const auto diagnostics = value.contains("traceDiagnostics") ? value.at("traceDiagnostics").To<Map>() : Map{};
    if (pose.SX < 0) value = geometry->TransformGeometry(value, {{"operation", Text("mirror")}, {"axis", Text("y")}});
    if (pose.SY < 0) value = geometry->TransformGeometry(value, {{"operation", Text("mirror")}, {"axis", Text("x")}});
    value = geometry->TransformGeometry(value, {{"operation", Text("rotate")}, {"angleDeg", pose.Angle}});
    value = geometry->TransformGeometry(value, {{"operation", Text("move")}, {"dx", pose.X}, {"dy", pose.Y}});
    value["approximationBoundMm"] = std::max(approximation, Number(value, "approximationBoundMm"));
    if (!diagnostics.empty()) value["traceDiagnostics"] = diagnostics;
    token.ThrowIfCancellationRequested();return value;
  });
}
Map PublishArtworkTask(Scene& scene, const GeometryTaskSpec& task, const Map& result) {
  if (task.Kind != "artwork-vectorize") return PublishBitmap(scene, task, result);
  const Map source{{"SourceKind", Text("image-vector")}, {"SourceObjectID", task.TargetID},
    {"SourceGeometryRef", task.Request.at("sourceGeometryRef")}, {"SourceGeometryVersion", task.Request.at("sourceGeometryVersion")}};
  const Array items{Map{{"name", String(task.Request, "name")}, {"geometry", result}, {"source", source}}};
  return PublishMachiningTask(scene, task, {{"items", items}});
}
} // namespace iCAX::PlaneCAM::detail
