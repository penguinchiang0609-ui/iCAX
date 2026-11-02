#include "pch.h"
#include <OpenCascadeResourceImport/CadBRepImportService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <GeometryData/BRepPersistence.h>
#include <CutRecognition/CutRecognitionService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_Ellipse.hxx>
#include <OpenCascadeResourceImport/CadReferenceService.h>
#include <OpenCascadeResourceImport/CurveEditingService.h>
#include <OpenCascadeResourceImport/GeometryFeatureAnalysisService.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Data/uuid.h>
#include <Task/TaskStatus.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <STEPControl_Writer.hxx>
#include <IGESControl_Writer.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESData_IGESModel.hxx>
#include <IGESData_IGESEntity.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_LayerTool.hxx>
#include <gp_Pln.hxx>
#include <Quantity_Color.hxx>
#include <StepVisual_Invisibility.hxx>
#include <StepVisual_InvisibleItem.hxx>
#include <StepVisual_StyledItem.hxx>
#include <StepVisual_PresentationRepresentation.hxx>
#include <StepShape_AdvancedFace.hxx>
#include <StepData_StepModel.hxx>
#include <DESTEP_Parameters.hxx>
#include <STEPControl_Reader.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <iostream>
#include <map>
#include <set>
#include <iomanip>
#include <future>
#include <latch>
#include <atomic>

namespace
{
    TEST(FiveAxisCadSourceAudit, DiskOriginalStepNativeWireJunctions)
    {
        const auto path=std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path()/"../../../../../../TestData/FA/2.stp.STEP");
        std::ifstream stream(path,std::ios::binary);ASSERT_TRUE(stream.good());
        STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadStream("2.stp.STEP",stream));
        ASSERT_GT(reader.TransferRoots(),0);const auto shape=reader.OneShape();
        ASSERT_TRUE(BRepCheck_Analyzer(shape).IsValid());
        double maximumGap=0;std::size_t faceIndex=0;
        for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()){
            ++faceIndex;const auto face=TopoDS::Face(f.Current());
            for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next()){
                std::vector<TopoDS_Edge> edges;
                for(BRepTools_WireExplorer e(TopoDS::Wire(w.Current()),face);e.More();e.Next())edges.push_back(e.Current());
                double perimeter=0,gapMax=0,declaredTolerance=0;
                for(std::size_t i=0;i<edges.size();++i){
                    const auto& edge=edges[i];const auto& next=edges[(i+1)%edges.size()];if(BRep_Tool::Degenerated(edge)||BRep_Tool::Degenerated(next))continue;
                    BRepAdaptor_Curve a(edge),b(next);perimeter+=GCPnts_AbscissaPoint::Length(a);
                    const auto end=a.Value(edge.Orientation()==TopAbs_REVERSED?a.FirstParameter():a.LastParameter());
                    const auto start=b.Value(next.Orientation()==TopAbs_REVERSED?b.LastParameter():b.FirstParameter());
                    const auto gap=end.Distance(start);maximumGap=std::max(maximumGap,gap);
                    if(gap>gapMax){gapMax=gap;TopoDS_Vertex first,last,nfirst,nlast;TopExp::Vertices(edge,first,last,true);TopExp::Vertices(next,nfirst,nlast,true);
                        declaredTolerance=BRep_Tool::Tolerance(last)+BRep_Tool::Tolerance(nfirst);}
                }
                if(perimeter>200)std::cout<<std::setprecision(12)<<"Original STEP face="<<faceIndex<<" perimeter="<<perimeter<<" maxJunctionGap="<<gapMax<<" sharedVertexToleranceSum="<<declaredTolerance<<std::endl;
            }
        }
        std::cout<<std::setprecision(12)<<"Original STEP maximum junction gap="<<maximumGap<<std::endl;
        EXPECT_GT(maximumGap,1e-5);
    }

    class CSourceFixture final
    {
    public:
        explicit CSourceFixture(const std::wstring& extension)
            : Path(std::filesystem::temp_directory_path()
                / (std::wstring(L"icax五轴导入-") + std::filesystem::path(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())).wstring() + extension)) {}
        ~CSourceFixture() { std::error_code error; std::filesystem::remove(Path, error); }
        std::filesystem::path Path;
    };

    std::shared_ptr<iCAX::CAD::ICadBRepImportService> Resolve(iCAX::Services::CServiceProvider& provider)
    {
        // Production loads this service module through the product manifest.
        static const auto module = ::LoadLibraryW(L"OpenCascadeResourceImport.dll");
        if (!module) throw std::runtime_error("Cannot load CAD service module");
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
        return provider.Resolve<iCAX::CAD::ICadBRepImportService>();
    }

    void WriteStep(const std::filesystem::path& path, const TopoDS_Shape& shape,
                   UnitsMethods_LengthUnit unit = UnitsMethods_LengthUnit_Millimeter)
    {
        STEPControl_Writer writer;
        DESTEP_Parameters parameters;
        parameters.WriteUnit = unit;
        ASSERT_EQ(IFSelect_RetDone, writer.Transfer(shape, STEPControl_AsIs, parameters));
        std::ofstream stream(path, std::ios::binary);
        ASSERT_EQ(IFSelect_RetDone, writer.WriteStream(stream));
    }

    void ExpectBox(const iCAX::GeometryData::BRepModel& model, double x, double y, double z)
    {
        ASSERT_EQ(1u, model.Solids.size());
        ASSERT_EQ(6u, model.Faces.size());
        ASSERT_EQ(8u, model.Vertices.size());
        std::array<double, 3> low{1e30, 1e30, 1e30}, high{-1e30, -1e30, -1e30};
        for (const auto& vertex : model.Vertices)
        {
            const std::array<double, 3> point{vertex.Position.X, vertex.Position.Y, vertex.Position.Z};
            for (std::size_t i = 0; i < 3; ++i)
            {
                low[i] = std::min(low[i], point[i]);
                high[i] = std::max(high[i], point[i]);
            }
        }
        EXPECT_NEAR(x, high[0] - low[0], 1e-7);
        EXPECT_NEAR(y, high[1] - low[1], 1e-7);
        EXPECT_NEAR(z, high[2] - low[2], 1e-7);
    }
}

