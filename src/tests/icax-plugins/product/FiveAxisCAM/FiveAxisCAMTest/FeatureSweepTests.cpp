#include "pch.h"
#include <OpenCascadeResourceImport/GeometryFeatureAnalysisService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/CadBRepTransformService.h>
#include <GeometryData/BRepPersistence.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Task/TaskStatus.h>
#include <CutRecognition/CutRecognitionService.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <numbers>
#include <STEPControl_Reader.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <iostream>
#include <GeometryServices/CurveOffset3DService.h>
#include <filesystem>
#include <fstream>
#include <bit>
#include <limits>

namespace
{
    using namespace iCAX::Geometry;
    auto Service(){iCAX::Services::CServiceProvider p;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(p);return p.Resolve<IGeometryFeatureAnalysisService>();}
    auto Model(const TopoDS_Shape& shape){return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"sweep test","sweep-test",0.1);}
    TopoDS_Shape Plate(){return BRepPrimAPI_MakeBox(gp_Pnt(-20,-20,0),40,40,10).Shape();}
    TopoDS_Shape Bore(double z=-1,double depth=12,double radius=4){return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,z),gp::DZ()),radius,depth).Shape();}
    auto CircleHole(){return BRepAlgoAPI_Cut(Plate(),Bore()).Shape();}
    std::pair<std::uint64_t,std::vector<std::uint64_t>> FindSection(const iCAX::GeometryData::BRepModel& model,double z,std::string relation="InnerLoop")
    {
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);
        for(const auto& [id,face]:built.Faces)
        {
            BRepAdaptor_Surface surface(face);if(surface.GetType()!=GeomAbs_Plane||!surface.Plane().Axis().Direction().IsParallel(gp::DZ(),1e-7)
                ||std::abs(surface.Plane().Location().Z()-z)>1e-6)continue;
            const auto a=Service()->AnalyzeFace(model,id,{});
            for(const auto& b:a.Boundaries)if(b.Valid&&b.RegionRelation==relation)
            {std::vector<std::uint64_t> edges;for(const auto& s:b.Spans)edges.push_back(s.EdgeID);return {id,edges};}
        }
        throw std::runtime_error("test section missing");
    }
    SBoundarySweepAnalysis Analyze(const TopoDS_Shape& shape,double z=10)
    {auto model=Model(shape);auto [id,edges]=FindSection(model,z);return Service()->AnalyzeBoundarySweep(model,id,edges,{});}
    std::string Diagnostics(const SBoundarySweepAnalysis& a){std::string text;for(auto& d:a.Diagnostics)text+=d+" ";return text;}
    auto Recognition()
    {
        EXPECT_EQ(1u,iCAX::CAM::Recognition::GetRecognitionContractVersion());
        iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
        return provider.Resolve<iCAX::CAM::Recognition::ICamCutRecognitionService>();
    }
    auto Input(const TopoDS_Shape& shape)
    {
        iCAX::CAM::Recognition::SRecognitionInput in;
        in.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(shape));
        in.SelectedFaces={FindSection(*in.Geometry,10).first};
        in.GeometryRef={"resource://test-geometry",1};in.TopologyMapRef=in.GeometryRef;in.PlanningProfileRef={"resource://test-profile",1};
        return in;
    }
    auto Opening(const iCAX::CAM::Recognition::SRecognitionResult& r)
    {
        for(const auto& c:r.Candidates)if(c.BoundaryRole=="Opening")return c;
        throw std::runtime_error("test opening missing");
    }
}
TEST(RetainedFaceSelection, PillarSelectsContinuousSkinAndRecognizesAllOpenings)
{
    wchar_t path[32768];ASSERT_GT(GetEnvironmentVariableW(L"ICAX_PILLAR_STEP",path,32768),0u);
    std::ifstream stream(std::filesystem::path(path),std::ios::binary);ASSERT_TRUE(stream.good());
    STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadStream("pillar.step",stream));reader.TransferRoots();
    iCAX::CAM::Recognition::SRetainedFaceSelectionInput selection;selection.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(reader.OneShape()));
    const auto before=iCAX::GeometryData::Persistence::Serialize(*selection.Geometry);
    const auto faces=Recognition()->SelectRetainedFaces(selection);
    // Face 1 is the complete formed skin. The small cylindrical faces in the
    // visible envelope are thickness walls around holes and the outer trim.
    EXPECT_EQ((std::vector<std::uint64_t>{1}),faces);
    iCAX::CAM::Recognition::SRecognitionInput in;in.Geometry=selection.Geometry;in.GeometryRef={"resource://pillar",1};in.TopologyMapRef=in.GeometryRef;in.PlanningProfileRef={"resource://profile",1};
    in.SelectedFaces=faces;in.SelectionMode="Faces";in.GeometryOnly=true;in.Representation="Solid";in.WorkpieceFamily="GeneralSolid";in.ObservationDirection=selection.ObservationDirection;
    const auto result=Recognition()->Recognize(in);ASSERT_TRUE(result.Complete);ASSERT_EQ(21u,result.Candidates.size());unsigned holes=0,outer=0;
    for(const auto& c:result.Candidates){EXPECT_EQ("Ready",c.ConfirmationEligibility);EXPECT_EQ("Cut",c.MachiningKind);holes+=c.BoundaryRole=="Opening";outer+=c.BoundaryRole=="OuterTrim";}
    EXPECT_EQ(20u,holes);EXPECT_EQ(1u,outer);EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(*selection.Geometry));
}
TEST(RetainedFaceSelection, EveryDisconnectedBodyContributesItsVisibleSkin)
{
    const auto a=CircleHole(),b=BRepBuilderAPI_Transform(a,[]{gp_Trsf t;t.SetTranslation(gp_Vec(80,0,0));return t;}(),true).Shape();
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);builder.Add(compound,a);builder.Add(compound,b);
    iCAX::CAM::Recognition::SRetainedFaceSelectionInput in;in.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(compound));
    const auto selected=Recognition()->SelectRetainedFaces(in);ASSERT_EQ(2u,selected.size());
    for(auto id:selected){const auto face=Service()->AnalyzeFace(*in.Geometry,id,{});EXPECT_TRUE(face.Planar);EXPECT_EQ(2u,face.Boundaries.size());}
    in.Options.IsCancelled=[]{return true;};EXPECT_THROW(Recognition()->SelectRetainedFaces(in),std::exception);
}
TEST(RetainedFaceSelection, TangentSkinSeamsRemainJoinedAndSelectionRotatesWithThePart)
{
    auto shape=BRepAlgoAPI_Fuse(BRepPrimAPI_MakeBox(gp_Pnt(-20,-20,0),20,40,10).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(0,-20,0),20,40,10).Shape()).Shape();
    shape=BRepAlgoAPI_Cut(shape,Bore()).Shape();
    std::vector<std::uint64_t> original;
    for(const double angle:{0.,.63}){
        gp_Trsf rotation;rotation.SetRotation(gp_Ax1({},gp_Dir(1,2,3)),angle);
        iCAX::CAM::Recognition::SRetainedFaceSelectionInput in;in.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(BRepBuilderAPI_Transform(shape,rotation,true).Shape()));
        const auto direction=gp_Dir(0,0,-1).Transformed(rotation);in.ObservationDirection={direction.X(),direction.Y(),direction.Z()};
        const auto selected=Recognition()->SelectRetainedFaces(in);ASSERT_EQ(2u,selected.size());if(angle==0)original=selected;else EXPECT_EQ(original,selected);
        const auto patch=Service()->AnalyzePatch(*in.Geometry,selected,{});ASSERT_EQ(2u,patch.Boundaries.size());EXPECT_GT(patch.CancelledInternalUses,0u);
    }
}
TEST(FeatureSweep, RoundPassageProvesEntireRegionAndKeepsExactSourceUses)
{
    for(const double z:{0.,10.})
    {
        const auto a=Analyze(CircleHole(),z);SCOPED_TRACE(Diagnostics(a));
        ASSERT_TRUE(a.Complete);ASSERT_TRUE(a.SingleSolid);ASSERT_TRUE(a.SideWallCoverage);
        ASSERT_TRUE(a.VolumeTestComplete);EXPECT_TRUE(a.SweptInteriorEmpty);EXPECT_EQ("LinearExtrusion",a.WallGeometry);
        EXPECT_EQ("InnerLoop",a.TerminalRelation);EXPECT_NEAR(10,a.LengthMm,1e-6);EXPECT_NEAR(z==0?1:-1,a.Direction[2],1e-8);
        ASSERT_EQ(1u,a.SideFaces.size());ASSERT_EQ(1u,a.TerminalFaces.size());
        for(const auto& s:a.Entrance.Spans){EXPECT_NE(0u,s.FaceID);EXPECT_NE(0u,s.WireID);}
    }
}

