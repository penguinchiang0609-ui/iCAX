#include "pch.h"
#include <TubeDesigner/PunchGeometry.h>
#include <TubeDesigner/PartDrawingDefinition.h>
#include <TubeDesigner/FinalGeometryMeasurement.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <gp_Ax2.hxx>
#include <TemplateRuntime/PythonTemplateHost.h>
#include <TemplateRuntime/TemplateCodec.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <BRepBuilderAPI_Transform.hxx>
#include <filesystem>
#include <TubeDesigner/TubeDesignerComponents.h>
#include <Database/IRepository.h>
#include <Database/MetaRegistrationCatalog.h>
#include <ProjectFile/ProjectFile.h>
#include <Resources/ResourceLibrary.h>
#include <GeometryData/BRepPersistence.h>
#include <STEPControl_Writer.hxx>
#include <stdexcept>
#include <limits>
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdlib>
#endif
using namespace iCAX::TubeDesigner;
namespace {
#if defined(_WIN32) && defined(_DEBUG)
// Unattended regressions must report CRT failures and exit, not wait in a modal dialog.
const bool nonInteractiveCrtReports = [] {
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    return true;
}();
#endif
TopoDS_Shape rectTube() {
    return BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(0,-20,-10),1000,40,20).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(-1,-18,-8),1002,36,16).Shape()).Shape();
}
TopoDS_Shape roundTube() {
    return BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(1,0,0)),20,1000).Shape(),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1,0,0),gp_Dir(1,0,0)),18,1002).Shape()).Shape();
}
bool inside(const TopoDS_Shape& shape, double x, double y, double z) {
    return BRepClass3d_SolidClassifier(shape,gp_Pnt(x,y,z),1e-6).State()==TopAbs_IN;
}
double mass(const TopoDS_Shape& shape) { GProp_GProps p; BRepGProp::VolumeProperties(shape,p); return p.Mass(); }
SPunchFeature hole() { SPunchFeature f; f.Station=500; f.Diameter=10; return f; }