TEST(FiveAxisCadImport, RoundedCoverEdgesRemainSampleable)
{
    iCAX::Services::CServiceProvider provider;const auto importer=Resolve(provider);
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath=std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path()
        / u8"../../../../../../TestData/FA/刀路/刀路/整理测试刀路/圆弧多义线盖-无孔-9696.stp.STEP");
    const auto result=importer->Load(request);ASSERT_TRUE(result.IsValidBRep);
    const auto editing=provider.Resolve<iCAX::Geometry::ICurveEditingService>();
    for(const auto& edge:result.Geometry.Edges){
        if(edge.Degenerated)continue;SCOPED_TRACE("edge="+std::to_string(edge.Id));
        const auto c=std::find_if(result.Geometry.Curves3.begin(),result.Geometry.Curves3.end(),[&](const auto& c){return c.Id==edge.Curve3Id;});
        ASSERT_NE(c,result.Geometry.Curves3.end());
        iCAX::GeometryData::CompositeCurve3 path;path.Segments.push_back({c->Geometry,edge.Range});
        const auto verify=[&](const auto& part){
            const auto samples=editing->Sample(part,.02,2.);const auto& r=part.Segments.front().Range;
            ASSERT_GE(samples.Points.size(),2u);
            EXPECT_DOUBLE_EQ(r.Reversed?r.Last:r.First,samples.Points.front().NativeParameter);
            EXPECT_DOUBLE_EQ(r.Reversed?r.First:r.Last,samples.Points.back().NativeParameter);
            for(const auto& p:samples.Points){EXPECT_EQ(0u,p.SegmentIndex);EXPECT_GE(p.NativeParameter,r.First);EXPECT_LE(p.NativeParameter,r.Last);
                EXPECT_NEAR(1.,std::hypot(p.Tangent[0],p.Tangent[1],p.Tangent[2]),1e-10);}
            for(std::size_t i=1;i<samples.Points.size();++i){const auto& a=samples.Points[i-1].Point;const auto& b=samples.Points[i].Point;
                EXPECT_LE(std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]),2.+1e-7);}
        };
        ASSERT_NO_THROW(verify(path));
        const auto samples=editing->Sample(path,.02,2.);
        // Deflection and arc-length stations can be only nanometres apart.
        // Every positive trim must retain its two exact source anchors.
        for(std::size_t k=1;k<samples.Points.size();++k){auto part=path;auto& r=part.Segments.front().Range;
            r.First=std::min(samples.Points[k-1].NativeParameter,samples.Points[k].NativeParameter);r.Last=std::max(samples.Points[k-1].NativeParameter,samples.Points[k].NativeParameter);
            EXPECT_NO_THROW(verify(part));part.Segments.front().Range.Reversed=!r.Reversed;
            EXPECT_NO_THROW(verify(part));}
        for(int k=0;k<16;++k){auto part=path;auto& r=part.Segments.front().Range;
            r.First=edge.Range.First+(edge.Range.Last-edge.Range.First)*k/16.;r.Last=edge.Range.First+(edge.Range.Last-edge.Range.First)*(k+1)/16.;
            EXPECT_NO_THROW(verify(part));}
    }
}
TEST(FiveAxisCadImport, EggTwoLayerStepImportsCompleteGeometry)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath = std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path()
        / u8"../../../../../../TestData/FA/刀路/刀路/整理测试刀路/蛋型零件-各种形状孔-two-layer-1-5.stp");
    const auto result = service->Load(request);
    EXPECT_TRUE(result.IsValidBRep);
    EXPECT_GT(result.TransferredRoots, 0u);
    EXPECT_FALSE(result.Geometry.Faces.empty());
    EXPECT_FALSE(result.Geometry.Edges.empty());
    EXPECT_FALSE(result.Bounds.Empty);
    const auto stored = iCAX::GeometryData::Persistence::Deserialize(
        iCAX::GeometryData::Persistence::Serialize(result.Geometry));
    const auto rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(stored);
    ASSERT_TRUE(rebuilt.bOK);
    EXPECT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
    EXPECT_EQ(rebuilt.Faces.size(), result.Geometry.Faces.size());
    EXPECT_EQ(rebuilt.Edges.size(), result.Geometry.Edges.size());
    const auto uses=iCAX::CAM::Recognition::EnumerateFaceUses(stored);
    ASSERT_EQ(3996u,uses.size());
    std::set<std::uint64_t> faceIds;
    for(const auto& use:uses){EXPECT_FALSE(use.UsePath.empty());EXPECT_TRUE(faceIds.insert(use.FaceID).second);}
    std::map<iCAX::GeometryData::EBRepShapeKind,size_t> childKinds;
    for(const auto& compound:stored.Compounds)for(const auto& child:compound.Children)++childKinds[child.Kind];
    for(const auto& [kind,count]:childKinds)std::cout<<"Egg compound child kind="<<int(kind)<<" count="<<count<<std::endl;
    iCAX::CAM::Recognition::SRecognitionInput input;
    input.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(stored);
    input.GeometryRef={"resource://egg-test-geometry",1};input.TopologyMapRef=input.GeometryRef;input.PlanningProfileRef={"resource://egg-test-profile",1};
    input.SelectedFaces={2373};input.GeometryOnly=true;input.Profile.Geometry.MaximumEdgeUses=200000;
    for(const auto& solid:stored.Solids)for(auto shellId:solid.ShellIds)for(const auto& shell:stored.Shells)
        if(shell.Id==shellId&&std::find(shell.FaceIds.begin(),shell.FaceIds.end(),2373)!=shell.FaceIds.end())std::cout<<"Egg selected face solid="<<solid.Id<<std::endl;
    const auto recognized=provider.Resolve<iCAX::CAM::Recognition::ICamCutRecognitionService>()->Recognize(input);
    const auto ready=std::count_if(recognized.Candidates.begin(),recognized.Candidates.end(),[](const auto& c){return c.ConfirmationEligibility=="Ready";});
    std::cout<<"Egg selected face candidates="<<recognized.Candidates.size()<<" ready="<<ready<<std::endl;
    EXPECT_EQ(213u,recognized.Candidates.size());EXPECT_GT(ready,1);
    std::cout << "Egg STEP faces=" << result.Geometry.Faces.size()
        << " edges=" << result.Geometry.Edges.size() << std::endl;
}