TEST(CurveOffsetModel, ManufacturerStampedBracketExactBoundariesOffsetAndConnectIn3D)
{
    using namespace iCAX::GeometryData;using namespace iCAX::GeometryServices;
    char path[32768];ASSERT_GT(GetEnvironmentVariableA("ICAX_STAMPED_BRACKET_STEP",path,sizeof(path)),0u);
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);const auto dll=std::filesystem::path(exe).parent_path()/"OpenCascadeGeometryServices.dll";
    static const auto module=LoadLibraryW(dll.c_str());ASSERT_TRUE(module);
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);const auto offset=provider.Resolve<ICurveOffset3DService>();ASSERT_TRUE(offset);
    STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadFile(path));reader.TransferRoots();const auto model=Model(reader.OneShape());
    const auto patch=Service()->AnalyzePatch(model,{4,16,7},{});ASSERT_TRUE(patch.Complete);unsigned tested=0,spatial=0;
    std::ofstream artifact;char report[32768];if(GetEnvironmentVariableA("ICAX_OFFSET_MODEL_CSV",report,sizeof(report))){artifact.open(report);artifact<<"boundary,piece,u,source_x,source_y,source_z,offset_x,offset_y,offset_z\n";}
    for(const auto& boundary:patch.Boundaries){if(!boundary.TopologyClosed||boundary.Spans.empty()||boundary.PerimeterMm<5)continue;
        // An oblique fixed cutting axis exercises the noncoplanar outer trim as
        // well as planar openings, without substituting a projected 2D contour.
        for(double distance:{-.1,.1}){
            std::vector<DirectionalCurveOffset3> laws;
            for(const auto& span:boundary.Spans){const auto curve=std::find_if(model.Curves3.begin(),model.Curves3.end(),[&](const auto& c){return c.Id==span.CurveID;});ASSERT_NE(curve,model.Curves3.end());
                DirectionalCurveOffset3 law;law.Source={curve->Geometry,{span.First,span.Last,span.Reversed}};law.ReferenceDirection=Direction3{1/std::sqrt(14.),2/std::sqrt(14.),3/std::sqrt(14.)};law.SignedDistance=distance*(span.Reversed?-1:1);laws.push_back(law);}
            CompositeOffsetOptions3 options;options.Tolerance=1e-4;options.MaxSamples=500000;
            std::cout<<"OFFSET_BOUNDARY "<<tested<<" "<<boundary.RegionRelation<<" perimeter="<<boundary.PerimeterMm<<" segments="<<laws.size()<<std::endl;
            const auto result=offset->BuildComposite(laws,boundary.Closed,options);ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<"boundary "<<tested<<" "<<boundary.RegionRelation<<" "<<distance<<" "<<result.Diagnostic;
            ASSERT_FALSE(result.Curve.Segments.empty());
            const auto range=[](const auto& p){if(const auto* law=std::get_if<DirectionalCurveOffset3>(&p.Geometry))return law->Source.Range;return std::get<CurveSegment3>(p.Geometry).Range;};
            for(size_t i=0;i<result.Curve.Segments.size();++i){const auto& p=result.Curve.Segments[i];const auto r=range(p);std::vector<double> parameters;for(int n=0;n<=16;++n)parameters.push_back(r.First+(r.Last-r.First)*n/16);
                const auto eval=offset->EvaluatePiece(p,parameters);ASSERT_EQ(ECurveOffsetStatus::SamplesEvaluated,eval.Status);
                for(const auto& s:eval.Samples){if(p.SourceSegment)EXPECT_NEAR(std::abs(distance),std::hypot(s.OffsetPoint.X-s.SourcePoint.X,s.OffsetPoint.Y-s.SourcePoint.Y,s.OffsetPoint.Z-s.SourcePoint.Z),1e-9);
                    if(artifact)artifact<<tested<<','<<i<<','<<s.SourceParameter<<','<<s.SourcePoint.X<<','<<s.SourcePoint.Y<<','<<s.SourcePoint.Z<<','<<s.OffsetPoint.X<<','<<s.OffsetPoint.Y<<','<<s.OffsetPoint.Z<<'\n';}
                const auto& next=result.Curve.Segments[(i+1)%result.Curve.Segments.size()];const auto nr=range(next);
                const auto a=offset->EvaluatePiece(p,{r.Reversed?r.First:r.Last}),b=offset->EvaluatePiece(next,{nr.Reversed?nr.Last:nr.First});
                EXPECT_LE(std::hypot(a.Samples[0].OffsetPoint.X-b.Samples[0].OffsetPoint.X,a.Samples[0].OffsetPoint.Y-b.Samples[0].OffsetPoint.Y,a.Samples[0].OffsetPoint.Z-b.Samples[0].OffsetPoint.Z),options.Tolerance*1.01);
            }
            ++tested;for(const auto& span:boundary.Spans)if(span.FaceID!=boundary.Spans.front().FaceID){++spatial;break;}
        }
    }
    std::cout<<"OFFSET_MODEL boundaries="<<tested<<" spatial="<<spatial<<std::endl;EXPECT_GE(tested,20u);EXPECT_GE(spatial,2u);
}
TEST(FeatureSweep, BlindCavityHasTerminalMaterialCapInsteadOfExit)
{
    const auto a=Analyze(BRepAlgoAPI_Cut(Plate(),Bore(5,6)).Shape());SCOPED_TRACE(Diagnostics(a));
    EXPECT_EQ("OuterLoop",a.TerminalRelation);EXPECT_NEAR(5,a.LengthMm,1e-6);
    EXPECT_TRUE(a.SideWallCoverage);EXPECT_TRUE(a.VolumeTestComplete);EXPECT_TRUE(a.SweptInteriorEmpty);
}
TEST(FeatureTermination, ConcaveAndConvexOppositeEdgesDistinguishThroughBlindAndRaised)
{
    BRepBuilderAPI_MakePolygon polygon;for(const gp_Pnt& p:{gp_Pnt(-4,-3,0),gp_Pnt(5,-2,0),gp_Pnt(2,0,0),gp_Pnt(5,4,0),gp_Pnt(-3,3,0)})polygon.Add(p);polygon.Close();
    const auto prism=[&](double z,double length){gp_Trsf t;t.SetTranslation(gp_Vec(0,0,z));return BRepPrimAPI_MakePrism(BRepBuilderAPI_Transform(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),t).Shape(),gp_Vec(0,0,length)).Shape();};
    const std::vector<std::pair<TopoDS_Shape,std::string>> cases{{BRepAlgoAPI_Cut(Plate(),prism(-1,12)).Shape(),"Cut"},
        {BRepAlgoAPI_Cut(Plate(),prism(8,3)).Shape(),"Mark"},{BRepAlgoAPI_Fuse(Plate(),prism(9,3)).Shape(),"Mark"}};
    for(const auto& [shape,kind]:cases)for(const double angle:{0.,.6}){
        auto in=Input(shape);in.GeometryOnly=true;
        if(angle){gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(11,2,-8),gp_Dir(1,2,3)),angle);
            in.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(BRepBuilderAPI_Transform(shape,transform,true).Shape()));
            // Conversion preserves face enumeration through the rigid transform.
        }
        const auto result=Recognition()->Recognize(in);unsigned found=0;
        for(const auto& c:result.Candidates)if(c.Boundary.RegionRelation=="InnerLoop"){
            ++found;EXPECT_EQ("GeneralContour",c.GeometryKind);EXPECT_EQ(kind,c.MachiningKind);EXPECT_EQ("Ready",c.ConfirmationEligibility);
            EXPECT_FALSE(c.Termination.SideFaces.empty());EXPECT_FALSE(c.Termination.OppositeEdges.empty());
            if(kind=="Cut"){EXPECT_EQ("Convex",c.Termination.OppositeRelation);EXPECT_EQ("ThroughLocalWall",c.PassageKind);}
        }EXPECT_EQ(1u,found);
    }
}
TEST(FeatureTermination, RealStampedBracketSeparatesEngravedLogoFromThroughHoles)
{
    char path[32768];ASSERT_GT(GetEnvironmentVariableA("ICAX_STAMPED_BRACKET_STEP",path,sizeof(path)),0u)<<"Set ICAX_STAMPED_BRACKET_STEP to the unmodified manufacturer STEP";
    STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadFile(path));reader.TransferRoots();
    auto model=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(reader.OneShape()));
    auto service=Service();unsigned marks=0,cuts=0,unknown=0;
    for(const auto& face:model->Faces){if(face.WireIds.size()<20)continue;auto boundaries=service->AnalyzeFace(*model,face.Id,{});
        if(!boundaries.Planar||boundaries.Boundaries.size()<20)continue;
        auto in=Input(CircleHole());in.Geometry=model;in.SelectedFaces={face.Id};in.GeometryOnly=true;in.Profile.Geometry.MaximumEdgeUses=200000;
        const auto result=Recognition()->Recognize(in);
        std::cout<<"BRACKET_FACE "<<face.Id<<" boundaries="<<result.Candidates.size()<<std::endl;
        for(const auto& c:result.Candidates){
            std::cout<<c.GeometryKind<<" "<<c.Boundary.RegionRelation<<" "<<c.Termination.EntranceRelation<<"/"<<c.Termination.OppositeRelation<<" "<<c.MachiningKind<<" edges="<<c.Termination.OppositeEdges.size()<<std::endl;
            if(c.MachiningKind=="Mark")++marks;else if(c.MachiningKind=="Cut")++cuts;else ++unknown;
        }
    }
    EXPECT_EQ(21u,marks);EXPECT_EQ(6u,cuts);EXPECT_EQ(0u,unknown);
}
TEST(FeatureTermination, StampedBracketMultiFacePatchKeepsClassificationAndSourceIdentity)
{
    char path[32768];ASSERT_GT(GetEnvironmentVariableA("ICAX_STAMPED_BRACKET_STEP",path,sizeof(path)),0u);
    STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadFile(path));reader.TransferRoots();
    auto in=Input(CircleHole());in.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(reader.OneShape()));
    in.GeometryOnly=true;in.Profile.Geometry.MaximumEdgeUses=200000;in.SelectedFaces={4};
    const auto single=Recognition()->Recognize(in);in.SelectedFaces={4,16,7};
    const auto patch=Service()->AnalyzePatch(*in.Geometry,in.SelectedFaces,in.Profile.Geometry);
    EXPECT_GT(patch.CancelledInternalUses,0u);
    const auto multiple=Recognition()->Recognize(in);unsigned marks=0,cuts=0,unknown=0,unchanged=0;
    for(const auto& c:multiple.Candidates){
        std::cout<<"PATCH "<<c.GeometryKind<<" "<<c.Boundary.RegionRelation<<" "<<c.Termination.EntranceRelation<<"/"<<c.Termination.OppositeRelation<<" "<<c.MachiningKind<<" spans="<<c.Boundary.Spans.size();
        for(const auto& issue:c.Termination.Diagnostics)std::cout<<" "<<issue;std::cout<<std::endl;
        if(c.MachiningKind=="Mark")++marks;else if(c.MachiningKind=="Cut")++cuts;else ++unknown;
        for(const auto& original:single.Candidates)if(c.SourceKey==original.SourceKey){
            ++unchanged;EXPECT_EQ(original.MachiningKind,c.MachiningKind);EXPECT_EQ(original.GeometryKind,c.GeometryKind);
            EXPECT_EQ(original.Boundary.RegionRelation,c.Boundary.RegionRelation);
        }
    }
    EXPECT_EQ(32u,multiple.Candidates.size());EXPECT_EQ(21u,marks);EXPECT_EQ(11u,cuts);EXPECT_EQ(0u,unknown);EXPECT_EQ(26u,unchanged);
    in.SelectedFaces={7,4,16};const auto reordered=Recognition()->Recognize(in);
    ASSERT_EQ(multiple.Candidates.size(),reordered.Candidates.size());
    for(size_t i=0;i<multiple.Candidates.size();++i){
        EXPECT_EQ(multiple.Candidates[i].SourceKey,reordered.Candidates[i].SourceKey);
        EXPECT_EQ(multiple.Candidates[i].MachiningKind,reordered.Candidates[i].MachiningKind);
    }
    // Selected faces' internal seams must never reappear in any extracted path.
    std::map<std::uint64_t,unsigned> uses;
    for(auto faceID:in.SelectedFaces)for(const auto& b:Service()->AnalyzeFace(*in.Geometry,faceID,in.Profile.Geometry).Boundaries)
        for(const auto& span:b.Spans)++uses[span.EdgeID];
    for(const auto& c:multiple.Candidates)for(const auto& span:c.Boundary.Spans)EXPECT_EQ(1u,uses[span.EdgeID]);
}
TEST(FeatureTermination, JoinedPlanarFacesCancelSeamsAndClassifyCrossFaceOpening)
{
    auto shape=BRepAlgoAPI_Fuse(BRepPrimAPI_MakeBox(gp_Pnt(-20,-20,0),20,40,10).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(0,-20,0),20,40,10).Shape()).Shape();
    for(const auto& p:{gp_Pnt(-10,0,-1),gp_Pnt(10,0,8),gp_Pnt(0,10,-1)})
        shape=BRepAlgoAPI_Cut(shape,BRepPrimAPI_MakeCylinder(gp_Ax2(p,gp::DZ()),3,12).Shape()).Shape();
    auto in=Input(CircleHole());in.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(shape));
    in.GeometryOnly=true;in.SelectedFaces.clear();
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);
    for(const auto& [id,face]:built.Faces){BRepAdaptor_Surface surface(face);
        if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().IsParallel(gp::DZ(),1e-7)&&std::abs(surface.Plane().Location().Z()-10)<1e-6)in.SelectedFaces.push_back(id);
    }
    ASSERT_EQ(2u,in.SelectedFaces.size());const auto result=Recognition()->Recognize(in);unsigned mark=0,cut=0,crossFace=0;
    for(const auto& c:result.Candidates){EXPECT_EQ("Ready",c.ConfirmationEligibility);
        mark+=c.MachiningKind=="Mark";cut+=c.MachiningKind=="Cut";
        if(c.GeometryKind=="Circle"&&std::any_of(c.Boundary.Spans.begin(),c.Boundary.Spans.end(),[&](const auto& s){return s.FaceID!=c.Boundary.Spans.front().FaceID;})){
            ++crossFace;EXPECT_EQ("Cut",c.MachiningKind);EXPECT_EQ("Convex",c.Termination.OppositeRelation);
        }
    }
    EXPECT_EQ(1u,mark);EXPECT_EQ(3u,cut);EXPECT_EQ(1u,crossFace);
    const auto patch=Service()->AnalyzePatch(*in.Geometry,in.SelectedFaces,{});
    auto partial=patch.Boundaries;for(auto& boundary:partial)if(boundary.Spans.size()>1)boundary.Spans.pop_back();
    const auto invalid=Service()->AnalyzeBoundaryTerminations(*in.Geometry,in.SelectedFaces,partial,{});
    for(size_t i=0;i<invalid.size();++i)if(patch.Boundaries[i].Spans.size()>1){EXPECT_EQ("Unknown",invalid[i].OppositeRelation);EXPECT_FALSE(invalid[i].Diagnostics.empty());}
}
TEST(FeatureTermination, OppositeSelectedSkinsKeepBothBoundariesRegardlessOfSelectionOrder)
{
    auto in=Input(CircleHole());in.GeometryOnly=true;const auto front=in.SelectedFaces.front();
    const auto back=FindSection(*in.Geometry,0).first;in.SelectedFaces={front,back};
    auto both=Recognition()->Recognize(in);ASSERT_EQ(4u,both.Candidates.size());
    unsigned frontCount=0,backCount=0;
    for(const auto& c:both.Candidates){EXPECT_EQ("Cut",c.MachiningKind);EXPECT_EQ("Ready",c.ConfirmationEligibility);
        frontCount+=c.Boundary.Spans.front().FaceID==front;backCount+=c.Boundary.Spans.front().FaceID==back;}
    EXPECT_EQ(2u,frontCount);EXPECT_EQ(2u,backCount);
    in.SelectedFaces={back,front};const auto reverse=Recognition()->Recognize(in);ASSERT_EQ(4u,reverse.Candidates.size());
    EXPECT_EQ(both.InputDigest,reverse.InputDigest);
    for(size_t i=0;i<both.Candidates.size();++i)EXPECT_EQ(both.Candidates[i].SourceKey,reverse.Candidates[i].SourceKey);
    in.SelectedFaces={front};const auto first=Recognition()->Recognize(in);in.SelectedFaces={back};const auto second=Recognition()->Recognize(in);
    for(const auto& c:first.Candidates){
        EXPECT_EQ(0,std::count_if(second.Candidates.begin(),second.Candidates.end(),[&](const auto& other){return c.SourceKey==other.SourceKey;}));
        EXPECT_EQ(1,std::count_if(both.Candidates.begin(),both.Candidates.end(),[&](const auto& other){return c.SourceKey==other.SourceKey;}));
    }
}
TEST(FeatureTermination, OppositeSideBlindFeaturesRemainDistinctMarks)
{
    // Aligned blind holes from opposite skins do not meet: proximity or matching
    // shape must never merge them, nor promote either one to a through cut.
    auto in=Input(BRepAlgoAPI_Cut(BRepAlgoAPI_Cut(Plate(),Bore(8,3)).Shape(),Bore(-1,3)).Shape());in.GeometryOnly=true;
    const auto front=in.SelectedFaces.front(),back=FindSection(*in.Geometry,0).first;in.SelectedFaces={front,back};
    const auto result=Recognition()->Recognize(in);std::set<std::string> markingKeys;unsigned marks=0;
    for(const auto& c:result.Candidates)if(c.MachiningKind=="Mark"){
        ++marks;EXPECT_EQ("Blind",c.PassageKind);EXPECT_EQ("Ready",c.ConfirmationEligibility);
        markingKeys.insert(c.SourceKey);
    }
    EXPECT_EQ(2u,marks);EXPECT_EQ(2u,markingKeys.size());EXPECT_EQ(4u,result.Candidates.size());
}
TEST(FeatureTermination, RealBracketKeepsBothSkinPathsAndClassifiesLogoAsMarking)
{
    char path[32768];ASSERT_GT(GetEnvironmentVariableA("ICAX_STAMPED_BRACKET_STEP",path,sizeof(path)),0u);
    STEPControl_Reader reader;ASSERT_EQ(IFSelect_RetDone,reader.ReadFile(path));reader.TransferRoots();
    auto in=Input(CircleHole());in.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(reader.OneShape()));
    in.GeometryOnly=true;in.Profile.Geometry.MaximumEdgeUses=200000;in.SelectedFaces={4,16,7};
    const auto front=Recognition()->Recognize(in);std::set<std::uint64_t> backFaces;
    for(const auto& c:front.Candidates)if(c.MachiningKind=="Cut")backFaces.insert(c.Termination.TerminalFaces.begin(),c.Termination.TerminalFaces.end());
    ASSERT_FALSE(backFaces.empty());std::cout<<"BACK_FACES";for(auto id:backFaces)std::cout<<" "<<id;std::cout<<std::endl;
    in.SelectedFaces.insert(in.SelectedFaces.end(),backFaces.begin(),backFaces.end());const auto both=Recognition()->Recognize(in);
    ASSERT_EQ(43u,both.Candidates.size());unsigned marks=0,cuts=0;
    for(const auto& c:both.Candidates){marks+=c.MachiningKind=="Mark";cuts+=c.MachiningKind=="Cut";EXPECT_EQ("Ready",c.ConfirmationEligibility);}
    for(const auto& c:front.Candidates)EXPECT_EQ(1,std::count_if(both.Candidates.begin(),both.Candidates.end(),[&](const auto& other){return other.SourceKey==c.SourceKey;}));
    EXPECT_EQ(21u,marks);EXPECT_EQ(22u,cuts);
    in.SelectedFaces.assign(backFaces.begin(),backFaces.end());const auto back=Recognition()->Recognize(in);
    for(const auto& c:back.Candidates){EXPECT_EQ("Cut",c.MachiningKind);EXPECT_EQ("Ready",c.ConfirmationEligibility);
        EXPECT_EQ(0,std::count_if(front.Candidates.begin(),front.Candidates.end(),[&](const auto& old){return old.SourceKey==c.SourceKey;}));
        EXPECT_EQ(1,std::count_if(both.Candidates.begin(),both.Candidates.end(),[&](const auto& other){return other.SourceKey==c.SourceKey;}));}
    EXPECT_EQ(11u,back.Candidates.size());
}
TEST(FeatureTermination, MultipleOppositePortsRemainUnresolvedAndCancellationDoesNotReturnPartialProof)
{
    const auto cross=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-21,0,5),gp::DX()),2,42).Shape();
    auto in=Input(BRepAlgoAPI_Cut(CircleHole(),cross).Shape());in.GeometryOnly=true;
    const auto result=Recognition()->Recognize(in);
    for(const auto& c:result.Candidates)if(c.Boundary.RegionRelation=="InnerLoop"){
        EXPECT_EQ("Unknown",c.MachiningKind);EXPECT_EQ("Blocked",c.ConfirmationEligibility);
        EXPECT_FALSE(c.Termination.Diagnostics.empty());
    }
    const auto face=Service()->AnalyzeFace(*in.Geometry,in.SelectedFaces.front(),{});SGeometryAnalysisOptions options;
    options.IsCancelled=[]{return true;};
    EXPECT_THROW(Service()->AnalyzeBoundaryTerminations(*in.Geometry,in.SelectedFaces,face.Boundaries,options),iCAX::Tasks::TaskCanceledException);
    options={};options.MaximumEdgeUses=1;
    EXPECT_THROW(Service()->AnalyzeBoundaryTerminations(*in.Geometry,in.SelectedFaces,face.Boundaries,options),std::runtime_error);
}
TEST(FeatureSweep, CircularBossTopCannotProduceVoidEvidence)
{
    const auto shape=BRepAlgoAPI_Fuse(Plate(),Bore(9,6)).Shape();const auto model=Model(shape);
    const auto [face,edges]=FindSection(model,15,"OuterLoop");const auto a=Service()->AnalyzeBoundarySweep(model,face,edges,{});
    EXPECT_FALSE(a.SideWallCoverage);EXPECT_FALSE(a.VolumeTestComplete);EXPECT_FALSE(a.SweptInteriorEmpty);
    ASSERT_FALSE(a.Diagnostics.empty());EXPECT_EQ("SECTION_IS_NOT_INNER_REGION",a.Diagnostics.front());
}
TEST(FeatureSweep, RectangularAndStadiumPassagesKeepShapeSeparateFromPassage)
{
    const auto rectangle=BRepPrimAPI_MakeBox(gp_Pnt(-6,-3,-1),12,6,12).Shape();
    const auto rect=Analyze(BRepAlgoAPI_Cut(Plate(),rectangle).Shape());SCOPED_TRACE(Diagnostics(rect));
    EXPECT_EQ("Rectangle",rect.Entrance.GeometryKind);EXPECT_EQ("InnerLoop",rect.TerminalRelation);
    EXPECT_TRUE(rect.SideWallCoverage);EXPECT_TRUE(rect.VolumeTestComplete);EXPECT_TRUE(rect.SweptInteriorEmpty);
    auto left=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-5,0,-1),gp::DZ()),3,12).Shape();
    auto right=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(5,0,-1),gp::DZ()),3,12).Shape();
    auto slot=BRepAlgoAPI_Fuse(BRepAlgoAPI_Fuse(left,right).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-5,-3,-1),10,6,12).Shape());slot.SimplifyResult();
    const auto stadium=Analyze(BRepAlgoAPI_Cut(Plate(),slot.Shape()).Shape());SCOPED_TRACE(Diagnostics(stadium));
    EXPECT_EQ("Slot",stadium.Entrance.GeometryKind);EXPECT_EQ("InnerLoop",stadium.TerminalRelation);
    EXPECT_TRUE(stadium.SideWallCoverage);EXPECT_TRUE(stadium.VolumeTestComplete);EXPECT_TRUE(stadium.SweptInteriorEmpty);
}
TEST(FeatureSweep, ConicalPassagePreservesDifferentSectionDiametersAndTaper)
{
    const auto cutter=BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0,0,-1),gp::DZ()),2.7,6.3,12).Shape();
    const auto a=Analyze(BRepAlgoAPI_Cut(Plate(),cutter).Shape());SCOPED_TRACE(Diagnostics(a));
    EXPECT_EQ("Conical",a.WallGeometry);EXPECT_NEAR(6,a.Entrance.RadiusMm,1e-6);EXPECT_NEAR(3,a.Terminal.RadiusMm,1e-6);
    EXPECT_NEAR(std::atan(.3),a.ConeHalfAngleRad,1e-7);EXPECT_EQ("InnerLoop",a.TerminalRelation);
    EXPECT_TRUE(a.SideWallCoverage);EXPECT_TRUE(a.VolumeTestComplete);EXPECT_TRUE(a.SweptInteriorEmpty);
}
TEST(FeatureSweep, CounterboreAndIntersectingBoreDoNotBecomeStraightPassages)
{
    const auto stepped=Analyze(BRepAlgoAPI_Cut(CircleHole(),Bore(6,5,7)).Shape());
    EXPECT_FALSE(stepped.SideWallCoverage);EXPECT_FALSE(stepped.VolumeTestComplete);
    EXPECT_EQ("OuterLoopWithOpenings",stepped.TerminalRelation);
    auto cross=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-21,0,5),gp::DX()),2,42).Shape();
    const auto branched=Analyze(BRepAlgoAPI_Cut(CircleHole(),cross).Shape());
    EXPECT_FALSE(branched.SideWallCoverage);EXPECT_FALSE(branched.VolumeTestComplete);
    EXPECT_NE(std::string::npos,Diagnostics(branched).find("BRANCHES"));
}
TEST(FeatureSweep, OffCentreMaterialObstructionCannotPassUsingOnlyTheCentreRay)
{
    // The z-axis stays empty, but an off-centre rib connects into the wall.
    const auto rib=BRepPrimAPI_MakeBox(gp_Pnt(2,-.1,4),4,.2,1).Shape();
    const auto a=Analyze(BRepAlgoAPI_Fuse(CircleHole(),rib).Shape());
    EXPECT_FALSE(a.SideWallCoverage&&a.VolumeTestComplete&&a.SweptInteriorEmpty);
    EXPECT_FALSE(a.Diagnostics.empty());
}
TEST(FeatureSweep, ScopeCancellationAndBudgetNeverReturnPartialProof)
{
    const auto model=Model(CircleHole());const auto [face,edges]=FindSection(model,10);auto service=Service();
    EXPECT_THROW(service->AnalyzeBoundarySweep(model,face,{},{}),std::invalid_argument);
    EXPECT_THROW(service->AnalyzeBoundarySweep(model,face,{999999},{}),std::invalid_argument);
    auto duplicated=edges;duplicated.push_back(edges.front());EXPECT_THROW(service->AnalyzeBoundarySweep(model,face,duplicated,{}),std::invalid_argument);
    SGeometryAnalysisOptions o;o.MaximumEdgeUses=10;EXPECT_THROW(service->AnalyzeBoundarySweep(model,face,edges,o),std::runtime_error);
    o={};o.IsCancelled=[]{return true;};EXPECT_THROW(service->AnalyzeBoundarySweep(model,face,edges,o),iCAX::Tasks::TaskCanceledException);
}
TEST(CutRecognition, AProvenPassageOnlyPermitsIntentCreationAndNeverGrantsProcessQualification)
{
    const auto result=Recognition()->Recognize(Input(CircleHole()));ASSERT_TRUE(result.Complete);
    EXPECT_EQ(result.RequestedFaces,result.CheckedFaces);EXPECT_EQ(64u,result.InputDigest.size());
    const auto cut=Opening(result);EXPECT_EQ("Circle",cut.GeometryKind);EXPECT_EQ("ThroughLocalWall",cut.PassageKind);
    EXPECT_EQ("Ready",cut.ConfirmationEligibility);EXPECT_EQ("NeedsProcessValidation",cut.ProcessEligibility);
    EXPECT_EQ("TopologyAndAnalyticVolume",cut.Proof);EXPECT_EQ("RelativeToStoredBRepOnly",cut.SourceErrorCoverage);
    EXPECT_EQ(64u,cut.SourceKey.size());EXPECT_EQ(3u,cut.Predicates.size());
    for(const auto& c:result.Candidates)if(c.BoundaryRole=="PatchBoundary")EXPECT_EQ("NeedsChoice",c.ConfirmationEligibility);
}
TEST(CutRecognition, BlindCavityIsBlockedAndConicalPassageRequiresAMeasurementDecision)
{
    auto service=Recognition();const auto blind=Opening(service->Recognize(Input(BRepAlgoAPI_Cut(Plate(),Bore(5,6)).Shape())));
    EXPECT_EQ("Circle",blind.GeometryKind);EXPECT_EQ("Blind",blind.PassageKind);EXPECT_EQ("Blocked",blind.ConfirmationEligibility);EXPECT_EQ("Blocked",blind.ProcessEligibility);
    auto cone=BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0,0,-1),gp::DZ()),2.7,6.3,12).Shape();
    const auto tapered=Opening(service->Recognize(Input(BRepAlgoAPI_Cut(Plate(),cone).Shape())));
    EXPECT_EQ("ThroughLocalWall",tapered.PassageKind);EXPECT_EQ("NeedsChoice",tapered.ConfirmationEligibility);
    EXPECT_NEAR(6,tapered.Boundary.RadiusMm,1e-6);EXPECT_NEAR(3,tapered.Sweep.Terminal.RadiusMm,1e-6);
}
TEST(CutRecognition, KindSizeAndSourceAccuracyFiltersCannotSilentlyFallbackToGeneralContour)
{
    auto service=Recognition();auto in=Input(CircleHole());in.RequestedKinds={"GeneralContour"};
    auto c=Opening(service->Recognize(in));EXPECT_EQ("Circle",c.GeometryKind);EXPECT_EQ("Blocked",c.ConfirmationEligibility);
    in.RequestedKinds={"Circle"};in.Profile.Radius.MaximumMm=3;
    c=Opening(service->Recognize(in));EXPECT_EQ("Blocked",c.ConfirmationEligibility);
    in.Profile.Radius.MaximumMm=10;in.SourceErrorUpperBoundMm=.02;
    c=Opening(service->Recognize(in));EXPECT_EQ("KnownBound",c.SourceErrorCoverage);EXPECT_EQ("Blocked",c.ConfirmationEligibility);
}
TEST(CutRecognition, SurfaceModeRequiresThicknessAndDoesNotInventSolidPassageEvidence)
{
    auto in=Input(CircleHole());auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);
    in.Geometry=std::make_shared<const iCAX::GeometryData::BRepModel>(Model(built.Faces.at(in.SelectedFaces[0])));
    in.SelectedFaces={in.Geometry->Faces.front().Id};in.Representation="SkinSurface";in.WorkpieceFamily="SheetSurface";
    auto service=Recognition();EXPECT_THROW(service->Recognize(in),std::invalid_argument);
    in.DeclaredThicknessMm=2;const auto r=service->Recognize(in);EXPECT_EQ(2u,r.Candidates.size());
    for(const auto& c:r.Candidates)
    {EXPECT_EQ("Ready",c.ConfirmationEligibility);EXPECT_EQ("Unknown",c.PassageKind);EXPECT_EQ("None",c.Proof);EXPECT_FALSE(c.Sweep.VolumeTestComplete);}
}
TEST(CutRecognition, IdentityIsDeterministicButChangesWithSourceVersionAndRules)
{
    auto in=Input(CircleHole());auto service=Recognition();const auto a=service->Recognize(in),b=service->Recognize(in);
    EXPECT_EQ(a.InputDigest,b.InputDigest);EXPECT_EQ(Opening(a).SourceKey,Opening(b).SourceKey);
    in.Profile.Radius.MaximumMm=20;const auto changedRule=service->Recognize(in);
    EXPECT_NE(a.InputDigest,changedRule.InputDigest);EXPECT_EQ(Opening(a).SourceKey,Opening(changedRule).SourceKey);
    ++in.GeometryRef.nVersion;const auto changedSource=service->Recognize(in);
    EXPECT_NE(Opening(a).SourceKey,Opening(changedSource).SourceKey);
}
TEST(CutRecognition, UnsupportedInputsAndIncompleteBudgetsCannotPublishSuccessfulCandidates)
{
    auto in=Input(CircleHole());auto service=Recognition();
    in.SelectedFaces.clear();EXPECT_THROW(service->Recognize(in),std::invalid_argument);
    in=Input(CircleHole());in.GeometryRef.nVersion=0;EXPECT_THROW(service->Recognize(in),std::invalid_argument);
    in=Input(CircleHole());in.WorkpieceFamily="BentTube";EXPECT_THROW(service->Recognize(in),std::invalid_argument);
    in=Input(CircleHole());in.OccurrencePath={1};EXPECT_THROW(service->Recognize(in),std::invalid_argument);
    in=Input(CircleHole());in.Profile.MaximumCandidates=1;EXPECT_THROW(service->Recognize(in),std::runtime_error);
    in=Input(CircleHole());in.Profile.Geometry.IsCancelled=[]{return true;};EXPECT_THROW(service->Recognize(in),iCAX::Tasks::TaskCanceledException);
}
TEST(CutRecognition, TopologyUsePathsIdentifyOccurrencesAndRejectIgnoredLocationsOrCycles)
{
    using namespace iCAX::CAM::Recognition;using namespace iCAX::GeometryData;
    const auto in=Input(CircleHole());const auto uses=EnumerateFaceUses(*in.Geometry);ASSERT_EQ(in.Geometry->Faces.size(),uses.size());
    for(const auto& use:uses)EXPECT_FALSE(use.UsePath.empty());
    auto moved=std::make_shared<BRepModel>(*in.Geometry);moved->RootShapes[0].Location.Matrix.Values[0][3]=10;
    EXPECT_THROW(EnumerateFaceUses(*moved),std::invalid_argument);
    auto repeated=std::make_shared<BRepModel>(*in.Geometry);repeated->RootShapes.push_back(repeated->RootShapes.front());
    auto invalid=in;invalid.Geometry=repeated;EXPECT_THROW(Recognition()->Recognize(invalid),std::invalid_argument);
    BRepModel cycle;cycle.Compounds.push_back({1,{{EBRepShapeKind::Compound,1}}});cycle.RootShapes.push_back({EBRepShapeKind::Compound,1});
    EXPECT_THROW(EnumerateFaceUses(cycle),std::invalid_argument);
}