using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;
ObjectMap templateRequest(ObjectMap input, bool missingTools=false,const std::string& runtimeFile="") {
    const auto root=std::filesystem::current_path();
    const auto worker=root/"src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py";
    iCAX::TemplateRuntime::CPythonTemplateHost host({root/"src/x64/Debug/runtime/python/python312.dll",worker,worker.parent_path()});
    return host.Invoke({{"protocol",std::string("icax.template-runtime")},{"protocolVersion",1ull},
        {"operation",std::string("evaluate")},{"templatePath",(root/(!runtimeFile.empty()?runtimeFile:missingTools?
            "src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/MissingPunchToolRuntimeFixture.py":
            "src/apps/tube-designer/templates/_shared/punch_tool_runtime.py")).string()},
        {"template",ObjectMap{{"id",std::string(missingTools?"punch-test-missing":"punch-test")},{"version",std::string("1.0.0")},{"packageDigest",std::string("test")}}},
        {"parameters",input},{"context",ObjectMap{}}});
}
ObjectMap testBounds() { return {{"min",VariantArray{0.,-20.,-10.}},{"max",VariantArray{1000.,20.,10.}}}; }
TopoDS_Shape templateShape(const ObjectMap& snapshot) {
    auto g=snapshot.at("geometry").To<ObjectMap>();
    if(g.at("mode").To<std::string>()=="profile") {
        const ObjectMap model{{"schema",std::string("icax.neutral-model")},{"schemaVersion",1ull},
            {"template",ObjectMap{{"id",std::string("test")},{"version",std::string("1.0.0")},{"packageDigest",std::string("test")}}},
            {"geometry",VariantArray{ObjectMap{{"key",std::string("profile")},{"operator",std::string("profile2d")},
            {"arguments",ObjectMap{{"placement",ObjectMap{{"origin",VariantArray{0.,0.,0.}},{"xAxis",VariantArray{1.,0.,0.}},{"yAxis",VariantArray{0.,1.,0.}}}},
                {"contours",g.at("contours")}}}}}}};
        return iCAX::OpenCascade::EvaluateNeutralModel(iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(model),{"profile"}).At("profile");
    }
    const auto key=g.at("outputKey").To<std::string>();
    return iCAX::OpenCascade::EvaluateNeutralModel(iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(g.at("model")),{key}).At(key);
}
SPunchFeature templateHole(const std::string& id, ObjectMap parameters={}) {
    const auto prepared=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"features",VariantArray{ObjectMap{{"toolRef",ObjectMap{{"id",id}}},{"toolParameters",parameters}}}}});
    const auto snapshot=prepared.at("features").To<VariantArray>()[0].To<ObjectMap>().at("toolSnapshot").To<ObjectMap>();
    auto f=hole(); f.Type=id; f.ToolShape=templateShape(snapshot);
    f.ToolIsProfile=snapshot.at("geometry").To<ObjectMap>().at("mode").To<std::string>()=="profile";
    PreparePunchToolFootprint(f);
    return f;
}
SPunchEnd templateEnd(const std::string& id,bool start,ObjectMap parameters={},double trim=0,ObjectMap section={},double rotation=0) {
    ObjectMap item{{"type",std::string("template")},{"trim",trim},{"rotation",rotation},
        {"toolRef",ObjectMap{{"id",id}}},{"toolParameters",parameters}};
    if(!section.empty()) item["section"]=section;
    const auto prepared=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"ends",ObjectMap{{start?"start":"end",item}}}});
    const auto saved=prepared.at("ends").To<ObjectMap>().at(start?"start":"end").To<ObjectMap>();
    const auto snapshot=saved.at("toolSnapshot").To<ObjectMap>();
    SPunchEnd e; e.Type="template";e.Trim=trim;e.Rotation=rotation;e.ToolShape=templateShape(snapshot);e.TemplateData=saved;
    const auto g=snapshot.at("geometry").To<ObjectMap>();
    if(!g.at("datumCenter").Is<std::monostate>()){e.HasDatumCenter=true;e.DatumCenter=g.at("datumCenter").To<double>();}
    if(g.contains("requireEndContact"))e.RequireEndContact=g.at("requireEndContact").To<bool>();
    if(g.contains("requireRetainedEndMaterial"))e.RequireRetainedEndMaterial=g.at("requireRetainedEndMaterial").To<bool>();
    if(g.at("coordinateSpace").To<std::string>()=="end-local") {
        const double sign=start?1:-1,angle=rotation*3.14159265358979323846/180;
        gp_Trsf placement;placement.SetValues(sign,0,0,(start?0:1000)+sign*trim,
            0,std::cos(angle),-sign*std::sin(angle),0,0,std::sin(angle),sign*std::cos(angle),0);
        e.ToolShape=BRepBuilderAPI_Transform(e.ToolShape,placement,true).Shape();
    }
    return e;
}
SPunchFeature partTool(const std::string& id,ObjectMap parameters={},double station=500,double radius=6,ObjectMap bounds={}) {
    const ObjectMap section{{"profile",ObjectMap{{"contours",VariantArray{
        ObjectMap{{"kind",std::string("circle")},{"center",VariantArray{0.,0.}},{"radius",radius}},
        ObjectMap{{"kind",std::string("circle")},{"center",VariantArray{0.,0.}},{"radius",radius-1}}}}}}};
    const auto prepared=templateRequest({{"action",std::string("prepare")},{"bounds",bounds.empty()?testBounds():bounds},
        {"features",VariantArray{ObjectMap{{"id",std::string("part-tool")},{"toolTarget",std::string("part")},
            {"station",station},{"reference",std::string("start")},{"section",section},
            {"toolRef",ObjectMap{{"id",id}}},{"toolParameters",parameters}}}}});
    const auto item=prepared.at("features").To<VariantArray>()[0].To<ObjectMap>();
    SPunchFeature f;f.ID="part-tool";f.Station=station;f.Type=id;f.ToolInPartCoordinates=true;
    const auto snapshot=item.at("toolSnapshot").To<ObjectMap>();
    f.ToolInPartLocalCoordinates=snapshot.at("geometry").To<ObjectMap>().at("coordinateSpace").To<std::string>()=="part-local";
    f.ToolShape=templateShape(snapshot);f.TemplateData=item;
    return f;
}
}
TEST(PartDrawing, BranchBoresBothWallsAndUsesExactInclinedPlacement) {
    for(double angle:{90.,45.,135.}) {
        SCOPED_TRACE(angle);
        auto f=partTool("branch-profile",{{"angle",angle}});
        const auto result=BuildPunchGeometry(rectTube(),{f});
        const double dx=angle==90?0:angle==45?9:-9;
        EXPECT_FALSE(inside(result,500+dx,0,9));EXPECT_FALSE(inside(result,500-dx,0,-9));
        EXPECT_TRUE(inside(result,520+dx,0,9));
    }
}
TEST(PartDrawing, JsonNumberSpellingDoesNotRebaseMainBlank) {
    const ObjectMap a{{"length",500.},{"section",ObjectMap{{"profile",ObjectMap{{"contours",VariantArray{
        ObjectMap{{"kind",std::string("circle")},{"radius",20.},{"center",VariantArray{-0.,0.}}}}}}}}}};
    const ObjectMap b{{"length",500ull},{"section",ObjectMap{{"profile",ObjectMap{{"contours",VariantArray{
        ObjectMap{{"kind",std::string("circle")},{"radius",20},{"center",VariantArray{0,0}}}}}}}}}};
    EXPECT_EQ(PartDrawingGeometrySignature(a),PartDrawingGeometrySignature(b));
    auto changed=b;changed["length"]=501ull;EXPECT_NE(PartDrawingGeometrySignature(a),PartDrawingGeometrySignature(changed));
}
TEST(PartDrawing, JsonTransportPreservesRotationCoefficientsExactly) {
    using iCAX::TemplateRuntime::CStandardJsonCodec;
    for(double value:{6.123233995736766e-17,0.7071067811865476,1.0000000000000002,1.2345678901234567e-80}) {
        const auto transported=CStandardJsonCodec::Parse(CStandardJsonCodec::Serialize(iCAX::Data::Variant(value)));
        EXPECT_EQ(value,transported.To<double>());
    }
}
TEST(PartDrawing, BranchExtrusionDirectionAndMaterialRegion) {
    auto f=partTool("branch-profile",{{"direction",std::string("positive")}});
    auto result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(result,500,0,9));EXPECT_TRUE(inside(result,500,0,-9));
    // An annular through-cut can leave a loose plug and must be rejected.
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{partTool("branch-profile",{{"cutRegion",std::string("material")}})}),std::invalid_argument);
    // Place the annular region across the end without leaving a detached island.
    f=partTool("branch-profile",{{"cutRegion",std::string("material")}},-5.5);
    result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(result,.25,0,9));EXPECT_TRUE(inside(result,1,0,9));
}
TEST(PartDrawing, SharpRoundedAndFlatRootVGroovesPreserveBottomBridge) {
    for(auto parameters:{ObjectMap(),ObjectMap{{"rootRadius",2.}},ObjectMap{{"rootWidth",2.}},
        ObjectMap{{"bridge",3.},{"reliefDiameter",2.},{"reliefLift",1.}}}) {
        const auto result=BuildPunchGeometry(rectTube(),{partTool("v-notch-sharp",parameters)});
        EXPECT_FALSE(inside(result,500,0,9));EXPECT_TRUE(inside(result,500,0,-9.5));
        EXPECT_TRUE(inside(result,550,0,9));
    }
}
TEST(PartDrawing, NativeArraysAndEmptyIntersectionRejection) {
    auto f=partTool("branch-profile",{},200,3);f.ArrayCount=3;f.ArrayPitch=100;
    auto result=BuildPunchGeometry(rectTube(),{f});
    for(double x:{200.,300.,400.})EXPECT_FALSE(inside(result,x,0,9));
    EXPECT_TRUE(inside(result,500,0,9));
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{partTool("branch-profile",{{"offsetY",100.}})}),std::invalid_argument);
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{partTool("branch-profile",{{"direction",std::string("symmetric")},{"length",2.}})}),std::invalid_argument);
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{partTool("branch-profile",{},500,50)}),std::invalid_argument);
}
TEST(PartDrawing, AxialArraySupportsBothSignsAndEndReference) {
    for(const std::string reference:{"start","end"}) for(double sign:{-1.,1.}) {
        SCOPED_TRACE(reference+" / "+std::to_string(sign));
        auto f=partTool("branch-profile",{},500,3);
        f.Reference=reference;f.ArrayCount=3;f.ArrayPitch=sign*100;
        const auto result=BuildPunchGeometry(rectTube(),{f});
        const double direction=(reference=="end"?-1:1)*sign;
        for(int col=0;col<3;++col)EXPECT_FALSE(inside(result,500+direction*col*100,0,9));
        EXPECT_TRUE(inside(result,500-direction*100,0,9));
    }
}
TEST(PartDrawing, CartesianMultiRowsCombineXWithYOrZInBothDirections) {
    for(bool z:{false,true}) for(double sign:{-1.,1.}) {
        SCOPED_TRACE(std::string(z?"Z":"Y")+" / "+std::to_string(sign));
        const double initial=sign>0?(z?-4:-10):(z?4:10);
        auto f=partTool("branch-profile",{{"azimuth",z?90.:0.},{z?"offsetZ":"offsetY",initial}},200,2);
        f.Face=z?"left":"top";f.ArrayCount=3;f.ArrayPitch=100;f.RowCount=2;f.RowPitch=sign*(z?8:20);
        std::vector<SPunchCut> cuts;
        const auto result=BuildPunchGeometry(rectTube(),{f},{},{},&cuts);
        ASSERT_EQ(cuts.size(),1u);
        for(double x:{200.,300.,400.})for(int row=0;row<2;++row) {
            const double across=initial+row*f.RowPitch;
            EXPECT_FALSE(inside(result,x,z?19:across,z?across:9));
            EXPECT_FALSE(inside(result,x,z?-19:across,z?across:-9));
        }
        EXPECT_TRUE(inside(result,300,z?19:0,z?0:9));
        EXPECT_TRUE(inside(result,500,z?19:initial,z?initial:9));
    }
}
TEST(PartDrawing, CircularMultiRowsRotateAboutXWithExplicitDirection) {
    for(double sign:{-1.,1.}) {
        SCOPED_TRACE(sign);
        auto f=partTool("branch-profile",{},200,2,{{"min",VariantArray{0.,-20.,-20.}},{"max",VariantArray{1000.,20.,20.}}});
        f.Face="round";f.ArrayCount=3;f.ArrayPitch=100;f.RowCount=2;f.RowPitch=sign*45;
        const auto result=BuildPunchGeometry(roundTube(),{f});
        const double yz=19/std::sqrt(2.);
        for(double x:{200.,300.,400.}) {
            EXPECT_FALSE(inside(result,x,0,19));
            EXPECT_FALSE(inside(result,x,-sign*yz,yz));
            EXPECT_TRUE(inside(result,x,sign*yz,yz));
        }
        EXPECT_TRUE(inside(result,500,0,19));
    }
}
TEST(PartDrawing, FrozenBranchKeepsPositionAfterRemovingEndCut) {
    auto f=partTool("branch-profile",{},100);SPunchEnds ends;ends.Start=templateEnd("end-square",true,{},50);
    std::vector<SPunchCut> cuts;BuildPunchGeometry(rectTube(),{f},ends,{},&cuts);ASSERT_EQ(cuts.size(),2u);
    const auto brep=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(cuts[1].Shape,"branch-fixed","branch-fixed",.025);
    const auto restored=iCAX::OpenCascade::BuildOpenCascadeShape(brep);ASSERT_TRUE(restored.bOK);
    f.FrozenCut=restored.Shape;f.ToolShape.Nullify();
    const auto result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(result,100,0,9));EXPECT_TRUE(inside(result,150,0,9));EXPECT_TRUE(inside(result,20,19,0));
}
TEST(PartDrawing, EveryLibrarySectionAndImportedDxfBuildExactBranchTools) {
    const auto library=templateRequest({{"action",std::string("list-system")}},false,
        "src/apps/tube-designer/templates/_shared/profile_package_runtime.py");
    auto profiles=library.at("systemProfiles").To<VariantArray>();
    const auto imported=templateRequest({{"sourcePath",(std::filesystem::current_path()/
        "src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/PartDrawingSection.dxf").string()}},false,
        "src/apps/tube-designer/templates/_shared/dxf_profile_importer.py");
    profiles.emplace_back(ObjectMap{{"previewProfile",imported.at("profile")}});
    ASSERT_EQ(profiles.size(),11u);
    for(const auto& value:profiles) {
        const auto profile=value.To<ObjectMap>().at("previewProfile").To<ObjectMap>();
        SCOPED_TRACE(profile.at("name").To<std::string>());
        const auto prepared=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
            {"features",VariantArray{ObjectMap{{"toolTarget",std::string("part")},{"station",500.},
                {"toolRef",ObjectMap{{"id",std::string("branch-profile")}}},{"toolParameters",ObjectMap{{"angle",60.},{"roll",15.}}},
                {"section",ObjectMap{{"profile",profile}}}}}}});
        const auto shape=templateShape(prepared.at("features").To<VariantArray>()[0].To<ObjectMap>().at("toolSnapshot").To<ObjectMap>());
        EXPECT_GT(mass(shape),0);
    }
    // The imported concentric ellipses are filled only for the branch bore.
    auto f=partTool("branch-profile");auto item=f.TemplateData;
    item["section"]=ObjectMap{{"profile",imported.at("profile")}};
    const auto prepared=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},{"features",VariantArray{item}}});
    f.ToolShape=templateShape(prepared.at("features").To<VariantArray>()[0].To<ObjectMap>().at("toolSnapshot").To<ObjectMap>());
    const auto result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(result,500,0,9));EXPECT_TRUE(inside(result,514,0,9));EXPECT_TRUE(inside(result,500,9,9));
}
TEST(PunchGeometry, SingleWallPreservesOppositeAndInput) {
    const auto base=rectTube(); const double before=mass(base);
    const auto result=BuildPunchGeometry(base,{hole()});
    EXPECT_FALSE(inside(result,500,0,9)); EXPECT_TRUE(inside(result,500,0,-9));
    EXPECT_NEAR(mass(base),before,1e-6); EXPECT_LT(mass(result),before);
}
TEST(PunchGeometry, ReverseModeCutsOnlyTheOppositeEntryWall) {
    auto f=hole();f.Reverse=true;
    const auto result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_TRUE(inside(result,500,0,9));
    EXPECT_FALSE(inside(result,500,0,-9));
}
TEST(PunchGeometry, CurvedWallIsPiercedAtHoleEdges) {
    auto f=hole(); f.Face="round";
    const auto result=BuildPunchGeometry(roundTube(),{f});
    EXPECT_FALSE(inside(result,500,17.6,4.9));
    EXPECT_TRUE(inside(result,500,-19,0));
}
TEST(PunchGeometry, RoundRotationAndTranslatedSection) {
    auto f=hole(); f.Face="round"; f.Offset=90;
    gp_Trsf move;move.SetTranslation(gp_Vec(50,23,-17));
    const auto result=BuildPunchGeometry(BRepBuilderAPI_Transform(roundTube(),move,true).Shape(),{f});
    EXPECT_FALSE(inside(result,550,23,2)); EXPECT_TRUE(inside(result,550,42,-17));
}

