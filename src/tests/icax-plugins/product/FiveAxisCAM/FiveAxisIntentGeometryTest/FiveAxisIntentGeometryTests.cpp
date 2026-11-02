#include <gtest/gtest.h>
#include <FiveAxisCAM/FiveAxisIntentGeometry.h>
#include <OpenCascadeResourceImport/CadReferenceService.h>
#include <GeometryServices/CurveOffset3DService.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Database/IMetaRegistry.h>
#include <Database/IRepository.h>
#include <Database/MetaRegistrationCatalog.h>
#include <Data/VariantSerializer.h>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

namespace
{
    using namespace iCAX::CAM::Intent;
    using namespace iCAX::GeometryData;
    using namespace iCAX::FiveAxisCAM;
    using Map=iCAX::Data::ObjectMap;
    using Array=iCAX::Data::VariantArray;

    std::vector<std::string> LoadBackends()
    {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        const auto directory=std::filesystem::path(executable).parent_path();std::vector<std::string> paths;
        for(const auto* name:{L"OpenCascadeResourceImport.dll",L"OpenCascadeGeometryServices.dll"}) {
            const auto path=directory/name;
            // Catalog callbacks remain valid until process termination.
            if(!LoadLibraryW(path.c_str()))throw std::runtime_error("Cannot load exact geometry backend: "+path.string());
            paths.push_back(path.string());
        }
        return paths;
    }
    std::shared_ptr<iCAX::Database::IRepository> Repository()
    {
        auto meta=iCAX::Database::CreateMetaRegistry();iCAX::Database::CMetaRegistrationCatalog::ReplayAll(*meta);
        return iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID(),meta);
    }
    ParameterRange Range(const SEditableSegment& segment)
    {
        return std::visit([](const auto& exact) {
            if constexpr(std::is_same_v<std::decay_t<decltype(exact)>,DirectionalCurveOffset3>)return exact.Source.Range;
            else return exact.Range;
        },segment.Curve);
    }
    void Near(Point3 actual,Point3 expected,double tolerance=1e-10)
    { EXPECT_NEAR(actual.X,expected.X,tolerance);EXPECT_NEAR(actual.Y,expected.Y,tolerance);EXPECT_NEAR(actual.Z,expected.Z,tolerance); }

    class FiveAxisIntentGeometry : public ::testing::Test
    {
    protected:
        iCAX::Services::CServiceProvider Provider;
        std::shared_ptr<const iCAX::Geometry::ICadReferenceService> Cad;
        std::shared_ptr<const iCAX::GeometryServices::ICurveOffset3DService> Offset;
        std::shared_ptr<const IToolpathGeometryServices> Geometry;
        std::shared_ptr<iCAX::Database::IRepository> Db;
        std::shared_ptr<CToolpathOperationRegistry> Registry;
        std::unique_ptr<CEditableToolpathDocument> Document;
        IntentId Plan;
        void SetUp() override
        {
            static const auto paths=LoadBackends();iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(Provider,paths);
            Cad=Provider.Resolve<iCAX::Geometry::ICadReferenceService>();Offset=Provider.Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
            Geometry=CreateFiveAxisIntentGeometryServices(Cad,Offset);
            Db=Repository();Registry=std::make_shared<CToolpathOperationRegistry>();RegisterStandardToolpathOperations(*Registry);
            Document=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry,Geometry);Plan=Document->CreatePlan("native geometry");
        }
        Map Command(const std::string& kind) const
        { return {{"kind",kind},{"commandId",iCAX::Data::GenerateNewUUID()},{"planId",Plan}}; }
        void Source(const SToolpathValue& value)
        { auto command=Command("CreateSource");command["value"]=ToData(value);Document->Execute(command);Generate(); }
        void Generate()
        {
            auto task=Document->GenerateAsync(Plan,2);const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(!task->IsReady()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ASSERT_TRUE(task->IsReady());ASSERT_TRUE(Document->Publish(task));
            const auto result=Document->Resolve(Plan);for(const auto& diagnostic:result.Diagnostics)ADD_FAILURE()<<diagnostic.Message;
            ASSERT_EQ(EOutputCompleteness::Complete,result.Status);
        }
        std::vector<SResolvedPath> Paths() const { return Document->Resolve(Plan).Paths; }
        void Add(const std::string& type,const SResolvedPath& path,const Map& parameters={})
        {
            auto command=Command("AddOperation");command["typeId"]=type;command["parameters"]=parameters;
            command["inputs"]=Array{Map{{"slot",std::string("input")},{"source",ToData(path.Port)},{"replace",true},{"ranges",Array{}}}};
            command["expectedInputs"]=Map{{ToolpathPortKey(path.Port),path.Fingerprint}};Document->Execute(command);Generate();
        }
        void Split(const SResolvedPath& path,double parameter)
        {
            const SPathAnchor at{path.Port,path.Value.Geometry.Segments.front().Id,parameter};
            const Array cuts{Map{{"id",std::string("cut-A")},{"anchor",ToData(at)}}};
            Add("icax.edit.split",path,{{"cuts",cuts}});
        }
        void CheckReopened()
        {
            const auto before=Paths();auto db=Repository();
            const auto serialized=iCAX::Data::VariantSerializer::Serialize(Document->ExportDocument());
            const auto data=iCAX::Data::VariantSerializer::Deserialize(serialized).To<Map>();
            const auto documentId=CEditableToolpathDocument::ImportDocument(db,data,Geometry);
            CEditableToolpathDocument reopened(db,documentId,Registry,Geometry);const auto after=reopened.Resolve(Plan);
            ASSERT_EQ(EOutputCompleteness::Complete,after.Status);ASSERT_EQ(before.size(),after.Paths.size());
            for(const auto& path:before) {
                const auto found=std::find_if(after.Paths.begin(),after.Paths.end(),[&](const auto& candidate){return candidate.PathId==path.PathId;});
                ASSERT_NE(after.Paths.end(),found);EXPECT_EQ(path.Fingerprint,found->Fingerprint);EXPECT_EQ(ToData(path.Value),ToData(found->Value));
            }
        }
        void CheckCadPoints(const CompositeCurve3& original,const std::vector<SResolvedPath>& paths)
        {
            for(const auto& path:paths)for(const auto& segment:path.Value.Geometry.Segments) {
                EXPECT_EQ("CadNative",segment.Parameterization);const auto range=Range(segment);
                for(double parameter:{range.First,(range.First+range.Last)*.5,range.Last}) {
                    const auto expected=Cad->EvaluateCurve(original,0,parameter);
                    Near(Geometry->Evaluate(path.Value.Geometry,segment,parameter),{expected[0],expected[1],expected[2]});
                }
            }
        }
    };
}