namespace {
using LocationTestModel=iCAX::GeometryData::BRepModel;
LocationTestModel NestedLocationTestModel(const LocationTestModel& source){
    auto result=source;result.Compounds.push_back({987654,result.RootShapes});result.RootShapes={{iCAX::GeometryData::EBRepShapeKind::Compound,987654}};return result;
}
iCAX::GeometryData::Transform3 RecordedFrameResetLocation(){
    // Captured arithmetic remainder of the real OBB alignment's T*I*T^-1.
    // Geometry is already in its final frame; these entries are not a pose.
    iCAX::GeometryData::Transform3 result;auto& m=result.Matrix.Values;
    m[0][0]=.9999999999999991;m[0][1]=-6.157398269790351e-18;m[0][2]=2.117582368135751e-22;m[0][3]=-9.094947017729282e-13;
    m[1][0]=-6.157398269790351e-18;m[1][1]=1.0000000000000002;m[1][2]=2.710505431213761e-20;m[1][3]=-1.1368683772161603e-13;
    m[2][0]=2.117582368135751e-22;m[2][1]=2.710505431213761e-20;m[2][2]=1.0000000000000002;m[2][3]=-7.105427357601002e-15;
    return result;
}
}
TEST(CutRecognition, FrameResetTransformPreservesExactIdentityRootAndNestedUses){
    auto model=NestedLocationTestModel(Model(CircleHole()));
    const auto before=iCAX::GeometryData::Persistence::Serialize(model);
    const std::array<double,16> alignment{.9999999998260892,1.861485276987221e-5,-1.143642295118793e-6,-891.5180065079774,
        1.861503671114935e-5,-.9999999868583178,.00016104919974098622,773.9870083468954,
        -1.1406443729475253e-6,-.00016104922100192143,-.9999999870309236,52.81597717900577,0,0,0,1};
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);const auto service=provider.Resolve<iCAX::Geometry::ICadBRepTransformService>();ASSERT_TRUE(service);
    const auto transformed=service->Transform(model,alignment);
    const auto exactIdentity=[](const auto& ref){for(size_t row=0;row<4;++row)for(size_t col=0;col<4;++col){const double expected=row==col?1.:0.;EXPECT_EQ(std::bit_cast<std::uint64_t>(expected),std::bit_cast<std::uint64_t>(ref.Location.Matrix.Values[row][col]));}};
    for(const auto& root:transformed.RootShapes)exactIdentity(root);for(const auto& compound:transformed.Compounds)for(const auto& child:compound.Children)exactIdentity(child);
    EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(model));EXPECT_NE(before,iCAX::GeometryData::Persistence::Serialize(transformed));
    EXPECT_EQ(model.Faces.size(),iCAX::CAM::Recognition::EnumerateFaceUses(transformed).size());
}
TEST(CutRecognition, RecordedFrameResetRemaindersRemainReadableThroughNestedUsesWithoutMutatingGeometry){
    auto input=Input(CircleHole());const auto expected=Recognition()->Recognize(input);auto model=std::make_shared<LocationTestModel>(NestedLocationTestModel(*input.Geometry));
    model->RootShapes.front().Location=RecordedFrameResetLocation();for(auto& child:model->Compounds.back().Children)child.Location=RecordedFrameResetLocation();input.Geometry=model;
    const auto before=iCAX::GeometryData::Persistence::Serialize(*model);std::vector<iCAX::CAM::Recognition::SFaceUse> uses;ASSERT_NO_THROW(uses=iCAX::CAM::Recognition::EnumerateFaceUses(*model));ASSERT_EQ(model->Faces.size(),uses.size());
    iCAX::CAM::Recognition::SRecognitionResult actual;ASSERT_NO_THROW(actual=Recognition()->Recognize(input));EXPECT_TRUE(actual.Complete);ASSERT_EQ(expected.Candidates.size(),actual.Candidates.size());
    const auto original=Opening(expected),recognized=Opening(actual);EXPECT_EQ(original.GeometryKind,recognized.GeometryKind);EXPECT_EQ(original.PassageKind,recognized.PassageKind);EXPECT_EQ(original.ConfirmationEligibility,recognized.ConfirmationEligibility);EXPECT_NEAR(original.Boundary.RadiusMm,recognized.Boundary.RadiusMm,1e-10);
    EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(*model));
}
TEST(CutRecognition, RealOrAccumulatedOccurrenceTransformsAndNonfiniteEntriesRemainRejected){
    using namespace iCAX::CAM::Recognition;const auto source=Input(CircleHole());const auto reject=[&](const LocationTestModel& model){
        EXPECT_THROW(EnumerateFaceUses(model),std::invalid_argument);
        auto input=source;input.Geometry=std::make_shared<const LocationTestModel>(model);
        EXPECT_THROW(Recognition()->Recognize(input),std::invalid_argument);
    };
    auto model=NestedLocationTestModel(*source.Geometry);model.RootShapes.front().Location.Matrix.Values[0][3]=10;reject(model);
    model=NestedLocationTestModel(*source.Geometry);model.Compounds.back().Children.front().Location.Matrix.Values[1][3]=10;reject(model);
    model=NestedLocationTestModel(*source.Geometry);model.RootShapes.front().Location.Matrix.Values[0][3]=2e-11;reject(model);
    model=NestedLocationTestModel(*source.Geometry);model.RootShapes.front().Location.Matrix.Values[0][1]=-2e-14;model.RootShapes.front().Location.Matrix.Values[1][0]=2e-14;reject(model);
    model=NestedLocationTestModel(*source.Geometry);model.RootShapes.front().Location.Matrix.Values[0][3]=5e-12;EXPECT_NO_THROW(EnumerateFaceUses(model));model.Compounds.back().Children.front().Location.Matrix.Values[0][3]=8e-12;reject(model);
    model=NestedLocationTestModel(*source.Geometry);const double perturbation=20*std::numeric_limits<double>::epsilon();model.RootShapes.front().Location.Matrix.Values[0][1]=perturbation;EXPECT_NO_THROW(EnumerateFaceUses(model));model.Compounds.back().Children.front().Location.Matrix.Values[0][1]=perturbation;reject(model);
    for(size_t row=0;row<4;++row)for(size_t col=0;col<4;++col){SCOPED_TRACE(std::to_string(row)+","+std::to_string(col));model=NestedLocationTestModel(*source.Geometry);model.RootShapes.front().Location.Matrix.Values[row][col]=std::numeric_limits<double>::quiet_NaN();reject(model);}
    model=NestedLocationTestModel(*source.Geometry);model.RootShapes.front().Location.Matrix.Values[0][0]=std::numeric_limits<double>::max();model.RootShapes.front().Location.Matrix.Values[0][1]=std::numeric_limits<double>::max();reject(model);
    model=NestedLocationTestModel(*source.Geometry);model.RootShapes.front().Location.Matrix.Values[3][0]=1e-20;reject(model);
}