TEST(PunchTemplates, AllInstalledSideTemplatesBuildNativeSolids) {
    for(const auto* id:{"circle","rectangle","slot","ellipse","diamond-12"}) {
        SCOPED_TRACE(id);
        const auto f=templateHole(id);
        const auto result=BuildPunchGeometry(rectTube(),{f});
        EXPECT_FALSE(inside(result,500,0,9));EXPECT_TRUE(inside(result,500,0,-9));
    }
}
TEST(PunchTemplates, AnalyticRoundWallIsFullyPierced) {
    auto f=templateHole("circle",{{"diameter",10.}});f.Face="round";
    const auto result=BuildPunchGeometry(roundTube(),{f});
    EXPECT_FALSE(inside(result,500,17.6,4.9));EXPECT_TRUE(inside(result,500,-19,0));
}
TEST(PunchTemplates, ConvexAndConcaveKeepOppositeSilhouettesAtBothEnds) {
    for(bool start:{true,false}) {
        SPunchEnds concave,convex;
        (start?concave.Start:concave.End)=templateEnd("end-cope",start,{{"diameter",20.}});
        (start?convex.Start:convex.End)=templateEnd("end-convex",start,{{"diameter",20.}});
        const auto a=BuildPunchGeometry(rectTube(),{},concave),b=BuildPunchGeometry(rectTube(),{},convex);
        const double x=start?5:995;
        EXPECT_FALSE(inside(a,x,19,0));EXPECT_TRUE(inside(a,x,0,9));
        EXPECT_TRUE(inside(b,x,19,0));EXPECT_FALSE(inside(b,x,0,9));
    }
}
TEST(PunchTemplates, PlanarEndToolsAndRelativePlacement) {
    SPunchEnds ends;ends.Start=templateEnd("end-miter",true,{{"angle",45.}},10);
    auto f=templateHole("circle");f.Station=100;
    const auto shape=BuildPunchGeometry(rectTube(),{f},ends);
    EXPECT_FALSE(inside(shape,20,19,0));EXPECT_TRUE(inside(shape,80,19,0));
    EXPECT_FALSE(inside(shape,110,0,9));
    ends.Start=templateEnd("end-square",true,{},25);
    EXPECT_FALSE(inside(BuildPunchGeometry(rectTube(),{},ends),20,19,0));
}