TEST(FiveAxisCadImport, SweptSupportsKeepNativeParametersThroughPersistence)
{
    iCAX::Services::CServiceProvider provider; ASSERT_NE(nullptr,Resolve(provider));
    NCollection_Array1<gp_Pnt> poles(1,3);
    poles(1)={10,0,-10};poles(2)={14,0,0};poles(3)={10,0,10};
    Handle(Geom_Surface) revolution=new Geom_SurfaceOfRevolution(
        new Geom_BezierCurve(poles),gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)));
    Handle(Geom_Surface) extrusion=new Geom_SurfaceOfLinearExtrusion(
        new Geom_Ellipse(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),8,4),gp_Dir(0,0,1));
    for(const auto& surface:{revolution,extrusion}) {
        const auto face=BRepBuilderAPI_MakeFace(surface,.3,2.3,.1,.9,1e-7).Face();
        auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(face,"swept support","test",.1);
        ASSERT_EQ(model.Surfaces3.size(),1u);
        if(surface==revolution) EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::RevolutionSurface3>(model.Surfaces3.front().Geometry));
        else EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::ExtrusionSurface3>(model.Surfaces3.front().Geometry));
        const auto stored=iCAX::GeometryData::Persistence::Deserialize(iCAX::GeometryData::Persistence::Serialize(model));
        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(stored);
        ASSERT_TRUE(rebuilt.bOK);ASSERT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
        BRepAdaptor_Surface original(face),restored(rebuilt.Faces.begin()->second);
        for(double u:{.3,1.1,2.3})for(double v:{.1,.5,.9}) {
            gp_Pnt a,b;gp_Vec au,av,bu,bv;original.D1(u,v,a,au,av);restored.D1(u,v,b,bu,bv);
            EXPECT_LT(a.Distance(b),1e-10);EXPECT_LT((au-bu).Magnitude(),1e-10);EXPECT_LT((av-bv).Magnitude(),1e-10);
        }
    }
}