TEST(CutRecognition, MixedTopologyKeepsFacePathsAndDoesNotTreatCurvesAsFaces)
{
    using namespace iCAX::CAM::Recognition;using namespace iCAX::GeometryData;
    auto in=Input(CircleHole());auto mixed=std::make_shared<BRepModel>(*in.Geometry);
    const auto original=EnumerateFaceUses(*mixed);
    const auto vertex=BRepShapeRef{EBRepShapeKind::Vertex,mixed->Vertices.front().Id};
    const auto edge=BRepShapeRef{EBRepShapeKind::Edge,mixed->Edges.front().Id};
    const auto wire=BRepShapeRef{EBRepShapeKind::Wire,mixed->Wires.front().Id};
    std::vector<BRepShapeRef> children{vertex,edge,wire};
    children.insert(children.end(),mixed->RootShapes.begin(),mixed->RootShapes.end());
    mixed->Compounds.push_back({987654,std::move(children)});
    mixed->RootShapes={edge,{EBRepShapeKind::Compound,987654},wire,vertex};
    const auto uses=EnumerateFaceUses(*mixed);ASSERT_EQ(original.size(),uses.size());
    for(size_t i=0;i<uses.size();++i){
        EXPECT_EQ(original[i].FaceID,uses[i].FaceID);
        auto path=original[i].UsePath;path.front()+=3;path.insert(path.begin(),1);
        EXPECT_EQ(path,uses[i].UsePath);
    }
    const auto expected=Recognition()->Recognize(in);in.Geometry=mixed;
    const auto actual=Recognition()->Recognize(in);
    ASSERT_EQ(expected.Candidates.size(),actual.Candidates.size());
    for(size_t i=0;i<expected.Candidates.size();++i){
        EXPECT_EQ(expected.Candidates[i].SourceKey,actual.Candidates[i].SourceKey);
        EXPECT_EQ(expected.Candidates[i].ConfirmationEligibility,actual.Candidates[i].ConfirmationEligibility);
    }
    mixed->RootShapes={wire,edge,vertex};EXPECT_TRUE(EnumerateFaceUses(*mixed).empty());
    mixed->RootShapes.push_back({EBRepShapeKind::Wire,987654});
    EXPECT_THROW(EnumerateFaceUses(*mixed),std::invalid_argument);
    mixed->RootShapes.pop_back();mixed->Edges.push_back(mixed->Edges.front());
    EXPECT_THROW(EnumerateFaceUses(*mixed),std::invalid_argument);
}