TEST_F(FiveAxisIntentGeometry, NativeArcAnglesSurviveSplitReverseAndDatabaseReopen)
{
    Arc3 arc;arc.Basis.Radius=7;arc.StartAngle=.3;arc.EndAngle=1.7;
    CompositeCurve3 original{{{arc,{.3,1.7,false}}},false};
    Source(MakeFiveAxisIntentValue(original,{"cad/edge-A"}));ASSERT_EQ(1u,Paths().size());
    Split(Paths().front(),.9);ASSERT_EQ(2u,Paths().size());CheckCadPoints(original,Paths());
    const auto selected=Paths().front();Add("icax.edit.reverse",selected);ASSERT_EQ(2u,Paths().size());CheckCadPoints(original,Paths());
    const auto reversed=Paths();
    EXPECT_TRUE(std::any_of(reversed.begin(),reversed.end(),[](const auto& path){return Range(path.Value.Geometry.Segments.front()).Reversed;}));
    CheckReopened();
}

TEST_F(FiveAxisIntentGeometry, NativePolylineUsesPointIndicesBeyondOne)
{
    Polyline3 polyline;polyline.Points={{0,0,0},{3,0,0},{3,4,0}};
    CompositeCurve3 original{{{polyline,{0,2,false}}},false};
    Source(MakeFiveAxisIntentValue(original,{"cad/polyline-A"}));Near(Geometry->Evaluate(Paths()[0].Value.Geometry,Paths()[0].Value.Geometry.Segments[0],1.5),{3,2,0});
    Split(Paths().front(),1.25);ASSERT_EQ(2u,Paths().size());CheckCadPoints(original,Paths());CheckReopened();
}