TEST(FiveAxisCadImport, StepMillimetresAndInchesHaveTheSamePhysicalSize)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    ASSERT_NE(nullptr, service);
    for (const auto unit : {UnitsMethods_LengthUnit_Millimeter, UnitsMethods_LengthUnit_Inch})
    {
        CSourceFixture source(L".step");
        WriteStep(source.Path, BRepPrimAPI_MakeBox(25.4, 50.8, 12.7).Shape(), unit);
        iCAX::CAD::SCadBRepImportRequest request;
        request.SourcePath = source.Path;
        const auto result = service->Load(request);
        EXPECT_EQ("mm", result.GeometryLengthUnit);
        EXPECT_EQ("cad.step", result.FormatId);
        EXPECT_TRUE(result.IsValidBRep);
        EXPECT_EQ(1u, result.TransferredRoots);
        EXPECT_FALSE(result.SourceLengthUnits.empty());
        EXPECT_EQ(64u, result.SourceSha256.size());
        EXPECT_FALSE(result.SourceBytes.empty());
        ExpectBox(result.Geometry, 25.4, 50.8, 12.7);
        const auto repeat = service->Load(request);
        EXPECT_EQ(result.SourceSha256, repeat.SourceSha256);
        EXPECT_EQ(result.SourceBytes, repeat.SourceBytes);
    }
}

TEST(FiveAxisCadImport, RealIgesBoundaryNativeParametersRemainEvaluable)
{
    wchar_t path[32768]{};ASSERT_GT(GetEnvironmentVariableW(L"ICAX_EASYBLANK_IGES",path,32768),0u);
    iCAX::Services::CServiceProvider provider;const auto importer=Resolve(provider);
    iCAX::CAD::SCadBRepImportRequest request;request.SourcePath=path;
    const auto model=importer->Load(request).Geometry;
    const auto analysis=provider.Resolve<iCAX::Geometry::IGeometryFeatureAnalysisService>();
    const auto references=provider.Resolve<iCAX::Geometry::ICadReferenceService>();
    std::size_t checked=0,invalid=0;
    for(const auto& face:model.Faces)for(const auto& boundary:analysis->AnalyzeFace(model,face.Id,{}).Boundaries)
        for(const auto& span:boundary.Spans){
            const auto record=std::find_if(model.Curves3.begin(),model.Curves3.end(),[&](const auto& c){return c.Id==span.CurveID;});
            ASSERT_NE(model.Curves3.end(),record);
            iCAX::GeometryData::CompositeCurve3 curve;curve.Segments.push_back({record->Geometry,{span.First,span.Last,span.Reversed}});
            try {for(double u:{span.First,(span.First+span.Last)/2,span.Last})references->EvaluateCurve(curve,0,u);}
            catch(const std::exception& e){
                if(invalid++<10){
                    std::ostringstream detail;detail<<std::setprecision(17)<<"face="<<face.Id<<" edge="<<span.EdgeID<<" range="<<span.First<<","<<span.Last;
                    for(const auto& edge:model.Edges)if(edge.Id==span.EdgeID)detail<<" sourceRange="<<edge.Range.First<<","<<edge.Range.Last;
                    std::visit([&](const auto& c){detail<<" type="<<typeid(c).name();if constexpr(requires{c.Knots;})detail<<" knots="<<c.Knots.front()<<","<<c.Knots.back();},record->Geometry);
                    ADD_FAILURE()<<detail.str()<<" "<<e.what();
                }
            }
            ++checked;
        }
    EXPECT_GT(checked,1000u);EXPECT_EQ(0u,invalid)<<"checked="<<checked;
}

TEST(FiveAxisCadImport, IgesMillimetresAndInchesAreNormalizedWithoutGlobalSettings)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    for (const auto unit : {"MM", "INCH"})
    {
        CSourceFixture source(L".igs");
        IGESControl_Writer writer(unit, 1);
        ASSERT_TRUE(writer.AddShape(BRepPrimAPI_MakeBox(25.4, 50.8, 12.7).Shape()));
        {
            std::ofstream stream(source.Path, std::ios::binary);
            ASSERT_TRUE(writer.Write(stream));
        }
        iCAX::CAD::SCadBRepImportRequest request;
        request.SourcePath = source.Path;
        const auto result = service->Load(request);
        EXPECT_EQ("cad.iges", result.FormatId);
        ExpectBox(result.Geometry, 25.4, 50.8, 12.7);
    }
}