TEST(CutRecognition, DisjointBodiesClassifyEachBoundaryOnItsOwnMaterial)
{
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
    builder.Add(compound,CircleHole());
    gp_Trsf translation;translation.SetTranslation(gp_Vec(80,0,0));
    const auto blind=BRepAlgoAPI_Cut(Plate(),Bore(5,10)).Shape();
    builder.Add(compound,BRepBuilderAPI_Transform(blind,translation,true).Shape());
    builder.Add(compound,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(200,0,20),gp::DZ()),-10,10,-10,10).Shape());
    auto in=Input(compound);in.GeometryOnly=true;in.SelectedFaces.clear();
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);
    for(const auto& [id,face]:built.Faces){
        const BRepAdaptor_Surface surface(face);
        if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().IsParallel(gp::DZ(),1e-7)
            &&std::abs(surface.Plane().Location().Z()-10)<1e-6)in.SelectedFaces.push_back(id);
    }
    ASSERT_EQ(2u,in.SelectedFaces.size());
    const auto result=Recognition()->Recognize(in);ASSERT_EQ(4u,result.Candidates.size());
    size_t through=0,blindMarks=0;
    for(const auto& c:result.Candidates){
        EXPECT_EQ("Ready",c.ConfirmationEligibility);
        EXPECT_EQ("NeedsProcessValidation",c.ProcessEligibility);
        if(c.PassageKind=="ThroughLocalWall"){++through;EXPECT_EQ("Cut",c.MachiningKind);}
        if(c.PassageKind=="Blind"){++blindMarks;EXPECT_EQ("Mark",c.MachiningKind);}
    }
    EXPECT_EQ(1u,through);EXPECT_EQ(1u,blindMarks);
}
TEST(FiveAxisManualSelection, UnorderedEdgesFormOneExactLoopAndRejectIncompleteOrMixedLoops)
{
    auto in=Input(CircleHole());auto service=Service();
    const auto face=service->AnalyzeFace(*in.Geometry,in.SelectedFaces.front(),{});
    std::vector<SBoundaryCurveSpan> outer,inner;
    for(const auto& b:face.Boundaries)(b.RegionRelation=="OuterLoop"?outer:inner)=b.Spans;
    ASSERT_EQ(4u,outer.size());ASSERT_EQ(1u,inner.size());
    std::swap(outer[0],outer[2]);std::swap(outer[1],outer[3]);
    const auto loop=service->AnalyzeEdgeLoop(*in.Geometry,outer,{});
    EXPECT_TRUE(loop.TopologyClosed);EXPECT_EQ(4u,loop.Spans.size());EXPECT_NEAR(160.,loop.PerimeterMm,1e-6);
    EXPECT_EQ("Rectangle",loop.GeometryKind);
    auto partial=outer;partial.pop_back();EXPECT_THROW(service->AnalyzeEdgeLoop(*in.Geometry,partial,{}),std::invalid_argument);
    auto repeated=outer;repeated.push_back(outer.front());EXPECT_THROW(service->AnalyzeEdgeLoop(*in.Geometry,repeated,{}),std::invalid_argument);
    auto mixed=outer;mixed.insert(mixed.end(),inner.begin(),inner.end());EXPECT_THROW(service->AnalyzeEdgeLoop(*in.Geometry,mixed,{}),std::invalid_argument);
    EXPECT_TRUE(service->AnalyzeEdgeLoop(*in.Geometry,inner,{}).TopologyClosed);
    in.GeometryOnly=true;in.SelectionMode="Edges";in.SelectedEdges=outer;
    const auto result=Recognition()->Recognize(in);ASSERT_EQ(1u,result.Candidates.size());
    EXPECT_EQ("Ready",result.Candidates.front().ConfirmationEligibility);EXPECT_EQ("Cut",result.Candidates.front().MachiningKind);
    const auto edgeDigest=result.InputDigest;in.SelectedEdges.clear();in.SelectionMode="Region";
    const auto region=Recognition()->Recognize(in);ASSERT_EQ(2u,region.Candidates.size());EXPECT_NE(edgeDigest,region.InputDigest);
    for(const auto& c:region.Candidates)EXPECT_EQ("Ready",c.ConfirmationEligibility);
    in.SelectedFaces.clear();for(const auto& f:in.Geometry->Faces)in.SelectedFaces.push_back(f.Id);
    EXPECT_THROW(Recognition()->Recognize(in),std::invalid_argument);
}
