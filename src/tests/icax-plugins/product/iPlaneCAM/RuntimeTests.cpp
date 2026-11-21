#include "ApplicationContext/ApplicationContext.h"
#include "ApplicationContext/UserDataStore.h"
#include "Data/VariantSerializer.h"
#include "Database/IRepositoryEvent.h"
#include "Resources/BinaryResource.h"
#include "Resources/ResourceAccess.h"
#include "GeometryServices/PlaneGeometryService.h"
#include "GeometryServices/PlaneArtworkService.h"
#include "Product/ProductManifest.h"
#include "Product/ProductRuntime.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <thread>
#include <tuple>
#include <vector>

namespace {
using Map = iCAX::Data::ObjectMap;
using Array = iCAX::Data::VariantArray;
using Text = std::string;
using UInt = unsigned long long;
iCAX::Project::CProjectScene *diagnosticScene = nullptr;
std::uint64_t callSequence = 0;
void Check(bool value, const char *why) {
  if (!value)
    throw std::runtime_error(why);
}
Text UTF8(const std::filesystem::path &p) {
  const auto s = p.u8string();
  return {s.begin(), s.end()};
}
class MemoryData final : public iCAX::Product::IProductDataStore {
public:
  iCAX::Product::CProductData Load(const Text &) const override { return {}; }
  void Save(const Text &, const iCAX::Product::CProductData &) const override {}
};
Map Call(const iCAX::Interaction::CSDOEndpoint &endpoint, const Text &group,
         const Text &method, const Map &payload = {}) {
  iCAX::Interaction::CSDOFrame request;
  request.nCallID = ++callSequence;
  std::cerr << "[call " << request.nCallID << "] " << group << '.' << method
            << " begin\n"
            << std::flush;
  request.nMethodCode =
      iCAX::Interaction::MakeSDOMethodCode(group.c_str(), method.c_str());
  request.nKind = iCAX::Interaction::ESDOFrameKind::Request;
  const auto bytes = iCAX::Data::VariantSerializer::Serialize(payload);
  request.Payload.assign(bytes.begin(), bytes.end());
  endpoint.Send(request);
  for (int i = 0; i < 2000; ++i) {
    for (const auto &response : endpoint.Receive())
      if (response.nCallID == request.nCallID &&
          response.nKind == iCAX::Interaction::ESDOFrameKind::Response) {
        if (response.nStatus != iCAX::Interaction::EInvocationStatus::Ok)
          throw std::runtime_error(
              "SDO rejected " + group + "." + method + ": " +
              Text(response.Payload.begin(), response.Payload.end()));
        std::cerr << "[call " << request.nCallID << "] " << group << '.'
                  << method << " ok\n"
                  << std::flush;
        return iCAX::Data::VariantSerializer::Deserialize(
                   Text(response.Payload.begin(), response.Payload.end()))
            .To<Map>();
      }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (diagnosticScene) {
    std::cerr << "[timeout] Scene state="
              << static_cast<int>(diagnosticScene->GetState()) << '\n';
    if (const auto fault = diagnosticScene->GetLastFault())
      std::cerr << "[timeout] Scene fault=" << fault->Message << '\n';
    else
      std::cerr << "[timeout] Scene has no recorded fault\n";
    std::cerr << std::flush;
  }
  throw std::runtime_error("SDO timeout: " + group + "." + method);
}
template <class F> void Rejected(F f, const char *why) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  Check(rejected, why);
}
Map Model(const iCAX::Interaction::CSDOEndpoint &e) {
  return Call(e, "PlaneScene", "ReadState").at("iplaneCam").To<Map>();
}
void Write(const std::filesystem::path &p, const Text &bytes) {
  std::ofstream out(p, std::ios::binary);
  out << bytes;
  Check(static_cast<bool>(out), "fixture write failed");
}
Text DXF(const Text &entities, bool declaredUnits = true) {
  return Text("0\nSECTION\n2\nHEADER\n") +
         (declaredUnits ? "9\n$INSUNITS\n70\n4\n" : "") +
         "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n" + entities +
         "0\nENDSEC\n0\nEOF\n";
}
#include "PolylineCloseTests.inc"
#include "DrawingDimensionTests.inc"
#include "GeometryServiceTests.inc"
#include "ArtworkServiceTests.inc"
#include "ArtworkTextureTests.inc"
#include "ArtworkBusinessTests.inc"
#include "MixedPolylineTests.inc"
#include "DrawingPrimitiveTests.inc"
#include "SketchEditingTests.inc"
#include "ColoredLayerTests.inc"
} // namespace
int main(int argc, char **argv) {
  try {
    wchar_t executable[32768]{};
    Check(GetModuleFileNameW(nullptr, executable, 32768) > 0,
          "executable path unavailable");
    const auto bin = std::filesystem::path(executable).parent_path();
    const auto manifestPath =
        argc > 1 ? std::filesystem::path(argv[1])
                 : bin / "apps/iplane-cam/product.manifest.json";
    const auto fixtureDir = argc > 2 ? std::filesystem::path(argv[2])
                                     : bin / "tests/iplane-cam/Fixtures";
    auto manifest = iCAX::Product::LoadProductManifest(UTF8(manifestPath));
    const auto local = [&](Text &module) {
      if (module.empty())
        return;
      const auto path = bin / std::filesystem::path(module).filename();
      Check(std::filesystem::is_regular_file(path),
            "runtime dependency missing");
      module = UTF8(path);
    };
    auto &modules = manifest.Definition.Modules;
    for (auto *list : {&modules.DependencyModules, &modules.ComponentModules,
                       &modules.BehaviourModules, &modules.ServiceModules,
                       &modules.SDOModules})
      for (auto &module : *list)
        local(module);
    for (auto &group : modules.ModuleGroups)
      for (auto *module : {&group.strComponentPath, &group.strBehaviourPath,
                           &group.strServicePath, &group.strSDOPath})
        local(*module);
    for (auto &h : manifest.Definition.ResourceHandlers)
      local(h.ModulePath);
    iCAX::Application::CApplicationDescriptor descriptor;
    descriptor.AppID = "iplane-native-acceptance";
    descriptor.AppName = "iPlaneCAM native acceptance";
    iCAX::Application::CApplicationPaths paths;
    paths.InstallDirectory = UTF8(bin);
    auto app = std::make_shared<iCAX::Application::CApplicationContext>(
        descriptor, paths, iCAX::Data::PropertyBag{});
    auto runtime = std::make_shared<iCAX::Product::CProductRuntime>(
        manifest.Definition, app,
        std::make_shared<iCAX::Interaction::CSDOChannelRegistry>(),
        std::make_shared<iCAX::Application::CProductUserDataStore>(
            std::make_shared<iCAX::Application::CSqliteUserDataStore>(
                ":memory:"),
            manifest.Definition.ProductID),
        std::make_shared<MemoryData>());
    runtime->Start();
    std::cerr << "[phase] product started\n" << std::flush;
    auto catalog =
        runtime->OpenProjectCatalog("Plane native", {}, "Plane native", {});
    auto project = catalog->GetMainProject();
    auto endpoint = runtime->GetSceneFrontendSDOEndpoint(
        project->GetProjectID(), project->GetMainSceneID());
    diagnosticScene = &project->GetMainScene();
    std::cerr << "[phase] new catalog scene ready\n" << std::flush;
    Check(Model(endpoint).at("parts").To<Array>().empty(),
          "new project must be empty");
    auto sourceDir = std::filesystem::temp_directory_path() /
                     ("icax-plane-test-" +
                      iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
    std::filesystem::create_directories(sourceDir);
    CheckDrawingPrimitives(runtime, sourceDir, fixtureDir);
    CheckPlaneArtworkService(runtime, fixtureDir);
    CheckArtworkBusiness(runtime, sourceDir, fixtureDir);
    CheckPolylineClosure(runtime, sourceDir);
    CheckDrawingDimensions(runtime, sourceDir);
    CheckMixedPolylines(runtime, sourceDir, fixtureDir);
    CheckSketchEditing(runtime, sourceDir, fixtureDir);
    CheckColoredLayers(runtime, sourceDir, fixtureDir);
    const auto part = sourceDir / "part.dxf", stock = sourceDir / "stock.dxf";
    std::filesystem::copy_file(fixtureDir / "part_with_hole.dxf", part);
    std::filesystem::copy_file(fixtureDir / "irregular_stock.dxf", stock);
    const auto circleId = Call(endpoint, "PlaneDrawing", "Create",
                               {{"primitive", Text("circle")},
                                {"center", Array{0., 0.}},
                                {"radius", 1.}})
                              .at("id")
                              .To<Text>();
    const auto circle = Model(endpoint).at("drawings").To<Array>()[0].To<Map>();
    Check(std::abs(circle.at("width").To<double>() - 2.) < 1e-9 &&
              std::abs(circle.at("height").To<double>() - 2.) < 1e-9,
          "circle bounds must be analytic diameter, not chord extrema");
    Call(endpoint, "PlaneObject", "Remove",
         {{"id", circleId}, {"kind", Text("drawing")}});
    const auto drawing =
        Call(endpoint, "PlaneDrawing", "Create",
             {{"primitive", Text("line")},
              {"points", Array{Array{0., 0.}, Array{100., 20.}}}})
            .at("id")
            .To<Text>();
    Check(Model(endpoint).at("parts").To<Array>().empty(),
          "drawing must never create a part");
    Check(Model(endpoint).at("stocks").To<Array>().empty(),
          "drawing must never create stock");
    Call(endpoint, "PlaneDrawing", "Edit",
         {{"id", drawing},
          {"operation", Text("move")},
          {"dx", 10.},
          {"dy", 5.}});
    auto exact =
        Call(endpoint, "PlaneDrawing", "ReadGeometry", {{"id", drawing}});
    Check(!exact.at("snapPoints").To<Array>().empty(),
          "exact snap query missing");
    const auto resourceCountBeforeInvalid =
        project->GetMainScene().Resources().GetManifest().size();
    const auto objectCountBeforeInvalid =
        Model(endpoint).at("parts").To<Array>().size();
    Rejected(
        [&] {
          Call(endpoint, "PlanePart", "Import",
               {{"sourcePath", UTF8(part)}, {"quantity", 0}});
        },
        "invalid import quantity must reject before resource publication");
    Rejected(
        [&] {
          Call(endpoint, "PlanePart", "Import",
               {{"sourcePath", UTF8(part)}, {"name", Text()}});
        },
        "invalid import name must reject before resource publication");
    Check(
        project->GetMainScene().Resources().GetManifest().size() ==
                resourceCountBeforeInvalid &&
            Model(endpoint).at("parts").To<Array>().size() ==
                objectCountBeforeInvalid,
        "rejected imports must not add Embedded resources or business objects");
    const auto partId = Call(endpoint, "PlanePart", "Import",
                             {{"sourcePath", UTF8(part)}, {"quantity", 3}})
                            .at("id")
                            .To<Text>();
    Call(endpoint, "PlaneStock", "Import",
         {{"sourcePath", UTF8(stock)}, {"quantity", 1}});
    Call(endpoint, "PlaneStock", "CreateRectangle",
         {{"name", Text("Rect")},
          {"width", 1000.},
          {"height", 1000.},
          {"quantity", 2}});
    auto state = Model(endpoint);
    Check(state.at("drawings").To<Array>().size() == 1,
          "independent drawing lost");
    Check(state.at("parts").To<Array>().size() == 1 &&
              state.at("stocks").To<Array>().size() == 2,
          "independent imports missing");
    Check(
        state.at("parts").To<Array>()[0].To<Map>().at("holeCount").To<UInt>() ==
            1,
        "part hole not retained");
    const auto open = sourceDir / "open.dxf";
    Write(open, "0\nSECTION\n2\nHEADER\n9\n$"
                "INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n"
                "10\n0\n20\n0\n11\n10\n21\n0\n0\nENDSEC\n0\nEOF\n");
    Rejected(
        [&] {
          Call(endpoint, "PlanePart", "Import", {{"sourcePath", UTF8(open)}});
        },
        "open material must reject");
    const auto unitless = sourceDir / "unitless.dxf";
    Write(unitless, DXF("0\nLINE\n10\n0\n20\n0\n11\n10\n21\n0\n", false));
    Rejected(
        [&] {
          Call(endpoint, "PlaneDrawing", "Import",
               {{"sourcePath", UTF8(unitless)}});
        },
        "unitless source must require explicit units");
    const auto importedDrawing =
        Call(endpoint, "PlaneDrawing", "Import",
             {{"sourcePath", UTF8(unitless)}, {"units", Text("mm")}})
            .at("id")
            .To<Text>();
    Check(Model(endpoint).at("drawings").To<Array>().size() == 2,
          "independent drawing import failed");
    const auto endz = sourceDir / "endz.dxf";
    Write(endz, DXF("0\nLINE\n10\n0\n20\n0\n11\n10\n21\n0\n31\n2\n"));
    Rejected(
        [&] {
          Call(endpoint, "PlaneDrawing", "Import",
               {{"sourcePath", UTF8(endz)}});
        },
        "nonplanar LINE endpoint Z must reject");
    const auto nan = sourceDir / "nan.dxf";
    Write(nan, DXF("0\nLWPOLYLINE\n90\n2\n10\nNaN\n20\n0\n10\n10\n20\n0\n"));
    Rejected(
        [&] {
          Call(endpoint, "PlaneDrawing", "Import", {{"sourcePath", UTF8(nan)}});
        },
        "NaN polyline vertex must reject");
    const auto text = sourceDir / "text.dxf";
    Write(text, DXF("0\nTEXT\n10\n0\n20\n0\n40\n5\n1\nunsupported content\n"));
    Rejected(
        [&] {
          Call(endpoint, "PlaneDrawing", "Import",
               {{"sourcePath", UTF8(text)}});
        },
        "unsupported drawing content must not disappear silently");
    const auto crossing = sourceDir / "crossing.dxf";
    Write(crossing, DXF("0\nCIRCLE\n10\n0\n20\n0\n40\n2\n0\nCIRCLE\n10\n3\n20\n"
                        "0\n40\n2\n"));
    Rejected(
        [&] {
          Call(endpoint, "PlanePart", "Import",
               {{"sourcePath", UTF8(crossing)}});
        },
        "intersecting original circles must reject");
    const auto before =
        Call(endpoint, "PlaneScene", "ReadState").at("revision").To<Text>();
    Call(endpoint, "PlaneObject", "Update",
         {{"id", partId},
          {"kind", Text("parts")},
          {"name", Text("Updated")},
          {"quantity", 4},
          {"expectedRevision", before}});
    Rejected(
        [&] {
          Call(endpoint, "PlaneObject", "Update",
               {{"id", partId},
                {"kind", Text("parts")},
                {"quantity", 5},
                {"expectedRevision", before}});
        },
        "stale input revision must reject");
    Call(endpoint, "Project", "Undo");
    Check(Model(endpoint)
                  .at("parts")
                  .To<Array>()[0]
                  .To<Map>()
                  .at("quantity")
                  .To<UInt>() == 3,
          "framework undo failed");
    Call(endpoint, "Project", "Redo");
    Check(Model(endpoint)
                  .at("parts")
                  .To<Array>()[0]
                  .To<Map>()
                  .at("quantity")
                  .To<UInt>() == 4,
          "framework redo failed");
    const auto oldBranchRevision =
        Call(endpoint, "PlaneScene", "ReadState").at("revision").To<Text>();
    Call(endpoint, "Project", "Undo");
    Call(endpoint, "PlaneObject", "Update",
         {{"id", partId}, {"kind", Text("parts")}, {"quantity", 4}});
    Rejected(
        [&] {
          Call(endpoint, "PlaneObject", "Update",
               {{"id", partId},
                {"kind", Text("parts")},
                {"quantity", 6},
                {"expectedRevision", oldBranchRevision}});
        },
        "framework version must reject a stale command from an undone branch");
    Call(endpoint, "PlaneNesting", "SetParameters",
         {{"partGap", 2.},
          {"stockMargin", 3.},
          {"rotationStepDeg", 90.},
          {"timeLimitSeconds", .5},
          {"allowMirror", false}});
    const auto taskId =
        Call(endpoint, "PlaneNesting", "Run").at("taskId").To<Text>();
    Map task;
    for (int i = 0; i < 2000; ++i) {
      task = Call(endpoint, "PlaneNesting", "ReadTask", {{"taskId", taskId}});
      if (task.at("status").To<Text>() != "Running")
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(task.at("status").To<Text>() == "Completed",
          "real nesting task did not complete");
    const auto resultId = task.at("resultId").To<Text>();
    const auto report =
        Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", resultId}});
    Check(report.at("valid").To<bool>(), "invalid geometry published");
    Check(report.at("placed").To<UInt>() == 4, "demand not fulfilled");
    Check(!report.at("boards").To<Array>().empty(),
          "physical result board missing");
    Call(endpoint, "PlaneNesting", "SetParameters", {{"partGap", 1001.}});
    const auto failedTaskId =
        Call(endpoint, "PlaneNesting", "Run").at("taskId").To<Text>();
    Map failedTask;
    for (int i = 0; i < 2000; ++i) {
      failedTask = Call(endpoint, "PlaneNesting", "ReadTask",
                        {{"taskId", failedTaskId}});
      if (failedTask.at("status").To<Text>() != "Running")
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(failedTask.at("status").To<Text>() == "Failed",
          "invalid solver input must not publish a successful result");
    Call(endpoint, "PlaneNesting", "SetParameters", {{"partGap", 2.}});
    Check(failedTask.at("parameters").To<Map>().at("partGap").To<double>() ==
              1001.,
          "failed task must retain its own typed parameter snapshot");
    Check(
        !Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", resultId}})
             .at("stale")
             .To<bool>(),
        "restoring exact parameter facts must restore result freshness");
    const auto destination = sourceDir / "saved.ipcam";
    std::cerr << "[phase] SaveProjectFile begin\n" << std::flush;
    runtime->SaveProjectFile(project->GetProjectID(), UTF8(destination));
    std::cerr << "[phase] SaveProjectFile returned\n" << std::flush;
    std::cerr << "[phase] CloseProjectCatalog begin\n" << std::flush;
    Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()),
          "catalog close failed");
    std::cerr << "[phase] CloseProjectCatalog returned\n" << std::flush;
    std::filesystem::remove(part);
    std::filesystem::remove(stock);
    std::filesystem::remove(open);
    for (const auto *name :
         {"unitless.dxf", "endz.dxf", "nan.dxf", "text.dxf", "crossing.dxf"})
      std::filesystem::remove(sourceDir / name);
    std::cerr
        << "[phase] original source files removed; OpenProjectFile begin\n"
        << std::flush;
    auto reopened = runtime->OpenProjectFile(UTF8(destination));
    std::cerr << "[phase] OpenProjectFile returned\n" << std::flush;
    project = reopened->GetMainProject();
    endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(),
                                                    project->GetMainSceneID());
    diagnosticScene = &project->GetMainScene();
    std::cerr << "[phase] reopened scene endpoint acquired; ReadState begin\n"
              << std::flush;
    state = Model(endpoint);
    Check(state.at("parts").To<Array>().size() == 1 &&
              state.at("stocks").To<Array>().size() == 2,
          "ECS definitions failed to reopen without original files");
    const auto reopenedReport =
        Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", resultId}});
    const auto reopenedFailedTask =
        Call(endpoint, "PlaneNesting", "ReadTask", {{"taskId", failedTaskId}});
    Check(reopenedFailedTask.at("status").To<Text>() == "Failed" &&
              reopenedFailedTask.at("parameters")
                      .To<Map>()
                      .at("partGap")
                      .To<double>() == 1001.,
          "failed task and typed input parameters must survive save/reopen");
    Check(reopenedReport.at("placed") == report.at("placed") &&
              reopenedReport.at("boards") == report.at("boards"),
          "ECS result graph changed on reopen");
    Check(!Call(endpoint, "PlaneDrawing", "ReadGeometry", {{"id", drawing}})
               .at("curves")
               .To<Array>()
               .empty(),
          "embedded drawing geometry failed to reopen");
    Check(
        !Call(endpoint, "PlaneDrawing", "ReadGeometry",
              {{"id", importedDrawing}})
             .at("curves")
             .To<Array>()
             .empty(),
        "embedded imported drawing failed to reopen after deleting its source");
    Call(endpoint, "PlaneObject", "Update",
         {{"id", partId}, {"kind", Text("parts")}, {"quantity", 5}});
    Check(Model(endpoint)
              .at("results")
              .To<Array>()[0]
              .To<Map>()
              .at("stale")
              .To<bool>(),
          "result freshness did not follow input change");
    Call(endpoint, "Project", "Undo");
    Check(
        !Model(endpoint)
             .at("results")
             .To<Array>()[0]
             .To<Map>()
             .at("stale")
             .To<bool>(),
        "result must become current when exact typed input facts are restored");
    Call(endpoint, "Project", "Redo");
    Check(Model(endpoint)
              .at("results")
              .To<Array>()[0]
              .To<Map>()
              .at("stale")
              .To<bool>(),
          "redo must stale the result again");
    runtime->Stop();
    std::cerr << "[phase] product stopped\n" << std::flush;
    std::cout << "PASS: native product runtime import, drawing isolation, "
                 "exact query, revision, undo/redo, real solver, typed result "
                 "graph, save/reopen without sources, staleness.\n";
    std::cout << "Saved acceptance project: " << UTF8(destination) << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