TEST(FiveAxisCadImport, ArbitraryIgesUnitIsRejectedInsteadOfAssumedMillimetres)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    CSourceFixture source(L".igs");
    IGESControl_Writer writer("MM", 1);
    ASSERT_TRUE(writer.AddShape(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    writer.Model()->ChangeGlobalSection().SetUnitFlag(3);
    {
        std::ofstream stream(source.Path, std::ios::binary);
        ASSERT_TRUE(writer.Write(stream));
    }
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath = source.Path;
    EXPECT_THROW(service->Load(request), std::invalid_argument);
}

TEST(FiveAxisCadImport, MissingIgesUnitIsRejectedInsteadOfDefaultingToInches)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    CSourceFixture source(L".iges");
    IGESControl_Writer writer("MM", 1);
    ASSERT_TRUE(writer.AddShape(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    writer.Model()->ChangeGlobalSection().SetUnitFlag(0);
    writer.Model()->ChangeGlobalSection().SetUnitName({});
    {
        std::ofstream stream(source.Path, std::ios::binary);
        ASSERT_TRUE(writer.Write(stream));
    }
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath = source.Path;
    EXPECT_THROW(service->Load(request), std::invalid_argument);
}

TEST(FiveAxisCadImport, CurvedWorkpieceDoesNotRequireTubeOrExtrusionRecognition)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    CSourceFixture source(L".stp");
    WriteStep(source.Path, BRepPrimAPI_MakeSphere(12.0).Shape());
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath = source.Path;
    const auto result = service->Load(request);
    EXPECT_TRUE(result.IsValidBRep);
    EXPECT_EQ(1u, result.Geometry.Solids.size());
    ASSERT_FALSE(result.Geometry.Surfaces3.empty());
    const auto* sphere = std::get_if<iCAX::GeometryData::SphericalSurface3>(&result.Geometry.Surfaces3.front().Geometry);
    ASSERT_NE(nullptr, sphere);
    EXPECT_NEAR(12.0, sphere->Radius, 1e-8);
}

TEST(FiveAxisCadImport, MultipleRootsAreKept)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    CSourceFixture source(L".step");
    STEPControl_Writer writer;
    ASSERT_EQ(IFSelect_RetDone, writer.Transfer(BRepPrimAPI_MakeBox(10, 20, 30).Shape(), STEPControl_AsIs));
    ASSERT_EQ(IFSelect_RetDone, writer.Transfer(BRepPrimAPI_MakeSphere(gp_Pnt(100, 0, 0), 5).Shape(), STEPControl_AsIs));
    {
        std::ofstream stream(source.Path, std::ios::binary);
        ASSERT_EQ(IFSelect_RetDone, writer.WriteStream(stream));
    }
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath = source.Path;
    const auto result = service->Load(request);
    EXPECT_EQ(2u, result.TransferredRoots);
    EXPECT_EQ(2u, result.Geometry.Solids.size());
    EXPECT_FALSE(result.Geometry.RootShapes.empty());
}

namespace
{
    void WriteVisibleStep(const std::filesystem::path& path, const occ::handle<TDocStd_Document>& doc,
                          bool hideStyledFaces = false, bool hidePresentation = false)
    {
        STEPCAFControl_Writer writer;
        ASSERT_TRUE(writer.Transfer(doc));
        if (hideStyledFaces)
        {
            // OCCT's writer only serializes root-label visibility. Build the
            // standard face INVISIBILITY record explicitly in this fixture.
            const auto model = writer.ChangeWriter().Model();
            int count = 0;
            const int entities = model->NbEntities();
            for (int i=1;i<=entities;++i)
            {
                const auto style=occ::down_cast<StepVisual_StyledItem>(model->Value(i));
                if (style.IsNull() || !style->ItemAP242().Value()->IsKind(STANDARD_TYPE(StepShape_AdvancedFace))) continue;
                occ::handle<NCollection_HArray1<StepVisual_InvisibleItem>> items = new NCollection_HArray1<StepVisual_InvisibleItem>(1,1);
                StepVisual_InvisibleItem item; item.SetValue(style); items->SetValue(1,item);
                occ::handle<StepVisual_Invisibility> invisible = new StepVisual_Invisibility();
                invisible->Init(items); model->AddWithRefs(invisible); ++count;
            }
            ASSERT_EQ(1,count);
        }
        if (hidePresentation)
        {
            const auto model = writer.ChangeWriter().Model();
            const int entities = model->NbEntities();
            int count = 0;
            for (int i=1;i<=entities;++i)
            {
                const auto presentation=occ::down_cast<StepVisual_PresentationRepresentation>(model->Value(i));
                if (presentation.IsNull()) continue;
                occ::handle<NCollection_HArray1<StepVisual_InvisibleItem>> items = new NCollection_HArray1<StepVisual_InvisibleItem>(1,1);
                StepVisual_InvisibleItem item; item.SetValue(presentation); items->SetValue(1,item);
                occ::handle<StepVisual_Invisibility> invisible = new StepVisual_Invisibility();
                invisible->Init(items); model->AddWithRefs(invisible); ++count;
            }
            ASSERT_EQ(1,count);
        }
        std::ofstream stream(path, std::ios::binary);
        ASSERT_EQ(IFSelect_RetDone, writer.WriteStream(stream));
        stream.close();
        wchar_t directory[32768];
        if (GetEnvironmentVariableW(L"ICAX_VISIBILITY_FIXTURE_DIRECTORY",directory,32768))
            std::filesystem::copy_file(path,std::filesystem::path(directory)
                /(std::string(::testing::UnitTest::GetInstance()->current_test_info()->name())+".step"),
                std::filesystem::copy_options::overwrite_existing);
    }

