#include "OpenCascadeResourceImport/TubeCadDocumentService.h"
#include "OpenCascadeResourceImport/TubeCadJson.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <sstream>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <ShapeFix_Shape.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <ShapeUpgrade_ShapeDivideClosed.hxx>
#include <ShapeUpgrade_ShapeDivideClosedEdges.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <Standard_Failure.hxx>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <set>
#include <cmath>
#include <cstdlib>
#if defined(_WIN32)
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace G = iCAX::GeometryData;
namespace T = G::Tube;
using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;
using Clock = std::chrono::steady_clock;
namespace
{
void Require(bool condition, const std::string& reason)
{
    if (!condition) throw std::runtime_error(reason);
}
std::string Join(const std::vector<std::string>& values)
{
    std::string result;
    for (const auto& value : values) result += (result.empty() ? "" : "; ") + value;
    return result;
}
std::string InvalidDetails(const TopoDS_Shape& shape);
std::filesystem::path DiagnosticDirectory;
TopoDS_Shape Native(const G::BRepModel& model)
{
    const auto built = iCAX::OpenCascade::BuildOpenCascadeShape(model);
    Require(built.bOK && !built.Shape.IsNull(), "Neutral BRep rebuild failed");
    if (!BRepCheck_Analyzer(built.Shape).IsValid()) {
        if (!DiagnosticDirectory.empty()) {
            BRepTools::Write(built.Shape, (DiagnosticDirectory / "invalid-neutral-rebuild.brep").string().c_str());
            std::ofstream stream(DiagnosticDirectory / "invalid-neutral-rebuild.json", std::ios::binary);
            stream << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::CAD::TubeCadJson::Encode(model));
        }
        throw std::runtime_error("Neutral BRep rebuild is invalid:" + InvalidDetails(built.Shape));
    }
    return built.Shape;
}
double Volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties, 1e-10);
    const auto value = std::abs(properties.Mass());
    Require(std::isfinite(value), "Non-finite solid volume");
    return value;
}
std::string InvalidDetails(const TopoDS_Shape& shape)
{
    BRepCheck_Analyzer check(shape);
    std::string details;
    for (auto type : { TopAbs_EDGE, TopAbs_FACE, TopAbs_SHELL, TopAbs_SOLID }) {
        int index = 0;
        for (TopExp_Explorer it(shape, type); it.More(); it.Next()) {
            ++index;
            const auto result = check.Result(it.Current());
            const auto append = [&](const auto& statuses) {
                for (auto status : statuses) if (status != BRepCheck_NoError)
                    details += " type=" + std::to_string(type) + " index=" + std::to_string(index)
                        + " status=" + std::to_string(status);
            };
            append(result->Status());
            for (result->InitContextIterator(); result->MoreShapeInContext(); result->NextShapeInContext())
                append(result->StatusOnShape());
        }
    }
    return details;
}
ObjectMap PCurveDiagnostics(const TopoDS_Shape& shape)
{
    double maximum = 0.0; int inconsistent = 0, faces = 0;
    VariantArray examples;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        ++faces; const auto face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face, false);
        for (TopExp_Explorer edges(face, TopAbs_EDGE); edges.More(); edges.Next()) {
            const auto edge = TopoDS::Edge(edges.Current());
            if (BRep_Tool::Degenerated(edge) || !BRep_Tool::SameParameter(edge)) continue;
            BRepAdaptor_Curve curve(edge); BRepAdaptor_Curve2d pcurve(edge, face);
            double deviation = 0;
            for (int i = 0; i <= 8; ++i) {
                const double parameter = curve.FirstParameter() + (curve.LastParameter() - curve.FirstParameter()) * i / 8.0;
                const auto uv = pcurve.Value(parameter);
                deviation = std::max(deviation, curve.Value(parameter).Distance(surface.Value(uv.X(), uv.Y())));
            }
            maximum = std::max(maximum, deviation);
            if (deviation > std::max(1e-7, BRep_Tool::Tolerance(edge))) {
                ++inconsistent;
                if (examples.size() < 12) examples.emplace_back(ObjectMap{{"faceIndex", faces},
                    {"deviation", deviation}, {"edgeTolerance", BRep_Tool::Tolerance(edge)}});
            }
        }
    }
    return {{"maxDeviation", maximum}, {"inconsistentOccurrences", inconsistent}, {"examples", examples}};
}
TopoDS_Shape IndependentPeriodicBoundary(const TopoDS_Shape& copy, bool& subdivided)
{
    bool shifted = false;
    for (TopExp_Explorer it(copy,TopAbs_EDGE); it.More(); it.Next()) {
        const auto edge=TopoDS::Edge(it.Current());
        if (BRep_Tool::Degenerated(edge) || !BRep_Tool::IsClosed(edge)) continue;
        double first,last; const auto curve=BRep_Tool::Curve(edge,first,last);
        if (!curve.IsNull() && curve->IsPeriodic() && last-first >= curve->Period()-1e-9
            && (first<curve->FirstParameter()-1e-9 || last>curve->LastParameter()+1e-9)) { shifted=true; break; }
    }
    if (!shifted) return copy;
    // This oracle owns its normalization independently of the production
    // helper, and first proves the subdivision against the untouched operand.
    ShapeUpgrade_ShapeDivideClosedEdges split(BRepBuilderAPI_Copy(copy,true,false).Shape());
    split.SetNbSplitPoints(1); split.SetSurfaceSegmentMode(false); split.SetPrecision(1e-7); split.Perform();
    const auto normalized=split.Result(); Require(!normalized.IsNull(),"Independent periodic subdivision failed");
    BRepLib::SameParameter(normalized,1e-7,true);
    Require(BRepCheck_Analyzer(normalized).IsValid(),"Independent periodic subdivision is invalid");
    double change=0;
    for (int direction=0; direction<2; ++direction) {
        BRepAlgoAPI_Cut proof; NCollection_List<TopoDS_Shape> arguments,tools;
        arguments.Append(BRepBuilderAPI_Copy(direction?normalized:copy,true,false).Shape());
        tools.Append(BRepBuilderAPI_Copy(direction?copy:normalized,true,false).Shape());
        proof.SetArguments(arguments); proof.SetTools(tools); proof.SetNonDestructive(true); proof.SetToFillHistory(false); proof.Build();
        Require(proof.IsDone()&&!proof.Shape().IsNull()&&BRepCheck_Analyzer(proof.Shape()).IsValid(),"Cannot prove independent periodic subdivision");
        change+=Volume(proof.Shape());
    }
    Require(change<=std::max(1e-9,std::max(Volume(copy),Volume(normalized))*1e-8),"Independent periodic subdivision changes geometry");
    subdivided=true; return normalized;
}
ObjectMap Compare(const TopoDS_Shape& source, const TopoDS_Shape& result, double fuzzy = 0.0, bool streamCopy = false, bool nonDestructive = true, bool checkInverted = true)
{
    // Independent from the converter's replay validator. No fuzzy tolerance
    // here: compare the original STEP with the document's persisted replay.
    int periodicSubdivisions=0;
    const auto difference = [fuzzy, streamCopy, nonDestructive, checkInverted, &periodicSubdivisions](const TopoDS_Shape& a, const TopoDS_Shape& b) {
        const auto copy = [streamCopy](const TopoDS_Shape& shape) {
            if (!streamCopy) return BRepBuilderAPI_Copy(shape, true, false).Shape();
            std::stringstream stream;
            BRepTools::Write(shape, stream, false, false, TopTools_FormatVersion_CURRENT);
            TopoDS_Shape result; BRep_Builder builder;
            BRepTools::Read(result, stream, builder);
            Require(!result.IsNull(), "Cannot deserialize independent BRep copy");
            return result;
        };
        BRepAlgoAPI_Cut cut;
        NCollection_List<TopoDS_Shape> arguments, tools;
        bool leftSubdivided=false,rightSubdivided=false;
        arguments.Append(IndependentPeriodicBoundary(copy(a),leftSubdivided));
        tools.Append(IndependentPeriodicBoundary(copy(b),rightSubdivided));
        periodicSubdivisions+=static_cast<int>(leftSubdivided)+static_cast<int>(rightSubdivided);
        cut.SetArguments(arguments); cut.SetTools(tools);
        cut.SetToFillHistory(false); cut.SetNonDestructive(nonDestructive); cut.SetCheckInverted(checkInverted); cut.SetFuzzyValue(fuzzy); cut.Build();
        Require(cut.IsDone() && !cut.Shape().IsNull(), "Independent difference could not be built");
        Require(BRepCheck_Analyzer(cut.Shape()).IsValid(), "Independent difference is invalid:" + InvalidDetails(cut.Shape()));
        return Volume(cut.Shape());
    };
    const double missing = difference(source, result), extra = difference(result, source);
    const double sourceVolume = Volume(source), replayVolume = Volume(result);
    const double scale = std::max(sourceVolume, replayVolume);
    Require(scale > 1e-12, "Source has no measurable volume");
    const double allowed = std::max(1e-9, scale * 1e-8);
    return {{"missingVolumeMm3", missing}, {"extraVolumeMm3", extra},
        {"sourceVolumeMm3", sourceVolume}, {"replayVolumeMm3", replayVolume},
        {"relativeDifference", (missing + extra) / scale}, {"allowedDifferenceMm3", allowed},
        {"equivalent", missing + extra <= allowed}, {"exactPeriodicSubdivisions",periodicSubdivisions}};
}
ObjectMap BoundaryDiagnostics(const TopoDS_Shape& shape)
{
    VariantArray shells, faults, faces;
    for (TopExp_Explorer it(shape, TopAbs_SHELL); it.More(); it.Next()) {
        BRepCheck_Shell check(TopoDS::Shell(it.Current()));
        NCollection_List<TopoDS_Shape> connected;
        shells.emplace_back(ObjectMap{{"closedStatus", static_cast<int>(check.Closed())},
            {"orientationStatus", static_cast<int>(check.Orientation())},
            {"closedFlag", it.Current().Closed()}, {"orientation", static_cast<int>(it.Current().Orientation())},
            {"connectedSets", check.NbConnectedSet(connected)}});
    }
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        const auto face = TopoDS::Face(it.Current()); BRepAdaptor_Surface surface(face, false);
        double u0,u1,v0,v1; BRepTools::UVBounds(face,u0,u1,v0,v1);
        ObjectMap item{{"type", static_cast<int>(surface.GetType())},
            {"orientation", static_cast<int>(face.Orientation())},
            {"u0",u0},{"u1",u1},{"v0",v0},{"v1",v1}};
        if (surface.GetType()==GeomAbs_Cylinder) item["radius"] = surface.Cylinder().Radius();
        faces.emplace_back(item);
    }
    BOPAlgo_ArgumentAnalyzer analyzer;
    analyzer.SetShape1(shape); analyzer.OperationType()=BOPAlgo_UNKNOWN;
    analyzer.ArgumentTypeMode()=true; analyzer.SelfInterMode()=true;
    analyzer.SmallEdgeMode()=true; analyzer.RebuildFaceMode()=true;
    analyzer.CurveOnSurfaceMode()=true; analyzer.Perform();
    for (const auto& fault : analyzer.GetCheckResult()) faults.emplace_back(ObjectMap{
        {"status",static_cast<int>(fault.GetCheckStatus())},
        {"faultyShapes",fault.GetFaultyShapes1().Size()}, {"maxDistance",fault.GetMaxDistance1()}});
    GProp_GProps props; BRepGProp::VolumeProperties(shape,props);
    BRepClass3d_SolidClassifier classifier(shape); classifier.Perform(props.CentreOfMass(),1e-7);
    return {{"centreOfMassState", static_cast<int>(classifier.State())},
        {"shells",shells},{"faces",faces},{"booleanArgumentFaults",faults},
        {"booleanArgumentHasFaulty",analyzer.HasFaulty()}};
}
ObjectMap DifferenceDiagnostics(const TopoDS_Shape& source)
{
    BRepAlgoAPI_Cut cut;
    NCollection_List<TopoDS_Shape> arguments, tools;
    arguments.Append(BRepBuilderAPI_Copy(source, true, false).Shape());
    tools.Append(BRepBuilderAPI_Copy(source, true, false).Shape());
    cut.SetArguments(arguments); cut.SetTools(tools);
    cut.SetToFillHistory(false); cut.SetNonDestructive(true); cut.Build();
    GProp_GProps mass;
    BRepGProp::VolumeProperties(source, mass, 1e-10);
    VariantArray orientations;
    for (TopExp_Explorer it(source, TopAbs_SOLID); it.More(); it.Next()) {
        BRepClass3d_SolidClassifier classifier(it.Current());
        classifier.PerformInfinitePoint(1e-7);
        orientations.emplace_back(static_cast<int>(classifier.State()));
    }
    ObjectMap report{{"built", cut.IsDone()}, {"sourceSignedVolume", mass.Mass()},
        {"infinitePointStates", orientations}};
    if (!cut.IsDone() || cut.Shape().IsNull()) return report;
    const auto shape = cut.Shape();
    int solids = 0, shells = 0, faces = 0;
    VariantArray members;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next()) {
        ++solids;
        members.emplace_back(ObjectMap{{"valid", BRepCheck_Analyzer(it.Current()).IsValid()},
            {"volume", Volume(it.Current())}, {"errors", InvalidDetails(it.Current())}});
    }
    for (TopExp_Explorer it(shape, TopAbs_SHELL); it.More(); it.Next()) ++shells;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) ++faces;
    report["solids"] = solids; report["shells"] = shells; report["faces"] = faces;
    report["volume"] = Volume(shape); report["valid"] = BRepCheck_Analyzer(shape).IsValid();
    report["errors"] = InvalidDetails(shape); report["solidMembers"] = members;
    return report;
}
ObjectMap Describe(const T::CTubeCadDocument& document,
    const iCAX::CAD::ITubeCadDocumentService& service)
{
    std::map<std::string, const T::SSolidNode*> nodes;
    for (const auto& node : document.Construction.SolidNodes) nodes.emplace(node.ID, &node);
    std::set<std::string> active;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (!active.insert(id).second) return;
        const auto* node = nodes.at(id);
        std::visit([&](const auto& data) {
            using D = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<D, T::SBooleanNode>) for (const auto& child : data.Children) visit(child);
            else if constexpr (std::is_same_v<D, T::STransformNode>) visit(data.Child);
            else if constexpr (std::is_same_v<D, T::SWrappedVolumeNode>) visit(data.Support.ExtrusionNodeID);
            else if constexpr (std::is_same_v<D, T::SOpeningProfileSweepNode>) visit(data.SourceCutNodeID);
            else if constexpr (std::is_same_v<D, T::SAlternativeNode>) {
                const auto selection = data.SelectedCandidateID.empty() ? data.RecommendedCandidateID : data.SelectedCandidateID;
                const auto chosen = std::find_if(data.Candidates.begin(), data.Candidates.end(),
                    [&](const auto& candidate) { return candidate.ID == selection; });
                Require(chosen != data.Candidates.end(), "Alternative has no explicit chosen/recommended interpretation");
                visit(chosen->CandidateNodeID);
            }
        }, node->Data);
    };
    visit(document.Construction.RootNodeID);
    std::vector<std::string> sketchIDs;
    for (const auto& node : document.Construction.SolidNodes)
        if (std::holds_alternative<T::SExtrudedRegionNode>(node.Data)
            || std::holds_alternative<T::STaperedRegionNode>(node.Data)
            || std::holds_alternative<T::SWrappedVolumeNode>(node.Data)
            || std::holds_alternative<T::SOpeningProfileSweepNode>(node.Data)) sketchIDs.push_back(node.ID);
    const auto sketches = service.ReadSketches(document, sketchIDs);
    Require(sketches.size() == sketchIDs.size(), "Batch sketch projection omitted construction views");
    std::set<std::string> readable;
    for (std::size_t index = 0; index < sketches.size(); ++index) {
        Require(sketches[index].NodeID == sketchIDs[index] && sketches[index].Revision == document.Revision,
            "Batch sketch projection changed feature identity or document revision");
        readable.insert(sketches[index].NodeID);
    }
    ObjectMap allTypes, activeTypes;
    VariantArray summaries;
    int activeExactBodies = 0, readableSketches = 0, activeGeometricFeatures = 0;
    for (const auto& node : document.Construction.SolidNodes) {
        ObjectMap summary{{"id", node.ID}, {"label", node.Label}, {"active", active.contains(node.ID)}};
        std::visit([&](const auto& data) {
            using D = std::decay_t<decltype(data)>;
            const auto type = iCAX::CAD::TubeCadJson::Name<D>();
            summary["type"] = type;
            const auto increment = [&](ObjectMap& counts) {
                counts[type] = counts.contains(type) ? counts.at(type).To<int>() + 1 : 1;
            };
            increment(allTypes); if (active.contains(node.ID)) increment(activeTypes);
            if constexpr (std::is_same_v<D, T::SCompositeVolumeNode> || std::is_same_v<D, T::SResidualBRepNode>) {
                summary["exactBodyReference"] = data.BRepResourceID;
                if (active.contains(node.ID)) ++activeExactBodies;
            } else if constexpr (!std::is_same_v<D, T::SBooleanNode> && !std::is_same_v<D, T::SAlternativeNode>) {
                if (active.contains(node.ID) && node.ID != document.Construction.BaseNodeID) ++activeGeometricFeatures;
            }
            if constexpr (std::is_same_v<D, T::SExtrudedRegionNode> || std::is_same_v<D, T::STaperedRegionNode>
                || std::is_same_v<D, T::SWrappedVolumeNode> || std::is_same_v<D, T::SOpeningProfileSweepNode>) {
                summary["planarViewReadable"] = readable.contains(node.ID);
                if (active.contains(node.ID)) ++readableSketches;
            }
        }, node.Data);
        summaries.emplace_back(summary);
    }
    const auto& geometry = document.Construction;
    return {{"baseNodeId", geometry.BaseNodeID}, {"rootNodeId", geometry.RootNodeID},
        {"recognitionStatus", static_cast<int>(geometry.RecognitionStatus)},
        {"nodeCount", static_cast<int>(nodes.size())}, {"activeNodeCount", static_cast<int>(active.size())},
        {"allNodeTypes", allTypes}, {"activeNodeTypes", activeTypes}, {"nodes", summaries},
        {"activeExactBodyNodes", activeExactBodies}, {"activeGeometricFeatures", activeGeometricFeatures},
        {"activePlanarViews", readableSketches}, {"fullyParametricActiveGraph", activeExactBodies == 0},
        {"unparameterizedVolumeRatio", geometry.RelativeUnparameterizedVolume},
        {"opaqueVolumeRatio", geometry.RelativeOpaqueResidualVolume}};
}
}