TEST(PunchTemplates, FrozenCutterDoesNotMoveWhenPrecedingEndNodeIsDeleted) {
    SPunchEnds ends;ends.Start=templateEnd("end-square",true,{},50);
    auto f=templateHole("circle");f.ID="round-node";f.Station=100;f.ArrayCount=2;f.ArrayPitch=100;
    std::vector<SPunchCut> cuts;
    const auto original=BuildPunchGeometry(rectTube(),{f},ends,{},&cuts);
    ASSERT_EQ(cuts.size(),2u);EXPECT_EQ(cuts[0].Target,"end");EXPECT_EQ(cuts[1].Key,f.ID);
    EXPECT_FALSE(inside(original,150,0,9));EXPECT_FALSE(inside(original,250,0,9));
    const auto frozen=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(cuts[1].Shape,"fixed","fixed",0.025);
    const auto restored=iCAX::OpenCascade::BuildOpenCascadeShape(frozen);ASSERT_TRUE(restored.bOK);
    f.ToolShape.Nullify();f.ToolFootprint.clear();f.FrozenCut=restored.Shape;
    const auto replayed=BuildPunchGeometry(rectTube(),{f}); // End deleted; fixed tools must NOT move to X=100/200.
    EXPECT_FALSE(inside(replayed,150,0,9));EXPECT_FALSE(inside(replayed,250,0,9));
    EXPECT_TRUE(inside(replayed,100,0,9));EXPECT_TRUE(inside(replayed,200,0,9));
    EXPECT_TRUE(inside(replayed,20,19,0));
    EXPECT_TRUE(inside(BuildPunchGeometry(rectTube(),{}),150,0,9));
}