    iCAX::CAD::SCadBRepImportResult LoadVisibleFixture(const std::filesystem::path& path)
    {
        iCAX::Services::CServiceProvider provider;
        iCAX::CAD::SCadBRepImportRequest request; request.SourcePath = path;
        return Resolve(provider)->Load(request);
    }
}

TEST(FiveAxisCadVisibility, StepHiddenObjectAndHiddenLayerAreExcludedBeforeBounds)
{
    CSourceFixture source(L".step");
    occ::handle<TDocStd_Document> doc = new TDocStd_Document("BinXCAF");
    const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    const auto layers = XCAFDoc_DocumentTool::LayerTool(doc->Main());
    shapes->AddShape(BRepPrimAPI_MakeBox(10,20,30).Shape());
    const auto hidden = shapes->AddShape(BRepPrimAPI_MakeBox(gp_Pnt(100,0,0),10,20,30).Shape());
    colors->SetVisibility(hidden, false);
    const auto layered = shapes->AddShape(BRepPrimAPI_MakeBox(gp_Pnt(200,0,0),10,20,30).Shape());
    const auto layer = layers->AddLayer("hidden-layer");
    layers->SetLayer(layered,layer); layers->SetVisibility(layer,false);
    WriteVisibleStep(source.Path,doc);
    const auto result = LoadVisibleFixture(source.Path);
    ExpectBox(result.Geometry,10,20,30);
    EXPECT_NEAR(0,result.Bounds.Min.X,1e-7); EXPECT_NEAR(10,result.Bounds.Max.X,1e-7);
    EXPECT_FALSE(result.Diagnostics.empty());
}

TEST(FiveAxisCadVisibility, StepHiddenOccurrenceDoesNotHideVisibleUseOfSamePart)
{
    CSourceFixture source(L".step");
    occ::handle<TDocStd_Document> doc = new TDocStd_Document("BinXCAF");
    const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    const auto part = shapes->AddShape(BRepPrimAPI_MakeBox(10,20,30).Shape());
    colors->SetColor(part,Quantity_Color(Quantity_NOC_GRAY),XCAFDoc_ColorGen);
    const auto assembly = shapes->NewShape();
    gp_Trsf first; first.SetTranslation(gp_Vec(25,40,60));
    gp_Trsf second; second.SetTranslation(gp_Vec(200,0,0));
    shapes->AddComponent(assembly,part,TopLoc_Location(first));
    const auto hidden = shapes->AddComponent(assembly,part,TopLoc_Location(second));
    colors->SetVisibility(hidden,false);
    shapes->UpdateAssemblies();
    WriteVisibleStep(source.Path,doc);
    const auto result = LoadVisibleFixture(source.Path);
    ExpectBox(result.Geometry,10,20,30);
    EXPECT_NEAR(25,result.Bounds.Min.X,1e-7);
    EXPECT_NEAR(40,result.Bounds.Min.Y,1e-7);
    EXPECT_NEAR(60,result.Bounds.Min.Z,1e-7);
}

TEST(FiveAxisCadVisibility, StepHiddenParentExcludesWholeNestedAssembly)
{
    CSourceFixture source(L".step");
    occ::handle<TDocStd_Document> doc = new TDocStd_Document("BinXCAF");
    const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    const auto part = shapes->AddShape(BRepPrimAPI_MakeBox(10,20,30).Shape());
    colors->SetColor(part,Quantity_Color(Quantity_NOC_GRAY),XCAFDoc_ColorGen);
    const auto nested = shapes->NewShape();
    colors->SetColor(nested,Quantity_Color(Quantity_NOC_GRAY),XCAFDoc_ColorGen);
    gp_Trsf offset; offset.SetTranslation(gp_Vec(100,0,0));
    shapes->AddComponent(nested,part,TopLoc_Location(offset));
    const auto root = shapes->NewShape();
    shapes->AddComponent(root,part,{});
    const auto hiddenParent = shapes->AddComponent(root,nested,{});
    colors->SetColor(hiddenParent,Quantity_Color(Quantity_NOC_GRAY),XCAFDoc_ColorGen);
    colors->SetVisibility(hiddenParent,false);
    shapes->UpdateAssemblies();
    WriteVisibleStep(source.Path,doc);
    const auto result = LoadVisibleFixture(source.Path);
    ExpectBox(result.Geometry,10,20,30);
    EXPECT_NEAR(10,result.Bounds.Max.X,1e-7);
}