int main(int argc, char** argv)
{
    Standard_Failure::SetDefaultStackTraceLength(32);
#if defined(_WIN32)
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* failure) -> LONG {
        const auto process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, nullptr, TRUE);
        auto context = *failure->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC = {context.Rip, AddrModeFlat};
        frame.AddrFrame = {context.Rbp, AddrModeFlat};
        frame.AddrStack = {context.Rsp, AddrModeFlat};
        std::cerr << "Native exception " << std::hex << failure->ExceptionRecord->ExceptionCode << std::dec << '\n';
        for (int index = 0; index < 32; ++index) {
            char storage[sizeof(SYMBOL_INFO) + 1024]{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = 1024;
            DWORD64 displacement = 0;
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
                std::cerr << symbol->Name << " + " << displacement;
            else std::cerr << "address " << std::hex << frame.AddrPC.Offset << std::dec;
            IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(line); DWORD delta = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &delta, &line))
                std::cerr << " " << line.FileName << ':' << line.LineNumber;
            std::cerr << '\n';
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame,
                &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        }
        std::cerr.flush();
        return EXCEPTION_EXECUTE_HANDLER;
    });
#endif
#if defined(_WIN32) && defined(_DEBUG)
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, [](int type, char* message, int*) -> int {
        if (type != _CRT_ASSERT) return 0;
        std::fputs(message, stderr); std::fflush(stderr); std::_Exit(3);
    });