TEST(PunchTemplates, ProjectFileRestoresBaseAndToolRecipeForRemovalAndReplay) {
    using namespace iCAX::Database;
    using namespace iCAX::Resource;
    using iCAX::GeometryData::BRepModel;
    for(bool drawing:{false,true}) {
    SCOPED_TRACE(drawing?"3D branch drawing":"side punch");
    const auto scene=iCAX::Data::GenerateNewUUID(), entity=iCAX::Data::GenerateNewUUID();
    auto registry=CreateMetaRegistry();
    wchar_t executable[32768]{},databaseModule[32768]{};
    ASSERT_GT(GetModuleFileNameW(nullptr,executable,32768),0u);
    ASSERT_GT(GetModuleFileNameW(GetModuleHandleW(L"Database.dll"),databaseModule,32768),0u);
    CMetaRegistrationCatalog::ReplayByModulePaths(*registry,{std::filesystem::path(databaseModule).string(),std::filesystem::path(executable).string()});
    const auto source=GenerateRepository(scene,registry);
    source->BeginLoadBaseline();
    const auto partEntity=source->CreateEntity(entity);
    const auto part=partEntity->AddComponent<CManufacturingPartComponent>();
    const auto feature=drawing?partTool("branch-profile").TemplateData:ObjectMap{
        {"id",std::string("circle-node")},{"toolRef",ObjectMap{{"id",std::string("circle")}}},
        {"toolParameters",ObjectMap{{"diameter",12.}}},{"station",500.}};
    const auto prepared=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"features",VariantArray{feature}}});
    const std::string baseID="icax://punch-test/base",resultID="icax://punch-test/result",cutID="icax://punch-test/cutter";
    auto savedFeatures=prepared.at("recipe").To<ObjectMap>().at("features").To<VariantArray>();
    auto savedFeature=savedFeatures[0].To<ObjectMap>();
    savedFeature["frozenCut"]=ObjectMap{{"url",cutID},{"version",1ull},{"coordinateSpace",std::string("part")}};
    savedFeatures[0]=savedFeature;
    ObjectMap configToSave{
        {"schemaVersion",4ull},{"baseResourceId",baseID},{"baseResourceVersion",1ull},{"features",savedFeatures},
        {"operations",prepared.at("recipe").To<ObjectMap>().at("operations")},
        {"ends",prepared.at("recipe").To<ObjectMap>().at("ends")},{"baseLength",1000.}};
    if(drawing)configToSave["drawing"]=ObjectMap{{"length",1000.},{"section",feature.at("section")}};
    if(drawing) {
        const auto component=partEntity->AddComponent<CPartDrawingComponent>();
        ASSERT_TRUE(component);ASSERT_TRUE(component->SetDefinition(configToSave));
    } else {
        const auto component=partEntity->AddComponent<CPunchWizardComponent>();
        ASSERT_TRUE(component);ASSERT_TRUE(component->SetDefinition(configToSave));
    }
    ASSERT_TRUE(part->SetItemProperties({}));
    ASSERT_TRUE(part->SetManufacturingGeometryResourceID(resultID));
    ASSERT_TRUE(part->SetManufacturingGeometryResourceVersion(1ull));
    source->EndLoadBaseline();
    const auto registerCodec=[](CResourceLibrary& library) {
        CResourceVersionCodec codec;
        codec.Serialize=[](const std::shared_ptr<void>& value) -> std::optional<std::vector<std::uint8_t>> {
            return iCAX::GeometryData::Persistence::Serialize(*std::static_pointer_cast<BRepModel>(value));
        };
        codec.Deserialize=[](std::span<const std::uint8_t> bytes) -> std::shared_ptr<void> {
            return std::make_shared<BRepModel>(iCAX::GeometryData::Persistence::Deserialize(bytes));
        };
        return library.RegisterPersistenceCodec<BRepModel>(BRepModel::kResourceTypeName,codec);
    };
    CResourceLibrary resources;
    ASSERT_TRUE(registerCodec(resources));
    std::vector<SPunchCut> cuts;
    const auto base=rectTube(),result=BuildPunchGeometry(base,{drawing?partTool("branch-profile"):templateHole("circle",{{"diameter",12.}})},{},{},&cuts);
    ASSERT_EQ(cuts.size(),1u);
    for(const auto& [id,shape]:std::vector<std::pair<std::string,TopoDS_Shape>>{{baseID,base},{resultID,result},{cutID,cuts[0].Shape}}) {
        CResourceInfo info;info.ResourceTypeID=BRepModel::kResourceTypeName;info.nVersion=1;info.nSchemaVersion=1;
        info.Persistence=EResourcePersistenceMode::Embedded;
        resources.Set(id,std::make_shared<BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"punch",id,0.025)),info);
    }
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=iCAX::Data::GenerateNewUUID();info.MainSceneID=scene;info.ProjectName="punch round trip";
    const auto path=std::filesystem::temp_directory_path()/(std::filesystem::path(u8"冲孔回放-").native()+std::filesystem::path(iCAX::Data::to_string(entity)+".ictd").native());
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
    file.Save(path,info,*source,resources);
    CResourceLibrary reopened;
    ASSERT_TRUE(registerCodec(reopened));
    const auto destination=GenerateRepository(scene,registry);
    file.Open(path,*destination,reopened);
    const auto loaded=std::dynamic_pointer_cast<CManufacturingPartComponent>(destination->GetEntity(entity)->GetComponent(CManufacturingPartComponent::S_ClassName));
    ASSERT_TRUE(loaded);
    const auto loadedEntity=destination->GetEntity(entity);
    const auto config=drawing
        ? std::dynamic_pointer_cast<CPartDrawingComponent>(loadedEntity->GetComponent(CPartDrawingComponent::S_ClassName))->GetDefinition()
        : std::dynamic_pointer_cast<CPunchWizardComponent>(loadedEntity->GetComponent(CPunchWizardComponent::S_ClassName))->GetDefinition();
    EXPECT_FALSE(config.at("features").To<VariantArray>()[0].To<ObjectMap>().contains("toolSnapshot"));
    const auto loadedFeature=config.at("features").To<VariantArray>()[0].To<ObjectMap>();
    if(drawing) {
        EXPECT_EQ(config.at("drawing"),configToSave.at("drawing"));
        EXPECT_EQ(loadedFeature.at("section"),feature.at("section"));
        EXPECT_EQ(loadedFeature.at("toolTarget").To<std::string>(),"part");
    } else EXPECT_EQ(loadedFeature.at("toolParameters").To<ObjectMap>().at("diameter").To<double>(),12.);
    EXPECT_TRUE(loadedFeature.contains("frozenTool"));EXPECT_TRUE(loadedFeature.contains("frozenCut"));
    EXPECT_EQ(config.at("operations").To<VariantArray>()[0].To<ObjectMap>().at("operator").To<std::string>(),"subtract");
    const auto savedBase=reopened.Get<BRepModel>(config.at("baseResourceId").To<std::string>(),1);
    ASSERT_TRUE(savedBase);ASSERT_TRUE(reopened.Get<BRepModel>(resultID,1));
    // Viewing/exporting uses the saved result directly: no tool runtime invocation.
    const auto savedResult=iCAX::OpenCascade::BuildOpenCascadeShape(*reopened.Get<BRepModel>(resultID,1));
    ASSERT_TRUE(savedResult.bOK);
    EXPECT_FALSE(inside(savedResult.Shape,500,0,9));
    STEPControl_Writer exporter;
    EXPECT_EQ(IFSelect_RetDone,exporter.Transfer(savedResult.Shape,STEPControl_AsIs));
    const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*savedBase);
    ASSERT_TRUE(rebuilt.bOK);
    const auto missing=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"original",config},{"features",config.at("features")},{"ends",config.at("ends")}},true);
    EXPECT_EQ(missing.at("features").To<VariantArray>()[0].To<ObjectMap>().at("toolSnapshot").To<ObjectMap>().at("resolution").To<std::string>(),"frozen");
    const auto withoutNode=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"original",config},{"features",VariantArray{}},{"ends",config.at("ends")}},true);
    EXPECT_TRUE(withoutNode.at("features").To<VariantArray>().empty());
    const auto replayed=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"features",config.at("features")},{"ends",config.at("ends")}});
    auto cutter=hole();cutter.Type=drawing?"branch-profile":"circle";cutter.ToolIsProfile=!drawing;cutter.ToolInPartCoordinates=drawing;
    cutter.ToolShape=templateShape(replayed.at("features").To<VariantArray>()[0].To<ObjectMap>().at("toolSnapshot").To<ObjectMap>());
    if(!drawing)PreparePunchToolFootprint(cutter);
    EXPECT_FALSE(inside(BuildPunchGeometry(rebuilt.Shape,{cutter}),500,0,9));
    // Rebuild directly from the embedded, already placed fixed cutter: no script/library lookup.
    const auto savedCut=reopened.Get<BRepModel>(cutID,1);ASSERT_TRUE(savedCut);
    const auto fixed=iCAX::OpenCascade::BuildOpenCascadeShape(*savedCut);ASSERT_TRUE(fixed.bOK);
    cutter.ToolShape.Nullify();cutter.ToolFootprint.clear();cutter.FrozenCut=fixed.Shape;
    EXPECT_FALSE(inside(BuildPunchGeometry(rebuilt.Shape,{cutter}),500,0,9));
    EXPECT_TRUE(inside(BuildPunchGeometry(rebuilt.Shape,{}),500,0,9));
    }
}
TEST(PunchGeometry, RotatedEndClipsButUnintendedOtherWallPenetrationStillFails) {
    auto f=hole(); f.Type="rectangle"; f.SpanAlong=60; f.SpanAcross=10; f.Rotation=90;
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{f}),std::invalid_argument);
    f.SpanAlong=10; f.SpanAcross=30; f.Station=10;
    const auto result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(result,1,0,9));EXPECT_TRUE(inside(result,30,0,9));
}
TEST(PunchGeometry, OppositeAndThroughModes) {
    auto f=hole(); f.Opposite=true;
    auto result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(result,500,0,9)); EXPECT_FALSE(inside(result,500,0,-9));
    f.Opposite=false; f.Through=true;
    result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(result,500,0,9)); EXPECT_FALSE(inside(result,500,0,-9));
}
TEST(PunchGeometry, ArrayAndEndReference) {
    auto f=hole(); f.Reference="end"; f.Station=100; f.ArrayCount=3; f.ArrayPitch=100;
    const auto result=BuildPunchGeometry(rectTube(),{f});
    for(double x:{900.,800.,700.}) EXPECT_FALSE(inside(result,x,0,9));
    EXPECT_TRUE(inside(result,500,0,9));
}
TEST(PunchLayouts, EndReferenceUsesSignedExplicitOffsetsExactlyOnce) {
    auto f=hole();f.Reference="end";f.Station=100;f.LayoutDatum="base";
    f.ArrayCount=3;f.ArrayPitch=0;f.ArrayOffsets={0,-120,-310};
    std::vector<SPunchCut> cuts;
    const auto result=BuildPunchGeometry(rectTube(),{f},{},{},&cuts);
    for(double x:{900.,780.,590.}) EXPECT_FALSE(inside(result,x,0,9));
    EXPECT_TRUE(inside(result,800,0,9));ASSERT_EQ(cuts.size(),1u);
    for(double x:{900.,780.,590.}) EXPECT_TRUE(inside(cuts[0].Shape,x,0,9));
}
TEST(PunchLayouts, CenterOutwardOffsetsPreserveBothSidesAndCandidateOrder) {
    auto f=hole();f.Reference="center";f.Station=0;f.LayoutDatum="base";
    f.ArrayCount=5;f.ArrayOffsets={0,-100,100,-250,250};f.SkippedInstances={"0:1"};
    const auto result=BuildPunchGeometry(rectTube(),{f});
    for(double x:{500.,600.,250.,750.}) EXPECT_FALSE(inside(result,x,0,9));
    EXPECT_TRUE(inside(result,400,0,9));EXPECT_TRUE(inside(result,350,0,9));
}
TEST(PunchLayouts, BlankDatumDoesNotFollowTrimmedEnds) {
    SPunchEnds ends;ends.Start.Type="square";ends.Start.Trim=50;ends.End.Type="square";ends.End.Trim=80;
    for(const auto* reference:{"start","end","center"}) {
        auto f=hole();f.Reference=reference;f.LayoutDatum="base";
        f.Station=f.Reference=="center"?0:100;
        const double x=f.Reference=="start"?100:f.Reference=="end"?900:500;
        const auto result=BuildPunchGeometry(rectTube(),{f},ends);
        EXPECT_FALSE(inside(result,x,0,9));
        EXPECT_TRUE(inside(result,x+10,0,9));
    }
}
TEST(PunchLayouts, RowsAndOppositeHolesUseStableSkippedCandidateIndices) {
    auto f=hole();f.Diameter=4;f.Opposite=true;
    f.ArrayCount=3;f.ArrayOffsets={-100,0,150};f.RowCount=3;f.RowOffsets={-10,0,10};
    f.SkippedInstances={"1:1","2:0"};
    std::vector<SPunchCut> cuts;
    const auto result=BuildPunchGeometry(rectTube(),{f},{},{},&cuts);
    ASSERT_EQ(cuts.size(),1u);
    for(double z:{-9.,9.}) for(int row=0;row<3;++row) for(int col=0;col<3;++col) {
        const bool skipped=(row==1&&col==1)||(row==2&&col==0);
        const double x=500+f.ArrayOffsets[col],y=f.RowOffsets[row];
        EXPECT_EQ(inside(result,x,y,z),skipped);
        EXPECT_EQ(inside(cuts[0].Shape,x,y,z),!skipped);
    }
}
TEST(PunchLayouts, FullRoundOffsetsDoNotRepeatClosingPosition) {
    auto f=hole();f.Diameter=4;f.Face="round";f.RowCount=4;f.RowOffsets={30,120,210,300};
    const auto result=BuildPunchGeometry(roundTube(),{f});
    for(double angle:f.RowOffsets) {
        const double radians=angle*3.14159265358979323846/180;
        EXPECT_FALSE(inside(result,500,19*std::cos(radians),19*std::sin(radians)));
    }
    EXPECT_TRUE(inside(result,510,19,0));
}
TEST(PunchLayouts, PartCoordinateRoundOffsetsIncludeNonZeroStartingPhase) {
    auto f=partTool("branch-profile",{{"direction",std::string("positive")}},500,2,
        {{"min",VariantArray{0.,-20.,-20.}},{"max",VariantArray{1000.,20.,20.}}});
    f.Face="round";f.RowCount=4;f.RowOffsets={30,120,210,300};
    const auto result=BuildPunchGeometry(roundTube(),{f});
    for(double angle:f.RowOffsets) {
        const double radians=angle*3.14159265358979323846/180;
        EXPECT_FALSE(inside(result,500,-19*std::sin(radians),19*std::cos(radians)));
    }
    EXPECT_TRUE(inside(result,500,0,19));
}
TEST(PunchLayouts, SkippedOutOfBoundsCandidatesAreNotEvaluated) {
    auto f=hole();f.ArrayCount=3;f.ArrayOffsets={0,-2000,150};f.SkippedInstances={"0:1"};
    const auto result=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(result,500,0,9));EXPECT_FALSE(inside(result,650,0,9));
}
TEST(PunchLayouts, OpenNotchMayHaveItsCenterBeyondTheBlank) {
    for(const auto* reference:{"start","end"}) {
        auto f=hole();f.Reference=reference;f.Station=-2;f.LayoutDatum="base";f.AllowOpen=true;
        f.ArrayOffsets={0};
        const auto result=BuildPunchGeometry(rectTube(),{f});
        const bool start=f.Reference=="start";
        EXPECT_FALSE(inside(result,start?1:999,0,9));
        EXPECT_TRUE(inside(result,start?8:992,0,9));
        f.AllowOpen=false;
        EXPECT_NEAR(mass(result),mass(BuildPunchGeometry(rectTube(),{f})),1e-6);
    }
}
TEST(PunchLayouts, AllSkippedGroupHasNoCutAndCanReplayWithoutTool) {
    auto f=hole();f.Type="unavailable-tool";f.ArrayCount=2;f.ArrayOffsets={0,100};
    f.SkippedInstances={"0:0","0:1"};std::vector<SPunchCut> cuts;
    const auto base=rectTube();const auto result=BuildPunchGeometry(base,{f},{},{},&cuts);
    EXPECT_NEAR(mass(base),mass(result),1e-6);EXPECT_TRUE(cuts.empty());
}
TEST(PunchLayouts, OrdinaryArraysCutAcrossEndsAndIgnoreWhollyOutsideInstances) {
    auto f=hole();f.Station=992;f.ArrayCount=3;f.ArrayPitch=10;f.AllowOpen=false;
    std::vector<SPunchCut> cuts;SPunchGeometryStatistics stats;
    const auto result=BuildPunchGeometry(rectTube(),{f},{},{},&cuts,&stats);
    EXPECT_FALSE(inside(result,992,0,9));EXPECT_FALSE(inside(result,999,0,9));
    EXPECT_TRUE(inside(result,999,0,-9));EXPECT_TRUE(inside(result,980,0,9));
    EXPECT_EQ(stats.CandidateCount,3u);EXPECT_EQ(stats.AppliedCount,2u);EXPECT_EQ(stats.OutsideCount,1u);
    ASSERT_EQ(cuts.size(),1u);EXPECT_EQ(cuts[0].InstanceCount,2u);EXPECT_TRUE(stats.CountsExact);
    auto frozen=f;frozen.FrozenCut=cuts[0].Shape;frozen.FrozenCutInstanceCount=2;
    EXPECT_NEAR(mass(BuildPunchGeometry(rectTube(),{frozen},{},{},nullptr,&stats)),mass(result),1e-5);
    EXPECT_EQ(stats.AppliedCount,2u);EXPECT_EQ(stats.OutsideCount,1u);EXPECT_TRUE(stats.CountsExact);
    auto second=hole();second.Station=1012;second.ArrayCount=2;second.ArrayOffsets={0,-212};
    const auto combined=BuildPunchGeometry(rectTube(),{f,second},{},{},nullptr,&stats);
    EXPECT_FALSE(inside(combined,800,0,9));EXPECT_EQ(stats.AppliedCount,3u);EXPECT_EQ(stats.OutsideCount,2u);
    f.Station=1100;EXPECT_THROW(BuildPunchGeometry(rectTube(),{f}),std::invalid_argument);
}
TEST(PunchLayouts, PartCoordinateArraysAlsoIgnoreOutsideInstances) {
    auto f=partTool("branch-profile",{},992,5);f.ArrayCount=3;f.ArrayPitch=10;
    SPunchGeometryStatistics stats;
    const auto result=BuildPunchGeometry(rectTube(),{f},{},{},nullptr,&stats);
    EXPECT_FALSE(inside(result,999,0,9));EXPECT_FALSE(inside(result,999,0,-9));
    EXPECT_EQ(stats.AppliedCount,2u);EXPECT_EQ(stats.OutsideCount,1u);
}
TEST(PunchEnds, RectangularMaleAndFemaleAreComplementaryMatingSolids) {
    const auto base=rectTube();SPunchEnds male,female;
    male.Start=templateEnd("end-key-joint",true,{{"gender",std::string("male")},{"width",10.},{"depth",20.}});
    female.End=templateEnd("end-key-joint",false,{{"gender",std::string("female")},{"width",10.},{"depth",20.}});
    const auto a=BuildPunchGeometry(base,{},male);
    const auto b=BuildPunchGeometry(base,{},female);
    gp_Trsf placement;placement.SetTranslation(gp_Vec(-980,0,0));
    const auto assembled=BRepBuilderAPI_Transform(b,placement,true).Shape();
    EXPECT_NEAR(mass(BRepAlgoAPI_Common(a,assembled).Shape()),0,1e-5);
    EXPECT_NEAR(mass(a)+mass(b),mass(base)*1.98,1e-4);
    EXPECT_TRUE(inside(a,10,0,9));EXPECT_FALSE(inside(assembled,10,0,9));
    EXPECT_FALSE(inside(a,10,15,9));EXPECT_TRUE(inside(assembled,10,15,9));
    female.End=templateEnd("end-key-joint",false,{{"gender",std::string("female")},{"width",10.},{"depth",20.},
        {"sideClearance",.5},{"axialClearance",1.}});
    const auto clear=BRepBuilderAPI_Transform(BuildPunchGeometry(base,{},female),placement,true).Shape();
    EXPECT_NEAR(mass(BRepAlgoAPI_Common(a,clear).Shape()),0,1e-5);
    EXPECT_FALSE(inside(a,10,5.25,9));EXPECT_FALSE(inside(clear,10,5.25,9));
    EXPECT_FALSE(inside(clear,-.5,0,9));
}
TEST(PunchEnds, SingleStepZHasExplicitHandAndIndependentEnds) {
    for(const auto* hand:{"positive","negative"}) {
        SPunchEnds ends;ends.Start=templateEnd("end-step-z",true,{{"depth",20.},{"hand",std::string(hand)}},10);
        ends.End=templateEnd("end-step-z",false,{{"depth",30.},{"hand",std::string(hand)}},5);
        const auto result=BuildPunchGeometry(rectTube(),{},ends);
        const double y=std::string(hand)=="positive"?10:-10;
        EXPECT_FALSE(inside(result,15,y,9));EXPECT_TRUE(inside(result,15,-y,9));
        EXPECT_FALSE(inside(result,985,y,9));EXPECT_TRUE(inside(result,985,-y,9));
        EXPECT_TRUE(inside(result,40,y,9));EXPECT_TRUE(inside(result,950,y,9));
    }
}
TEST(PunchEnds, BothMatingFormsSupportEitherEndAndRotation) {
    for(bool start:{true,false}) for(const auto* gender:{"male","female"}) {
        SPunchEnds ends;(start?ends.Start:ends.End)=templateEnd("end-key-joint",start,
            {{"gender",std::string(gender)},{"width",8.},{"depth",20.}},5,{},90);
        const auto result=BuildPunchGeometry(rectTube(),{},ends);
        const double x=start?15:985;
        EXPECT_EQ(inside(result,x,19,0),std::string(gender)=="male");
        EXPECT_EQ(inside(result,x,19,7),std::string(gender)=="female");
    }
}
TEST(PunchEnds, LibraryAndLocalDxfSectionsCutEitherEndWithoutSideHoles) {
    const auto imported=templateRequest({{"sourcePath",(std::filesystem::current_path()/
        "src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/PartDrawingSection.dxf").string()}},false,
        "src/apps/tube-designer/templates/_shared/dxf_profile_importer.py");
    const ObjectMap circle{{"contours",VariantArray{ObjectMap{{"kind",std::string("circle")},{"radius",10.}}}}};
    for(const auto& profile:{circle,imported.at("profile").To<ObjectMap>()}) for(bool start:{true,false}) {
        SPunchEnds ends;(start?ends.Start:ends.End)=templateEnd("end-profile",start,{},10,
            {{"source",std::string("dxf")},{"profile",profile}});
        SPunchGeometryStatistics stats;
        const auto result=BuildPunchGeometry(rectTube(),{},ends,{},nullptr,&stats);
        EXPECT_FALSE(inside(result,start?11:989,0,9));
        EXPECT_TRUE(inside(result,start?30:970,0,9));
        EXPECT_EQ(stats.AppliedCount,0u);EXPECT_EQ(stats.EndToolCount,1u);
    }
}
TEST(PunchEnds, SectionEndCutterRejectsMissingDetachedAndNonContactGeometry) {
    EXPECT_ANY_THROW(templateEnd("end-profile",true));
    const ObjectMap section{{"profile",ObjectMap{{"contours",VariantArray{
        ObjectMap{{"kind",std::string("circle")},{"radius",10.}}}}}}};
    for(const auto& parameters:std::vector<ObjectMap>{{{"axialOffset",100.}},{{"offsetY",100.}},{{"axialOffset",-100.}}}) {
        SPunchEnds ends;ends.Start=templateEnd("end-profile",true,parameters,0,section);
        EXPECT_THROW(BuildPunchGeometry(rectTube(),{},ends),std::invalid_argument);
    }
}
TEST(PunchEnds, ToolPreviewClipsHugeEndSlabsWithoutChangingManufacturingCutters) {
    SPunchEnds ends;ends.Start=templateEnd("end-step-z",true,{},10);
    ends.End=templateEnd("end-key-joint",false,{},5);
    std::vector<SPunchCut> cuts;const auto base=rectTube();
    const auto result=BuildPunchGeometry(base,{},ends,{},&cuts);
    ASSERT_EQ(cuts.size(),2u);const auto original0=mass(cuts[0].Shape),original1=mass(cuts[1].Shape);
    const auto preview=BuildPunchToolPreview(base,cuts);
    Bnd_Box box;BRepBndLib::AddOptimal(preview,box,false,false);box.SetGap(0);
    double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
    const double margin=std::hypot(40.,20.)/2;
    EXPECT_GE(x0,-margin-1e-4);EXPECT_LE(x1,1000+margin+1e-4);
    EXPECT_GE(y0,-20-margin-1e-4);EXPECT_LE(y1,20+margin+1e-4);
    EXPECT_GE(z0,-10-margin-1e-4);EXPECT_LE(z1,10+margin+1e-4);
    EXPECT_NEAR(mass(cuts[0].Shape),original0,1e-4);EXPECT_NEAR(mass(cuts[1].Shape),original1,1e-4);
    EXPECT_LT(mass(preview),original0+original1);
    EXPECT_NEAR(mass(BuildPunchGeometry(base,{},ends)),mass(result),1e-4);
    EXPECT_TRUE(inside(preview,15,10,9));EXPECT_FALSE(inside(result,15,10,9));
}
TEST(PunchLayouts, PartCoordinateBranchUsesOffsetsAndFreezesOnlyActiveInstances) {
    auto f=partTool("branch-profile",{},900,2);f.Reference="end";f.LayoutDatum="base";
    f.ArrayCount=3;f.ArrayOffsets={0,-120,-310};f.RowCount=2;f.RowOffsets={-10,10};
    f.SkippedInstances={"1:1"};
    std::vector<SPunchCut> cuts;
    const auto original=BuildPunchGeometry(rectTube(),{f},{},{},&cuts);
    ASSERT_EQ(cuts.size(),1u);
    const auto stored=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(cuts[0].Shape,"layout-fixed","layout-fixed",.025);
    const auto restored=iCAX::OpenCascade::BuildOpenCascadeShape(stored);ASSERT_TRUE(restored.bOK);
    f.FrozenCut=restored.Shape;f.ToolShape.Nullify();
    const auto replayed=BuildPunchGeometry(rectTube(),{f});
    for(const auto& result:{original,replayed}) for(int row=0;row<2;++row) for(int col=0;col<3;++col)
        for(double z:{-9.,9.}) EXPECT_EQ(inside(result,900+f.ArrayOffsets[col],f.RowOffsets[row],z),row==1&&col==1);
}
TEST(PunchLayouts, RejectsMalformedOffsetsCountsAndSkippedIndices) {
    auto f=hole();f.ArrayCount=3;f.ArrayOffsets={0,100};
    EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.ArrayOffsets={0,100,100};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.ArrayOffsets={0,100,std::numeric_limits<double>::infinity()};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.ArrayOffsets={0,100,std::numeric_limits<double>::quiet_NaN()};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.ArrayOffsets={0,100,200};f.RowCount=2;f.RowOffsets={0,0};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.Face="round";f.RowOffsets={0,360};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.RowOffsets={-180,180};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.RowOffsets={0,std::numeric_limits<double>::infinity()};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.RowOffsets={0,90};
    for(const auto* value:{"0:3","2:0","-1:0","0:-1","0:0:1","0",":0","0:","00:0","18446744073709551616:0"}) {
        f.SkippedInstances={value};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument)<<value;
    }
    f.SkippedInstances={"0:1","0:1"};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.SkippedInstances.clear();f.ArrayCount=1001;EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.ArrayCount=1000;f.ArrayOffsets.clear();f.ArrayPitch=10;EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.RowCount=1;f.RowOffsets.clear();f.LayoutDatum="unknown";EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
}
TEST(PunchLayouts, SuppressionDoesNotBypassCandidateLimit) {
    auto f=hole();f.ArrayCount=1000;f.ArrayPitch=10;
    for(int col=0;col<1000;++col) f.SkippedInstances.push_back("0:"+std::to_string(col));
    auto extra=hole();extra.SkippedInstances={"0:0"};
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{f,extra}),std::invalid_argument);
}
TEST(PunchLayouts, DisabledRulesAllowUnfinishedSpacingButRetainStructuralLimits) {
    auto f=hole();f.Enabled=false;f.ArrayCount=2;f.ArrayPitch=0;
    f.TemplateData={{"distributionMode",std::string("sequence")},{"spacingSequence",std::string("")}};
    EXPECT_NO_THROW(ValidatePunchFeatureLayout(f));
    f.ArrayOffsets={0,0};EXPECT_NO_THROW(ValidatePunchFeatureLayout(f));
    f.Enabled=true;EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.Enabled=false;f.ArrayOffsets={0};EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
    f.ArrayOffsets.clear();f.ArrayCount=1001;EXPECT_THROW(ValidatePunchFeatureLayout(f),std::invalid_argument);
}
TEST(PunchLayouts, RuntimeRoundTripRetainsRulesAndLocksFrozenPlacement) {
    const ObjectMap rules{
        {"layoutDatum",std::string("base")},{"distributionMode",std::string("center-out")},
        {"headMargin",20.},{"tailMargin",30.},{"centerOffset",0.},{"centerMode",std::string("hole")},
        {"centerFirstOffset",iCAX::Data::Variant{}},{"fillAlign",std::string("center")},{"maxSpacing",120.},
        {"spacingSequence",std::string("100, 150")},{"positionList",std::string("400, 500, 600")},
        {"skipInstancesText",std::string("2:2")},{"rowDistributionMode",std::string("pitch")},
        {"rowStartAngle",0.},{"rowEndAngle",180.},{"arrayCount",3ll},{"arrayOffsets",VariantArray{-100.,0.,100.}},
        {"rowCount",2ll},{"rowOffsets",VariantArray{-10.,10.}},{"skippedInstances",VariantArray{std::string("1:1")}}
    };
    auto feature=rules;feature["id"]=std::string("layout-node");feature["station"]=500.;
    feature["toolRef"]=ObjectMap{{"id",std::string("circle")}};
    feature["toolParameters"]=ObjectMap{{"diameter",4.}};
    const auto prepared=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"features",VariantArray{feature}}});
    const auto recipe=prepared.at("recipe").To<ObjectMap>();
    const auto saved=recipe.at("features").To<VariantArray>()[0].To<ObjectMap>();
    for(const auto& [key,value]:rules) {ASSERT_TRUE(saved.contains(key))<<key;EXPECT_EQ(saved.at(key),value)<<key;}
    const auto replayed=templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"features",recipe.at("features")},{"original",recipe}},true);
    const auto loaded=replayed.at("recipe").To<ObjectMap>().at("features").To<VariantArray>()[0].To<ObjectMap>();
    for(const auto& [key,value]:rules) EXPECT_EQ(loaded.at(key),value)<<key;
    auto changed=saved;changed["skippedInstances"]=VariantArray{std::string("1:0")};
    EXPECT_ANY_THROW(templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"features",VariantArray{changed}},{"original",recipe}},true));
    changed=saved;changed["arrayOffsets"]=VariantArray{-120.,0.,100.};
    EXPECT_ANY_THROW(templateRequest({{"action",std::string("prepare")},{"bounds",testBounds()},
        {"features",VariantArray{changed}},{"original",recipe}},true));
}
TEST(PunchGeometry, RejectsZeroPitchDuplicatesAndEmptyHits) {
    auto f=hole(); f.ArrayCount=2;
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{f}),std::invalid_argument);
    f.ArrayCount=1;
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{f,f}),std::invalid_argument);
    f.Offset=60; f.AllowOpen=true;
    EXPECT_THROW(BuildPunchGeometry(rectTube(),{f}),std::invalid_argument);
}
TEST(PunchGeometry, EndMiterAndEndRelativeHoleReplay) {
    SPunchEnds ends; ends.Start.Type="miter"; ends.Start.Angle=45; ends.Start.Trim=10;
    auto f=hole(); f.Station=100;
    auto result=BuildPunchGeometry(rectTube(),{f},ends);
    EXPECT_FALSE(inside(result,20,19,0)); EXPECT_TRUE(inside(result,80,19,0));
    EXPECT_FALSE(inside(result,110,0,9));
    ends.Start.Trim=20;
    result=BuildPunchGeometry(rectTube(),{f},ends);
    EXPECT_FALSE(inside(result,120,0,9)); EXPECT_TRUE(inside(result,110,0,9));
}
TEST(PunchGeometry, CopeAndOpenEndNotch) {
    SPunchEnds ends; ends.Start.Type="cope"; ends.Start.Diameter=60; ends.Start.Angle=90;
    const auto result=BuildPunchGeometry(rectTube(),{},ends);
    EXPECT_FALSE(inside(result,10,0,9)); EXPECT_TRUE(inside(result,50,0,9));
    auto f=hole(); f.Station=0; f.AllowOpen=true;
    const auto notch=BuildPunchGeometry(rectTube(),{f});
    EXPECT_FALSE(inside(notch,1,0,9)); EXPECT_TRUE(inside(notch,20,0,9));
}
TEST(PunchGeometry, AnalyticEllipseSlotAndRoundedRectangle) {
    for(const auto& type:{"ellipse","slot","rectangle"}) {
        auto f=hole(); f.Type=type; f.SpanAlong=30; f.SpanAcross=10; f.CornerRadius=2; f.Rotation=20;
        const auto result=BuildPunchGeometry(rectTube(),{f});
        EXPECT_FALSE(inside(result,500,0,9)); EXPECT_TRUE(inside(result,500,0,-9));
    }
}
TEST(PunchGeometry, FeatureSuppressionAndRemovalRestoreBase) {
    const auto base=rectTube(); auto f=hole(); f.Enabled=false;
    EXPECT_NEAR(mass(BuildPunchGeometry(base,{f})),mass(base),1e-5);
    EXPECT_NEAR(mass(BuildPunchGeometry(base,{})),mass(base),1e-5);
}
TEST(PunchGeometry, NeutralBaseRoundTripCanReplayAndRemoveHoles) {
    const auto base=roundTube(); auto f=hole(); f.Face="round";
    const auto stored=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(base,"base","test-base",0.1);
    const auto restored=iCAX::OpenCascade::BuildOpenCascadeShape(stored);
    ASSERT_TRUE(restored.bOK);
    auto result=BuildPunchGeometry(restored.Shape,{f});
    EXPECT_FALSE(inside(result,500,19,0));
    EXPECT_NEAR(mass(BuildPunchGeometry(restored.Shape,{})),mass(base),1e-3);
}
TEST(PunchGeometry, NormalizeRoundUsesSurfaceBoundsNotSeamVertices) {
    const auto result=NormalizeLinearPartForManufacturing(roundTube());
    Bnd_Box box; BRepBndLib::AddOptimal(result,box,false,false); box.SetGap(0);
    double a,b,c,d,e,f; box.Get(a,b,c,d,e,f);
    EXPECT_NEAR(b+e,0,1e-6); EXPECT_NEAR(c+f,0,1e-6); EXPECT_NEAR(a+d,0,1e-6);
}