TEST(FiveAxisCadVisibility, StepHiddenFaceKeepsRemainingSurfaceModelValid)
{
    CSourceFixture source(L".step");
    occ::handle<TDocStd_Document> doc = new TDocStd_Document("BinXCAF");
    const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    const auto box = BRepPrimAPI_MakeBox(10,20,30).Shape();
    const auto root = shapes->AddShape(box);
    TopExp_Explorer face(box,TopAbs_FACE); ASSERT_TRUE(face.More());
    colors->SetColor(shapes->AddSubShape(root,face.Current()),Quantity_Color(Quantity_NOC_RED),XCAFDoc_ColorSurf);
    WriteVisibleStep(source.Path,doc,true);
    const auto result = LoadVisibleFixture(source.Path);
    EXPECT_EQ(5u,result.Geometry.Faces.size()); EXPECT_TRUE(result.Geometry.Solids.empty());
    EXPECT_TRUE(result.IsValidBRep);
}

TEST(FiveAxisCadVisibility, EntirelyHiddenStepFailsWithoutInventingVisibleGeometry)
{
    CSourceFixture source(L".step");
    occ::handle<TDocStd_Document> doc = new TDocStd_Document("BinXCAF");
    const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    colors->SetVisibility(shapes->AddShape(BRepPrimAPI_MakeBox(10,20,30).Shape()),false);
    WriteVisibleStep(source.Path,doc);
    try { LoadVisibleFixture(source.Path); FAIL() << "Expected no visible geometry"; }
    catch (const std::runtime_error& e) { EXPECT_NE(std::string::npos,std::string(e.what()).find("CAD_EMPTY_VISIBLE_GEOMETRY")); }
}

TEST(FiveAxisCadVisibility, HiddenStepPresentationExcludesItsStyledMembers)
{
    CSourceFixture source(L".step");
    occ::handle<TDocStd_Document> doc = new TDocStd_Document("BinXCAF");
    const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    shapes->AddShape(BRepPrimAPI_MakeBox(10,20,30).Shape());
    const auto hidden = shapes->AddShape(BRepPrimAPI_MakeBox(gp_Pnt(100,0,0),10,20,30).Shape());
    colors->SetColor(hidden,Quantity_Color(Quantity_NOC_GRAY),XCAFDoc_ColorGen);
    WriteVisibleStep(source.Path,doc,false,true);
    const auto result=LoadVisibleFixture(source.Path);
    ExpectBox(result.Geometry,10,20,30);
    EXPECT_NEAR(10,result.Bounds.Max.X,1e-7);
}

TEST(FiveAxisCadVisibility, StepPartOnVisibleAndHiddenLayersRemainsVisible)
{
    CSourceFixture source(L".step");
    occ::handle<TDocStd_Document> doc = new TDocStd_Document("BinXCAF");
    const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto layers = XCAFDoc_DocumentTool::LayerTool(doc->Main());
    const auto part = shapes->AddShape(BRepPrimAPI_MakeBox(10,20,30).Shape());
    const auto hidden=layers->AddLayer("hidden"); layers->SetVisibility(hidden,false);
    layers->SetLayer(part,hidden); layers->SetLayer(part,layers->AddLayer("visible"));
    WriteVisibleStep(source.Path,doc);
    ExpectBox(LoadVisibleFixture(source.Path).Geometry,10,20,30);
}

TEST(FiveAxisCadVisibility, IgesBlankRootsAreExcludedButHiddenTrimmingDependenciesRemain)
{
    CSourceFixture source(L".igs");
    IGESControl_Writer writer("MM",0);
    ASSERT_TRUE(writer.AddShape(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,10,0,20).Face()));
    ASSERT_TRUE(writer.AddShape(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(100,0,0),gp_Dir(0,0,1)),0,10,0,20).Face()));
    int surfaces = 0;
    for (int i=1;i<=writer.Model()->NbEntities();++i)
    {
        const auto entity = writer.Model()->Entity(i);
        // Hide every supporting entity and only the second independent face.
        const bool hide = entity->TypeNumber()!=144 || ++surfaces>1;
        entity->InitStatus(hide?1:0,entity->SubordinateStatus(),entity->UseFlag(),entity->HierarchyStatus());
    }
    ASSERT_EQ(2,surfaces);
    { std::ofstream stream(source.Path,std::ios::binary); ASSERT_TRUE(writer.Write(stream)); }
    const auto result = LoadVisibleFixture(source.Path);
    EXPECT_EQ(1u,result.Geometry.Faces.size()); EXPECT_TRUE(result.IsValidBRep);
    EXPECT_NEAR(0,result.Bounds.Min.X,1e-7); EXPECT_NEAR(10,result.Bounds.Max.X,1e-7);
    EXPECT_EQ(4u,result.Geometry.Edges.size());
}