#endif
    if (argc < 3 || argc > 5) { std::cerr << "Usage: TubeCadStepAcceptance source.step output-directory [--diagnose|--inspect-boundary|--profile-conversion|--exercise-wrapped-edit|--exercise-circle-edit|--exercise-section-edit|--verify-extracted-solids|--verify-assembly-documents] [--original-solid=N]\n"; return 2; }
    std::string option;
    int originalSolid = 0;
    for (int argument = 3; argument < argc; ++argument) {
        const std::string value = argv[argument];
        if (value.starts_with("--original-solid=")) {
            try {
                const auto text = value.substr(17); std::size_t consumed = 0;
                originalSolid = std::stoi(text, &consumed);
                if (originalSolid < 1 || consumed != text.size()) return 2;
            } catch (const std::exception&) { return 2; }
        } else if (option.empty()) option = value;
        else return 2;
    }
    if (!option.empty() && option != "--diagnose" && option != "--inspect-boundary" && option != "--profile-conversion" && option != "--benchmark-booleans" && option != "--exercise-wrapped-edit" && option != "--exercise-circle-edit" && option != "--exercise-section-edit" && option != "--verify-extracted-solids" && option != "--verify-assembly-documents") return 2;
    const std::filesystem::path directory(argv[2]);
    std::filesystem::create_directories(directory);
    DiagnosticDirectory = directory;
    const auto began = Clock::now();
    ObjectMap report{{"schemaVersion", 1}, {"source", std::string(argv[1])},
        {"status", std::string("running")}, {"passed", false}};
    ObjectMap timings;
    auto write = [&] {
        report["seconds"] = std::chrono::duration<double>(Clock::now() - began).count();
        report["timings"] = timings;
        std::ofstream stream(directory / "report.json", std::ios::binary | std::ios::trunc);
        stream << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report); stream.flush();
    };
    auto stage = [&](const std::string& name, auto&& action) {
        report["stage"] = name; write(); std::cout << name << std::endl;
        const auto start = Clock::now();
        action();
        timings[name] = std::chrono::duration<double>(Clock::now() - start).count(); write();
    };
    try {
        iCAX::Services::CServiceProvider services;
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
        const auto service = services.Resolve<iCAX::CAD::ITubeCadDocumentService>();
        Require(bool(service), "Neutral CAD service is not registered");
        TopoDS_Shape source;
        G::BRepModel neutral;
        iCAX::CAD::STubeCadEditResult imported;
        T::CTubeCadDocument restored;
        if (option == "--benchmark-booleans") {
            stage("loadDocumentSource", [&] {
                std::ifstream stream(argv[1], std::ios::binary);
                Require(bool(stream), "Cannot open benchmark document");
                const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
                restored = T::Persistence::DeserializeDocument(bytes);
                Require(T::Persistence::Serialize(restored) == bytes, "Benchmark document transport changed data");
                source = Native(restored.Bodies.at("source"));
                Require(BRepCheck_Analyzer(source).IsValid(), "Benchmark source is invalid");
            });
            std::stringstream before, after; BRepTools::Write(source, before);
            for (const bool parallel : {false, true}) {
                const std::string name = parallel ? "parallelSelfDifference" : "serialSelfDifference";
                stage(name, [&] {
                    BRepAlgoAPI_Cut cut;
                    NCollection_List<TopoDS_Shape> arguments, tools;
                    arguments.Append(BRepBuilderAPI_Copy(source, true, false).Shape());
                    tools.Append(BRepBuilderAPI_Copy(source, true, false).Shape());
                    cut.SetArguments(arguments); cut.SetTools(tools);
                    cut.SetNonDestructive(true); cut.SetToFillHistory(false);
                    cut.SetUseOBB(true); cut.SetRunParallel(parallel); cut.Build();
                    Require(cut.IsDone() && !cut.Shape().IsNull() && BRepCheck_Analyzer(cut.Shape()).IsValid(),
                        "Benchmark self difference could not be built");
                    const double difference = Volume(cut.Shape()); report[name + "VolumeMm3"] = difference;
                    Require(difference <= std::max(1e-9, Volume(source) * 1e-8), "Benchmark self difference changed geometry");
                });
            }
            BRepTools::Write(source, after);
            Require(before.str() == after.str(), "Boolean benchmark modified the source");
            report["sourceChangedDuringBenchmark"] = false;
            // A timing experiment is not a complete STEP import acceptance.
            report["status"] = std::string("benchmarked"); write(); return 0;
        }
        stage("readOriginalStep", [&] {
            STEPControl_Reader reader;
            Require(reader.ReadFile(argv[1]) == IFSelect_RetDone, "Cannot read STEP file");
            Require(reader.TransferRoots() > 0, "STEP has no transferred roots");
            source = reader.OneShape(); Require(!source.IsNull(), "STEP is empty");
            if (originalSolid) {
                int index = 0; TopoDS_Shape selected;
                for (TopExp_Explorer it(source, TopAbs_SOLID); it.More(); it.Next())
                    if (++index == originalSolid) { selected = it.Current(); break; }
                Require(!selected.IsNull(), "Original assembly solid index is out of range");
                source = selected; report["originalSolidIndex"] = originalSolid;
            }
            int solids = 0, faces = 0;
            for (TopExp_Explorer it(source, TopAbs_SOLID); it.More(); it.Next()) ++solids;
            for (TopExp_Explorer it(source, TopAbs_FACE); it.More(); it.Next()) ++faces;
            report["sourceSolids"] = solids; report["sourceFaces"] = faces;
            report["sourceValid"] = BRepCheck_Analyzer(source).IsValid();
            if (option == "--inspect-boundary") return;
            if (option == "--verify-extracted-solids" || option == "--verify-assembly-documents") return;
            if (solids != 1) report["status"] = std::string(solids > 1 ? "assembly" : "no-solid");
            Require(solids > 0, "STEP contains no solid part");
            Require(solids == 1, "Expected one tube part; multi-solid assembly requires per-part import");
            if (!BRepCheck_Analyzer(source).IsValid()) report["status"] = std::string("invalid-source");
            Require(BRepCheck_Analyzer(source).IsValid(), "Original STEP BRep is invalid");
        });
        if (option == "--inspect-boundary") {
            stage("inspectOriginalBoundary", [&] {
                report["sourceBoundaryErrors"] = InvalidDetails(source);
                report["sourceBoundary"] = BoundaryDiagnostics(source);
                report["sourcePCurves"] = PCurveDiagnostics(source);
                BRepTools::Write(source, (directory / "source.brep").string().c_str());
            });
            report["status"] = std::string("boundary-inspected"); write(); return 0;
        }
        stage("productionStepDecoder", [&] {
            const auto read = iCAX::OpenCascade::ReadBRepFile(argv[1]);
            Require(read.bOK, "Production STEP decoder: " + Join(read.Diagnostics)); neutral = read.Geometry;
            report["sourceHash"] = read.ContentHash;
            if (originalSolid) {
                const auto decoded = Native(G::Persistence::Deserialize(G::Persistence::Serialize(neutral)));
                int index = 0; TopoDS_Shape selected;
                for (TopExp_Explorer it(decoded, TopAbs_SOLID); it.More(); it.Next())
                    if (++index == originalSolid) { selected = it.Current(); break; }
                Require(!selected.IsNull(), "Decoded assembly solid index is out of range");
                neutral = G::Persistence::Deserialize(G::Persistence::Serialize(
                    iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(selected,"","",0.025)));
            }
        });
        if (option == "--verify-extracted-solids" || option == "--verify-assembly-documents") {
            stage("verifyAssemblyExtraction", [&] {
                std::ifstream input(directory / "extraction-input.json", std::ios::binary);
                Require(bool(input), "Missing extraction-input.json");
                const std::string text(std::istreambuf_iterator<char>(input), {});
                const auto children = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(text).To<VariantArray>();
                std::vector<TopoDS_Shape> originalSolids, decodedSolids;
                for (TopExp_Explorer it(source, TopAbs_SOLID); it.More(); it.Next()) originalSolids.push_back(it.Current());
                const auto decoded = Native(G::Persistence::Deserialize(G::Persistence::Serialize(neutral)));
                for (TopExp_Explorer it(decoded, TopAbs_SOLID); it.More(); it.Next()) decodedSolids.push_back(it.Current());
                Require(children.size() == originalSolids.size(), "Extraction inventory does not cover every original solid");
                Require(decodedSolids.size() == originalSolids.size(), "Production decoder changed assembly solid count");
                VariantArray results;
                std::set<int> indices;
                bool allPassed = true;
                for (const auto& entry : children) {
                    const auto child = entry.To<ObjectMap>();
                    const auto index = iCAX::CAD::TubeCadJson::Decode<int>(child.at("solidIndex"));
                    const auto path = child.at("sourcePath").To<std::string>();
                    Require(index > 0 && index <= static_cast<int>(originalSolids.size()) && indices.insert(index).second,
                        "Invalid or repeated original solid index");
                    ObjectMap result{{"solidIndex", index}, {"sourcePath", path}, {"passed", false}};
                    GProp_GProps signedMass;
                    BRepGProp::VolumeProperties(originalSolids[index - 1], signedMass);
                    result["originalSignedVolumeMm3"] = signedMass.Mass();
                    BRepClass3d_SolidClassifier classifier(originalSolids[index - 1]);
                    classifier.PerformInfinitePoint(1e-7);
                    result["originalInfiniteState"] = static_cast<int>(classifier.State());
                    const auto check = [&](const std::string& key, const TopoDS_Shape& left, const TopoDS_Shape& right) {
                        try {
                            result[key] = Compare(left, right);
                            return result.at(key).To<ObjectMap>().at("equivalent").To<bool>();
                        } catch (const Standard_Failure& failure) { result[key + "Error"] = std::string(failure.what()); }
                        catch (const std::exception& failure) { result[key + "Error"] = std::string(failure.what()); }
                        return false;
                    };
                    bool extractionOK = false;
                    try {
                        STEPControl_Reader reader;
                        Require(reader.ReadFile(path.c_str()) == IFSelect_RetDone && reader.TransferRoots() > 0,
                            "Cannot read archived extracted solid");
                        extractionOK = check("extractionComparison", originalSolids[index - 1], reader.OneShape());
                    } catch (const Standard_Failure& failure) { result["extractionComparisonError"] = std::string(failure.what()); }
                    catch (const std::exception& failure) { result["extractionComparisonError"] = std::string(failure.what()); }
                    // A bad historical extraction must not prevent verification
                    // of the current decoder against the original assembly.
                    const bool decoderOK = check("decoderComparison", originalSolids[index - 1], decodedSolids[index - 1]);
                    result["archiveVerified"] = extractionOK;
                    result["decoderVerified"] = decoderOK;
                    result["passed"] = extractionOK && decoderOK;
                    if (!extractionOK || !decoderOK) {
                        const bool originalOK = check("originalSelfComparison", originalSolids[index - 1], originalSolids[index - 1]);
                        check("decodedSelfComparison", decodedSolids[index - 1], decodedSolids[index - 1]);
                        result["originalBoundaryVerifiable"] = originalOK;
                        result["classification"] = std::string(!originalOK ? "unverifiable-original-boundary"
                            : !decoderOK ? "decoder-geometry-mismatch" : "invalid-archived-extraction");
                    } else result["classification"] = std::string("verified");
                    if (option == "--verify-assembly-documents") {
                        bool documentOK=false;
                        try {
                            const auto part=G::Persistence::Deserialize(G::Persistence::Serialize(
                                iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(decodedSolids[index-1],"","",0.025)));
                            const auto importedPart=service->Import(part);
                            const auto bytes=T::Persistence::Serialize(importedPart.Document);
                            const auto partPath=directory/("solid-"+std::to_string(index)+".tcad");
                            { std::ofstream out(partPath,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); Require(bool(out),"Cannot save assembly part document"); }
                            std::ifstream saved(partPath,std::ios::binary);
                            const std::vector<std::uint8_t> disk{std::istreambuf_iterator<char>(saved),std::istreambuf_iterator<char>()};
                            const auto reopened=T::Persistence::DeserializeDocument(disk);
                            Require(T::Persistence::Serialize(reopened)==bytes,"Assembly part document changed on disk");
                            documentOK=check("documentComparison",originalSolids[index-1],Native(service->Evaluate(reopened).Shape));
                            const auto expression=Describe(reopened,*service);
                            result["documentNodeCount"]=expression.at("nodeCount");
                            result["activeExactBodyNodes"]=expression.at("activeExactBodyNodes");
                            result["activePlanarViews"]=expression.at("activePlanarViews");
                        } catch (const Standard_Failure& error) { result["documentError"]=std::string(error.what()); }
                        catch (const std::exception& error) { result["documentError"]=std::string(error.what()); }
                        result["documentVerified"]=documentOK;
                        result["passed"]=result.at("passed").To<bool>() && documentOK;
                        if (decoderOK && extractionOK && !documentOK) result["classification"]=std::string("neutral-document-conversion-rejected");
                    }
                    allPassed = allPassed && result.at("passed").To<bool>();
                    results.emplace_back(result);
                    report["solids"] = results; write();
                }
                report["passed"] = allPassed;
            });
            report["status"] = std::string(report.at("passed").To<bool>() ? "assembly-extraction-verified" : "assembly-extraction-failed");
            write(); return report.at("passed").To<bool>() ? 0 : 1;
        }
        if (option == "--profile-conversion") {
            stage("profileConversion", [&] {
                const auto input = Native(neutral);
                std::stringstream inputBefore, inputAfter;
                BRepTools::Write(input, inputBefore);
                iCAX::OpenCascade::STubeCSGConversionOptions options;
                options.SourceBRepResourceID = "source";
                options.RemovalBRepResourceID = "removal";
                options.AdditionBRepResourceID = "addition";
                options.StageCompleted = [&](const std::string& name, double milliseconds) {
                    timings["conversion." + name] = milliseconds / 1000.0;
                    report["sourceValidAfter." + name] = BRepCheck_Analyzer(input).IsValid();
                    if (!BRepCheck_Analyzer(input).IsValid()) {
                        report["sourceErrorsAfter." + name] = InvalidDetails(input);
                        BRepTools::Write(input, (directory / ("invalid-after-" + name + ".brep")).string().c_str());
                    }
                    report["conversionStage"] = name; write();
                    std::cout << name << " " << milliseconds << "ms" << std::endl;
                };
                const auto converted = iCAX::OpenCascade::ConvertBRepToTubeCSG(input, options);
                BRepTools::Write(input, inputAfter);
                report["sourceChangedDuringConversion"] = inputBefore.str() != inputAfter.str();
                std::string diagnostics = Join(converted.ReplayValidation.Diagnostics);
                for (const auto& diagnostic : converted.Geometry.Diagnostics)
                    diagnostics += "; " + diagnostic.Code + ": " + diagnostic.Message;
                report["conversionDiagnostics"] = diagnostics;
                std::ofstream expression(directory / "expression.json", std::ios::binary);
                expression << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::CAD::TubeCadJson::Encode(converted.Geometry));
                Require(converted.ReplayValidation.bEquivalent, "Profiled conversion failed: " + diagnostics);
            });
            // Profiling is not the complete persistence/import acceptance.
            report["status"] = std::string("profiled"); write(); return 0;
        }
        if (option == "--diagnose") {
            report["selfDifferenceDiagnostics"] = DifferenceDiagnostics(source);
            stage("inspectDecoderBoundary", [&] {
                const auto decoded = Native(neutral);
                report["sourcePCurves"] = PCurveDiagnostics(source);
                report["sourceBoundary"] = BoundaryDiagnostics(source);
                ShapeUpgrade_ShapeDivideClosedEdges edgeDivide(BRepBuilderAPI_Copy(source,true,false).Shape());
                edgeDivide.SetNbSplitPoints(1); edgeDivide.SetSurfaceSegmentMode(false); edgeDivide.SetPrecision(1e-7); edgeDivide.Perform();
                report["edgeDividedBeforeErrors"]=InvalidDetails(edgeDivide.Result());
                BRepLib::SameParameter(edgeDivide.Result(),1e-7,true);
                report["edgeDividedAfterErrors"]=InvalidDetails(edgeDivide.Result());
                report["edgeDividedValid"]=!edgeDivide.Result().IsNull() && BRepCheck_Analyzer(edgeDivide.Result()).IsValid();
                try { report["edgeDividedSelfComparison"] = Compare(edgeDivide.Result(),edgeDivide.Result()); }
                catch (const Standard_Failure& error) { report["edgeDividedSelfComparisonError"] = std::string(error.what()); }
                catch (const std::exception& error) { report["edgeDividedSelfComparisonError"] = std::string(error.what()); }
                try { report["edgeDividedSourceComparison"] = Compare(source,edgeDivide.Result()); }
                catch (const Standard_Failure& error) { report["edgeDividedSourceComparisonError"] = std::string(error.what()); }
                catch (const std::exception& error) { report["edgeDividedSourceComparisonError"] = std::string(error.what()); }
                ShapeUpgrade_ShapeDivideClosed divide(BRepBuilderAPI_Copy(source,true,false).Shape());
                divide.SetNbSplitPoints(1); divide.SetSurfaceSegmentMode(false); divide.SetPrecision(1e-7);
                divide.Perform(); const auto divided=divide.Result();
                report["dividedValid"]=!divided.IsNull() && BRepCheck_Analyzer(divided).IsValid();
                ShapeFix_Shape fix(BRepBuilderAPI_Copy(source,true,false).Shape()); fix.SetPrecision(1e-7); fix.Perform();
                report["shapeFixValid"]=BRepCheck_Analyzer(fix.Shape()).IsValid();
                try { report["shapeFixSelfComparison"] = Compare(fix.Shape(),fix.Shape()); }
                catch (const Standard_Failure& error) { report["shapeFixSelfComparisonError"] = std::string(error.what()); }
                catch (const std::exception& error) { report["shapeFixSelfComparisonError"] = std::string(error.what()); }
                report["shapeFixBoundary"]=BoundaryDiagnostics(fix.Shape());

                try { report["dividedSelfComparison"] = Compare(divided,divided); }
                catch (const Standard_Failure& error) { report["dividedSelfComparisonError"] = std::string(error.what()); }
                catch (const std::exception& error) { report["dividedSelfComparisonError"] = std::string(error.what()); }
                try { report["dividedSourceComparison"] = Compare(source,divided); }
                catch (const Standard_Failure& error) { report["dividedSourceComparisonError"] = std::string(error.what()); }
                catch (const std::exception& error) { report["dividedSourceComparisonError"] = std::string(error.what()); }

                report["decodedBoundary"] = BoundaryDiagnostics(decoded);
                try { report["serializedSelfComparison"] = Compare(source, source, 0.0, true); }
                catch (const Standard_Failure& error) { report["serializedSelfComparisonError"] = std::string(error.what()); }
                catch (const std::exception& error) { report["serializedSelfComparisonError"] = std::string(error.what()); }
                for (const auto& [name, fuzzy, nonDestructive, checkInverted] : std::vector<std::tuple<std::string,double,bool,bool>>{
                    {"precisionSelfComparison",1e-7,true,true},
                    {"mutableCopiesSelfComparison",0.0,false,true},
                    {"knownFiniteSelfComparison",0.0,true,false}}) {
                    try { report[name] = Compare(source,source,fuzzy,false,nonDestructive,checkInverted); }
                    catch (const Standard_Failure& error) { report[name+"Error"] = std::string(error.what()); }
                    catch (const std::exception& error) { report[name+"Error"] = std::string(error.what()); }
                }
                report["decodedPCurves"] = PCurveDiagnostics(decoded);
                report["sourceExactCheckValid"] = BRepCheck_Analyzer(source, true, false, true).IsValid();
                BRepTools::Write(source, (directory / "source.brep").string().c_str());
                BRepTools::Write(decoded, (directory / "decoded.brep").string().c_str());
                try { report["sourceSelfComparison"] = Compare(source, source); }
                catch (const std::exception& error) { report["sourceSelfComparisonError"] = std::string(error.what()); }
                const auto repaired = BRepBuilderAPI_Copy(source, true, false).Shape();
                BRepLib::SameParameter(repaired, 1e-7, true);
                report["sameParameterPCurves"] = PCurveDiagnostics(repaired);
                report["sameParameterValid"] = BRepCheck_Analyzer(repaired).IsValid();
                try { report["sameParameterSelfComparison"] = Compare(repaired, repaired); }
                catch (const std::exception& error) { report["sameParameterSelfComparisonError"] = std::string(error.what()); }
                ShapeUpgrade_UnifySameDomain unify(BRepBuilderAPI_Copy(source, true, false).Shape(), true, true, false);
                unify.SetLinearTolerance(1e-7); unify.Build();
                report["unifiedValid"] = BRepCheck_Analyzer(unify.Shape()).IsValid();
                try { report["unifiedSelfComparison"] = Compare(unify.Shape(), unify.Shape()); }
                catch (const std::exception& error) { report["unifiedSelfComparisonError"] = std::string(error.what()); }
                try { report["unifiedSourceComparison"] = Compare(source, unify.Shape()); }
                catch (const std::exception& error) { report["unifiedSourceComparisonError"] = std::string(error.what()); }
            });
            stage("verifyDecoderGeometry", [&] {
                const auto comparison = Compare(source, Native(neutral)); report["decoderComparison"] = comparison;
                Require(comparison.at("equivalent").To<bool>(), "STEP decoder changed geometry before tube recognition");
            });
            stage("diagnoseRawConversion", [&] {
                iCAX::OpenCascade::STubeCSGConversionOptions options;
                options.SourceBRepResourceID = "source";
                options.RemovalBRepResourceID = "removal";
                options.AdditionBRepResourceID = "addition";
                const auto converted = iCAX::OpenCascade::ConvertBRepToTubeCSG(Native(neutral), options);
                const auto& validation = converted.ReplayValidation;
                report["rawConversion"] = ObjectMap{{"evaluated", validation.bEvaluated},
                    {"equivalent", validation.bEquivalent}, {"relativeDifference", validation.RelativeVolumeError},
                    {"missingVolumeMm3", validation.MissingVolume}, {"extraVolumeMm3", validation.UnexpectedVolume},
                    {"diagnostics", Join(validation.Diagnostics)}};
                std::ofstream expression(directory / "rejected-expression.json", std::ios::binary);
                expression << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::CAD::TubeCadJson::Encode(converted.Geometry));
                iCAX::OpenCascade::STubeCSGEvaluationOptions replayOptions;
                replayOptions.ExternalBRepShapes.emplace("source", Native(neutral));
                if (!converted.RemovalShape.IsNull()) replayOptions.ExternalBRepShapes.emplace("removal", converted.RemovalShape);
                if (!converted.AdditionShape.IsNull()) replayOptions.ExternalBRepShapes.emplace("addition", converted.AdditionShape);
                const auto replay = iCAX::OpenCascade::EvaluateTubeCSG(converted.Geometry, replayOptions);
                report["rawEvaluation"] = ObjectMap{{"built", replay.bOK},
                    {"valid", !replay.Shape.IsNull() && BRepCheck_Analyzer(replay.Shape).IsValid()},
                    {"diagnostics", Join(replay.Diagnostics)}};
                if (!replay.Shape.IsNull()) {
                    BRepTools::Write(replay.Shape, (directory / "raw-replay.brep").string().c_str());
                    report["rawReplayBoundary"] = BoundaryDiagnostics(replay.Shape);
                    report["rawReplayPCurves"] = PCurveDiagnostics(replay.Shape);
                    try { report["rawIndependentComparison"] = Compare(source, replay.Shape); }
                    catch (const std::exception& error) { report["rawIndependentComparisonError"] = std::string(error.what()); }
                }
                replayOptions.Tolerance = 1e-7;
                const auto precise = iCAX::OpenCascade::ValidateTubeCSGReplay(Native(neutral), converted.Geometry, replayOptions);
                report["preciseValidation"] = ObjectMap{{"equivalent", precise.bEquivalent},
                    {"evaluated", precise.bEvaluated}, {"relativeDifference", precise.RelativeVolumeError},
                    {"diagnostics", Join(precise.Diagnostics)}};
                Require(validation.bEquivalent, "Raw tube conversion failed: " + Join(validation.Diagnostics));
            });
        }
        stage("importNeutralDocument", [&] { imported = service->Import(neutral); });
        stage("inspectExpression", [&] {
            report["expression"] = Describe(imported.Document, *service);
            VariantArray diagnostics;
            for (const auto& text : imported.Evaluation.Diagnostics) diagnostics.emplace_back(text);
            report["evaluationDiagnostics"] = diagnostics;
            report["unavailableConstructionCount"] = static_cast<int>(imported.Evaluation.UnavailableConstructionIDs.size());
        });
        stage("persistAndReload", [&] {
            const auto bytes = T::Persistence::Serialize(imported.Document);
            const auto path = directory / "document.tcad";
            { std::ofstream stream(path, std::ios::binary); stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); Require(bool(stream), "Cannot persist neutral document"); }
            std::ifstream stream(path, std::ios::binary);
            const std::vector<std::uint8_t> saved{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            restored = T::Persistence::DeserializeDocument(saved);
            Require(T::Persistence::Serialize(restored) == bytes, "Persistence changed the neutral expression");
            report["documentBytes"] = static_cast<unsigned long long>(bytes.size());
            // An inspectable geometric expression, separate from the source
            // evidence bodies, shows which nodes were actually recovered.
            std::ofstream expression(directory / "expression.json", std::ios::binary);
            expression << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::CAD::TubeCadJson::Encode(restored.Construction));
        });
        stage("independentReplayAgainstOriginalStep", [&] {
            const auto replay = Native(service->Evaluate(restored).Shape);
            const auto comparison = Compare(source, replay); report["comparison"] = comparison;
            Require(comparison.at("equivalent").To<bool>(), "Persisted expression is not geometrically equivalent to original STEP");
        });
        if (option == "--exercise-wrapped-edit" || option == "--exercise-circle-edit" || option == "--exercise-section-edit") stage("importedFeatureEditRoundtrip", [&] {
            const bool wrapped = option == "--exercise-wrapped-edit";
            const bool sectionEdit = option == "--exercise-section-edit";
            std::set<std::string> active;
            for (const auto& item : report.at("expression").To<ObjectMap>().at("nodes").To<VariantArray>()) {
                const auto summary = item.To<ObjectMap>();
                if (summary.at("active").To<bool>()) active.insert(summary.at("id").To<std::string>());
            }
            const auto node = std::find_if(restored.Construction.SolidNodes.begin(), restored.Construction.SolidNodes.end(),
                [&](const auto& item) {
                    if (!active.contains(item.ID) || (sectionEdit ? item.ID != restored.Construction.BaseNodeID : item.ID == restored.Construction.BaseNodeID)) return false;
                    if (wrapped) return std::holds_alternative<T::SWrappedVolumeNode>(item.Data);
                    const auto* extrusion = std::get_if<T::SExtrudedRegionNode>(&item.Data);
                    return extrusion && std::any_of(extrusion->Section.Boundaries.begin(),extrusion->Section.Boundaries.end(),
                        [](const auto& loop) { return loop.Curves.size()==1 && std::holds_alternative<G::Circle2>(loop.Curves.front().Segment.Curve); });
                });
            Require(node != restored.Construction.SolidNodes.end(), "Edit exercise requires an active imported construction feature");
            auto originalView = service->ReadSketch(restored, node->ID);
            auto editedView = originalView;
            if (wrapped) for (auto& loop : std::get<T::CPlanarRegion2>(editedView.Geometry).Boundaries)
                for (auto& curve : loop.Curves) {
                    auto* polyline = std::get_if<G::Polyline2>(&curve.Segment.Curve);
                    Require(polyline != nullptr, "Wrapped edit exercise requires polygonal UV boundaries");
                    for (auto& point : polyline->Points) point.Y += 5.0;
                }
            else {
                auto& loops=std::get<T::CPlanarRegion2>(editedView.Geometry).Boundaries;
                const auto circle=std::find_if(loops.begin(),loops.end(),[](const auto& loop) {
                    return loop.Curves.size()==1 && std::holds_alternative<G::Circle2>(loop.Curves.front().Segment.Curve); });
                Require(circle!=loops.end(),"Selected sketch has no circular contour");
                std::get<G::Circle2>(circle->Curves.front().Segment.Curve).Radius+=1.0;
            }
            auto edited = service->EditSketch(restored, editedView);
            Require(!Compare(source, Native(edited.Evaluation.Shape)).at("equivalent").To<bool>(),
                "Imported 2D sketch edit did not change the 3D solid");
            if (!wrapped) {
                auto spatial = edited.Document.Construction;
                const auto base = std::find_if(spatial.SolidNodes.begin(), spatial.SolidNodes.end(),
                    [&](const auto& item) { return item.ID == spatial.BaseNodeID; });
                const auto axis = std::get<T::SExtrudedRegionNode>(base->Data).Frame.ZDirection;
                const auto feature = std::find_if(spatial.SolidNodes.begin(), spatial.SolidNodes.end(),
                    [&](const auto& item) { return item.ID == node->ID; });
                auto& frame = std::get<T::SExtrudedRegionNode>(feature->Data).Frame;
                frame.Location.X += 5.0 * axis.X; frame.Location.Y += 5.0 * axis.Y; frame.Location.Z += 5.0 * axis.Z;
                const auto moved = service->Edit(edited.Document, edited.Document.Revision, spatial);
                Require(!Compare(Native(edited.Evaluation.Shape), Native(moved.Evaluation.Shape)).at("equivalent").To<bool>(),
                    "Imported 3D feature move did not change the solid");
                const auto viewAfter3D = service->ReadSketch(moved.Document, node->ID);
                Require(viewAfter3D.ConstructionFrame &&
                    iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::CAD::TubeCadJson::Encode(*viewAfter3D.ConstructionFrame))
                        == iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::CAD::TubeCadJson::Encode(frame)),
                    "3D move was not visible in the 2D construction placement");
                Require(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::CAD::TubeCadJson::Encode(viewAfter3D.Geometry))
                    == iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::CAD::TubeCadJson::Encode(editedView.Geometry)),
                    "3D edit lost the earlier 2D curve edit");
                edited = moved;
            }
            const auto bytes = T::Persistence::Serialize(edited.Document);
            const auto path = directory / "edited-document.tcad";
            { std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); Require(bool(out), "Cannot save edited document"); }
            std::ifstream in(path, std::ios::binary);
            const std::vector<std::uint8_t> disk{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            const auto reloaded = T::Persistence::DeserializeDocument(disk);
            Require(T::Persistence::Serialize(reloaded) == bytes, "Edited neutral data changed on disk roundtrip");
            Require(reloaded.Construction.SolidNodes.size() == restored.Construction.SolidNodes.size(), "Edit lost construction nodes");
            Require(reloaded.Bodies.size() == restored.Bodies.size(), "Edit lost source evidence bodies");
            for (const auto& [id, body] : restored.Bodies)
                Require(G::Persistence::Serialize(reloaded.Bodies.at(id)) == G::Persistence::Serialize(body), "Edit changed source evidence");
            const auto changed = Native(service->Evaluate(reloaded).Shape);
            const auto persistedComparison = Compare(Native(edited.Evaluation.Shape), changed);
            Require(persistedComparison.at("equivalent").To<bool>(), "Edited document changed geometry after disk reload");
            const auto originalComparison = Compare(source, changed);
            Require(!originalComparison.at("equivalent").To<bool>(), "Feature edit did not change the 3D solid");
            originalView.Revision = reloaded.Revision;
            const auto undone = wrapped ? service->EditSketch(reloaded, originalView)
                : service->Edit(reloaded, reloaded.Revision, restored.Construction);
            const auto restoredComparison = Compare(source, Native(undone.Evaluation.Shape));
            Require(restoredComparison.at("equivalent").To<bool>(), "Restoring the original sketch did not restore the original STEP solid");
            report[wrapped ? "wrappedEditRoundtrip" : sectionEdit ? "sectionEditRoundtrip" : "circleEditRoundtrip"] = ObjectMap{{"passed", true}, {"nodeId", node->ID},
                {"editedPersistenceComparison", persistedComparison}, {"changedVsOriginal", originalComparison},
                {"restoredVsOriginal", restoredComparison}};
        });
        report["status"] = std::string("passed"); report["passed"] = true; write(); return 0;
    } catch (const Standard_Failure& error) {
        report["error"] = std::string(error.what());
        std::cerr << error.what() << '\n' << error.GetStackString() << '\n';
    } catch (const std::exception& error) { report["error"] = std::string(error.what()); }
    if (report.at("status").To<std::string>() == "running") report["status"] = std::string("failed");
    write(); return 1;
}