TEST_F(FiveAxisIntentGeometry, VariableDirectionOffsetRemainsAnExactEditableLaw)
{
    DirectionalCurveOffset3 law;law.Source.Curve=Bezier3{{{0,0,0},{1./3,0,0},{2./3,1./3,0},{1,1,1}}};
    law.Source.Range={0,1,false};law.SignedDistance=.17;
    law.ReferenceDirection=ParametricVectorField3{Bezier3{{{0,1,1},{.5,1,1},{1,1,2}}}};
    Source(MakeFiveAxisIntentValue(CompositeCurve3{{law.Source},false},{"cad/spatial-cubic"}));
    SOperationType operation;operation.TypeId="fiveaxis.test.exact-offset";
    operation.Evaluate=[law](const SOperationEvaluation& input,const IToolpathGeometryServices&) {
        const auto& path=input.Inputs.at(0);SResolvedPath source;source.PathId=path.PathId;source.Port=path.Binding.Source;source.Value=path.Value;
        CompositeOffsetCurve3 exact;exact.Segments.push_back({law,std::size_t(0)});
        SOutputCandidate output;output.RoleKey="main";output.InheritInput=0;
        output.Value=MakeFiveAxisOffsetIntentValue(exact,source,{"offset/spatial-cubic"});return SOperationCandidate{{std::move(output)}};
    };
    Registry->Register(std::move(operation));Add("fiveaxis.test.exact-offset",Paths().front());ASSERT_EQ(1u,Paths().size());
    EXPECT_EQ("ToolCenter",Paths()[0].Value.Geometry.Meaning);Split(Paths().front(),.43);ASSERT_EQ(2u,Paths().size());
    Add("icax.edit.reverse",Paths().front());
    for(const auto& path:Paths())for(const auto& segment:path.Value.Geometry.Segments) {
        ASSERT_TRUE(std::holds_alternative<DirectionalCurveOffset3>(segment.Curve));const auto& stored=std::get<DirectionalCurveOffset3>(segment.Curve);
        ASSERT_TRUE(std::holds_alternative<ParametricVectorField3>(stored.ReferenceDirection));const auto range=Range(segment);
        for(double parameter:{range.First,(range.First+range.Last)*.5,range.Last}) {
            const auto expected=Offset->Evaluate(law,{parameter});ASSERT_EQ(ECurveOffsetStatus::SamplesEvaluated,expected.Status);
            Near(Geometry->Evaluate(path.Value.Geometry,segment,parameter),expected.Samples.front().OffsetPoint);
            const auto x=3*parameter*parameter-2*parameter-2*parameter*parameter*parameter;
            const auto y=1+parameter*parameter-3*parameter*parameter*parameter,z=2*parameter*parameter-1;
            const auto length=std::hypot(x,y,z);
            Near(expected.Samples.front().OffsetPoint,{parameter+.17*x/length,parameter*parameter+.17*y/length,parameter*parameter*parameter+.17*z/length});
        }
    }
    CheckReopened();
}

TEST_F(FiveAxisIntentGeometry, OffsetConnectorHasConstructionOriginAndNoFabricatedSource)
{
    Source(MakeFiveAxisIntentValue(CompositeCurve3{{{Segment3{{0,0,0},{2,0,0}},{0,1,false}}},false},{"cad/edge-A"}));
    CompositeOffsetCurve3 curve;curve.Segments.push_back({CurveSegment3{Segment3{{2,0,0},{2,1,0}},{0,1,false}},std::nullopt});
    const auto value=MakeFiveAxisOffsetIntentValue(curve,Paths().front(),{"corner/source-A/source-B"});
    ASSERT_EQ(1u,value.Geometry.Segments.size());EXPECT_TRUE(value.Geometry.Segments[0].Sources.empty());
    EXPECT_EQ("OffsetConnector",value.Geometry.Segments[0].ConstructionRole);Geometry->Validate(value.Geometry);
}

TEST_F(FiveAxisIntentGeometry, RejectsUnsupportedDomainMalformedIdentityAndDisconnectedChain)
{
    CompositeCurve3 original{{{Segment3{{0,0,0},{1,0,0}},{0,1,false}}},false};
    EXPECT_THROW(MakeFiveAxisIntentValue(original,{}),std::invalid_argument);
    auto value=MakeFiveAxisIntentValue(original,{"cad/edge-A"});
    EXPECT_THROW(CreateToolpathGeometryServices()->Validate(value.Geometry),std::invalid_argument);
    std::get<CurveSegment3>(value.Geometry.Segments[0].Curve).Range.Last=2;
    EXPECT_THROW(Geometry->Validate(value.Geometry),std::invalid_argument);
    original.Segments.push_back({Segment3{{4,0,0},{5,0,0}},{0,1,false}});
    value=MakeFiveAxisIntentValue(original,{"cad/edge-A","cad/edge-B"});EXPECT_THROW(Geometry->Validate(value.Geometry),std::invalid_argument);
}