TEST(FiveAxisCadVisibility, RealMultiSlotCoverImportsVisibleGroupOnly)
{
    const auto path=std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path()
        / u8"../../../../../../TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs");
    const auto result=LoadVisibleFixture(path);
    IGESControl_Reader original; original.SetReadVisible(false);
    const auto bytes=path.u8string();
    ASSERT_EQ(IFSelect_RetDone,original.ReadFile(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()).c_str()));
    ASSERT_GT(original.TransferRoots(),0);
    int allFaces=0; for(TopExp_Explorer f(original.OneShape(),TopAbs_FACE);f.More();f.Next())++allFaces;
    // The hidden/visible IGES groups reference the source faces more than once.
    EXPECT_EQ(1252,allFaces);
    EXPECT_TRUE(result.IsValidBRep);
    EXPECT_EQ(4u,result.Geometry.Faces.size());
    const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(result.Geometry);
    ASSERT_TRUE(rebuilt.bOK); EXPECT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
    std::cout << "Visible cover: unfiltered face uses=" << allFaces << " retained faces=" << result.Geometry.Faces.size()
              << " edges=" << result.Geometry.Edges.size() << " roots=" << result.TransferredRoots
              << " boundsZ=" << result.Bounds.Min.Z << "," << result.Bounds.Max.Z << std::endl;
}

TEST(FiveAxisCadImport, InvalidSourceOptionsAndCancellationDoNotReturnGeometry)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    CSourceFixture source(L".step");
    WriteStep(source.Path, BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath = "relative.step";
    EXPECT_THROW(service->Load(request), std::invalid_argument);
    request.SourcePath = source.Path;
    request.DisplayDeflectionMm = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(service->Load(request), std::invalid_argument);
    request.DisplayDeflectionMm = 0.1;
    request.MaximumSourceBytes = 1;
    EXPECT_THROW(service->Load(request), std::invalid_argument);
    request.MaximumSourceBytes = 512ull * 1024 * 1024;
    request.IsCancelled = [] { return true; };
    EXPECT_THROW(service->Load(request), iCAX::Tasks::TaskCanceledException);
    request.IsCancelled = {};
    { std::ofstream stream(source.Path, std::ios::binary | std::ios::trunc); stream << "not a STEP model"; }
    EXPECT_THROW(service->Load(request), std::runtime_error);
}

TEST(FiveAxisCadImport, ConcurrentIgesPreviewAndImportKeepEverySourceRoot)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    wchar_t path[32768];
    ASSERT_GT(GetEnvironmentVariableW(L"ICAX_EASYBLANK_IGES", path, 32768), 0u);
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath = path;
    // Warm module registration, then overlap independent preview/import loads.
    const auto original = service->Load(request);
    ASSERT_EQ(216u, original.TransferredRoots);
    ASSERT_EQ(216u, original.Geometry.Faces.size());
    for (int round = 0; round < 2; ++round)
    {
        std::latch start(4);
        std::vector<std::future<iCAX::CAD::SCadBRepImportResult>> loads;
        for (int i = 0; i < 4; ++i)
            loads.push_back(std::async(std::launch::async, [&] {
                start.count_down(); start.wait();
                return service->Load(request);
            }));
        for (auto& load : loads)
        {
            const auto imported = load.get();
            EXPECT_EQ(original.TransferredRoots, imported.TransferredRoots);
            EXPECT_EQ(original.Geometry.Faces.size(), imported.Geometry.Faces.size());
            EXPECT_EQ(original.Geometry.Edges.size(), imported.Geometry.Edges.size());
            EXPECT_EQ(original.SourceSha256, imported.SourceSha256);
            EXPECT_TRUE(imported.IsValidBRep);
        }
    }
}

TEST(FiveAxisCadImport, CancellingConcurrentIgesPreviewDoesNotCancelImport)
{
    iCAX::Services::CServiceProvider provider;
    const auto service = Resolve(provider);
    wchar_t path[32768];
    ASSERT_GT(GetEnvironmentVariableW(L"ICAX_EASYBLANK_IGES", path, 32768), 0u);
    iCAX::CAD::SCadBRepImportRequest request;
    request.SourcePath = path;
    std::atomic<bool> cancel = false;
    auto previewRequest = request;
    previewRequest.IsCancelled = [&] { return cancel.load(); };
    std::latch start(3);
    auto preview = std::async(std::launch::async, [&] { start.count_down(); start.wait(); return service->Load(previewRequest); });
    auto imported = std::async(std::launch::async, [&] { start.count_down(); start.wait(); return service->Load(request); });
    start.count_down(); start.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancel = true;
    EXPECT_THROW(preview.get(), iCAX::Tasks::TaskCanceledException);
    const auto result = imported.get();
    EXPECT_EQ(216u, result.TransferredRoots);
    EXPECT_EQ(216u, result.Geometry.Faces.size());
    EXPECT_TRUE(result.IsValidBRep);
}
