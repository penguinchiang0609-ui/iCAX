#include "OpenCascadeResourceImport/CadBevelService.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "TubeDesigner/TubeSideSketchCurveService.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"
#include "GeometryData/BRepPersistence.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <BRepTools.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <iostream>
#include <cmath>
#include <stdexcept>

namespace
{
    std::shared_ptr<iCAX::CAD::ICadBevelService> BevelService;
    double Volume(const TopoDS_Shape& S) {GProp_GProps P;BRepGProp::VolumeProperties(S,P,1.0e-10);return std::abs(P.Mass());}
    void Require(bool V,const std::string& M){if(!V) throw std::runtime_error(M);}
    void DiagnoseRoundTrip(const TopoDS_Shape& Source,const iCAX::OpenCascade::SOpenCascadeBRepBuildResult& Rebuilt)
    {
        for(const auto& D:Rebuilt.Diagnostics) std::cerr<<"BRep diagnostic "<<D<<"\n";
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> SourceFaces;TopExp::MapShapes(Source,TopAbs_FACE,SourceFaces);
        for(const auto& [ID,Face]:Rebuilt.Faces)
        {
            const BRepCheck_Analyzer Analyzer(Face);
            if(!Analyzer.IsValid(Face))
            {
                std::cerr<<"Invalid rebuilt face "<<ID<<" type="<<BRepAdaptor_Surface(Face).GetType()<<" statuses=";
                for(auto Status:Analyzer.Result(Face)->Status())std::cerr<<static_cast<int>(Status)<<",";
                std::cerr<<"\n";
            }
            for(TopExp_Explorer X(Face,TopAbs_EDGE);X.More();X.Next())
            {
                const auto Edge=TopoDS::Edge(X.Current());double First,Last;const auto C=BRep_Tool::Curve(Edge,First,Last);
                double PFirst,PLast;const auto PC=BRep_Tool::CurveOnSurface(Edge,Face,PFirst,PLast);const auto Surface=BRep_Tool::Surface(Face);
                if(C.IsNull() || PC.IsNull())continue;
                double Maximum=0;
                for(double T:{0.1,0.3,0.5,0.7,0.9})
                {
                    const auto UV=PC->Value(PFirst+(PLast-PFirst)*T);
                    Maximum=std::max(Maximum,C->Value(First+(Last-First)*T).Distance(Surface->Value(UV.X(),UV.Y())));
                }
                if(Maximum>1e-5)std::cerr<<"pcurve residual face="<<ID<<" edge="<<std::distance(Rebuilt.Edges.begin(),std::find_if(Rebuilt.Edges.begin(),Rebuilt.Edges.end(),[&](const auto& V){return V.second.IsSame(Edge);}))+1<<" error="<<Maximum<<" ranges="<<First<<","<<Last<<" / "<<PFirst<<","<<PLast<<"\n";
            }
        }
    }
    iCAX::CAD::SCadBevelOperation Pick(const TopoDS_Shape& Shape, double X,double Y,double Z)
    {
        const auto Topology=BevelService->InspectNative(Shape);Require(Topology.bOK,Topology.Diagnostic);
        double Best=1e100;std::uint64_t ID=0;
        for(const auto& Edge:Topology.Edges)for(std::size_t I=1;I<Edge.Points.size();++I)
        {
            const auto& A=Edge.Points[I-1];const auto& B=Edge.Points[I];
            const gp_Pnt P(X,Y,Z),Start(A[0],A[1],A[2]),End(B[0],B[1],B[2]);const gp_Vec Direction(Start,End),Delta(Start,P);
            const auto T=Direction.SquareMagnitude()>1e-20?std::clamp(Delta.Dot(Direction)/Direction.SquareMagnitude(),0.0,1.0):0.0;
            const auto D=P.Distance(Start.Translated(Direction*T));if(D<Best){Best=D;ID=Edge.EdgeID;}
        }
        Require(Best<1e-5,"Selection not found");return {ID,0,"outer",45,0.5};
    }
    std::uint64_t PlanarFaceAtZ(const TopoDS_Shape& Shape,double Z)
    {
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> Faces;TopExp::MapShapes(Shape,TopAbs_FACE,Faces);
        for(int I=1;I<=Faces.Extent();++I)
        {
            BRepAdaptor_Surface Surface(TopoDS::Face(Faces(I)));
            if(Surface.GetType()==GeomAbs_Plane && std::abs(Surface.Plane().Location().Z()-Z)<1e-6
                && std::abs(Surface.Plane().Axis().Direction().Z())>0.99) return I;
        }
        throw std::runtime_error("Reference face not found");
    }
    TopoDS_Shape RoundedTube(double Radius=5)
    {
        const auto RoundedBox=[](const TopoDS_Shape& Box,double R)
        {
            if(R<=0)return Box;
            BRepFilletAPI_MakeFillet Fillet(Box);
            for(TopExp_Explorer X(Box,TopAbs_EDGE);X.More();X.Next())
            {
                const auto Edge=TopoDS::Edge(X.Current());BRepAdaptor_Curve Curve(Edge);
                if(Curve.GetType()==GeomAbs_Line && std::abs(Curve.Line().Direction().Z())>.99)Fillet.Add(R,Edge);
            }
            Fillet.Build();Require(Fillet.IsDone(),"Rounded tube fixture failed");return Fillet.Shape();
        };
        return BRepAlgoAPI_Cut(RoundedBox(BRepPrimAPI_MakeBox(40,30,50).Shape(),Radius),
            RoundedBox(BRepPrimAPI_MakeBox(gp_Pnt(2,2,-1),36,26,52).Shape(),std::max(0.0,Radius-2))).Shape();
    }
    double CornerMidpoint(const TopoDS_Shape& Shape)
    {
        const auto Reference=iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(Shape);Require(Reference.bOK,Reference.Diagnostic);
        std::vector<double> Junctions{0,Reference.Perimeter};
        for(const auto& Surface:Reference.Surfaces)for(const auto& Wire:Surface.Wires)
            if(Wire.Role=="profile-junction" && !Wire.Points.empty())Junctions.push_back(Wire.Points.front().U);
        std::sort(Junctions.begin(),Junctions.end());
        for(std::size_t I=1;I<Junctions.size();++I)if(Junctions[I]-Junctions[I-1]<8 && Junctions[I]-Junctions[I-1]>1)
            return (Junctions[I]+Junctions[I-1])*0.5;
        throw std::runtime_error("Rounded corner not found in reference");
    }
    void Check(const TopoDS_Shape& S,iCAX::CAD::SCadBevelOperation Op,double Expected,const char* Name)
    {
        const auto Result=BevelService->ApplyNative(S,{Op});
        Require(Result.bOK,std::string(Name)+": "+Result.Diagnostic);
        Require(BRepCheck_Analyzer(Result.Shape).IsValid(),"Invalid result");
        Require(Result.RemovedVolume>0,"No volume removal");
        if(Expected>0)Require(std::abs(Result.RemovedVolume-Expected)<1e-3,"Incorrect bevel volume: "+std::to_string(Result.RemovedVolume)+" expected "+std::to_string(Expected));
        const auto Neutral=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Result.Shape,"bevel","test");
        const auto Rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(Neutral);
        Require(Rebuilt.bOK && BRepCheck_Analyzer(Rebuilt.Shape).IsValid(),"Neutral BRep failed round trip");
        Require(std::abs(Volume(Rebuilt.Shape)-Volume(Result.Shape))<1e-3,"Round-trip volume mismatch");
        if(Expected>0)
        {
            double Land=0;std::size_t ConeCount=0;
            for(TopExp_Explorer X(Result.Shape,TopAbs_FACE);X.More();X.Next())
            {
                const auto Face=TopoDS::Face(X.Current());BRepAdaptor_Surface Surface(Face);
                if(Surface.GetType()==GeomAbs_Cylinder && std::abs(Surface.Cylinder().Radius()-5)<1e-6)
                {
                    double U0,U1,V0,V1;BRepTools::UVBounds(Face,U0,U1,V0,V1);Land+=V1-V0;
                }
                if(Surface.GetType()==GeomAbs_Cone)
                {
                    Require(std::abs(std::abs(Surface.Cone().SemiAngle())-Op.AngleDegrees*3.14159265358979323846/180)<1e-6,"Incorrect bevel angle");
                    ++ConeCount;
                }
            }
            Require(std::abs(Land-Op.RootFace)<1e-6,"Incorrect remaining through-wall land");
            Require(ConeCount==(Op.Side=="both"?2:1),"Incorrect bevel side count");
        }
        std::cout<<"PASS "<<Name<<" removed="<<Result.RemovedVolume<<"\n";
    }
    void SketchCheck(const TopoDS_Shape& Blank,const char* Name,double U,double Width,const std::string& Kind="rectangle", bool Segments=false,bool AllowUnsupported=false)
    {
        using iCAX::Data::ObjectMap;using iCAX::Data::VariantArray;
        const auto Unfold=iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(Blank);Require(Unfold.bOK,Unfold.Diagnostic);
        ObjectMap Entity{{"kind",Kind=="roundedRectangle"?std::string("rectangle"):Kind},{"x",U},{"y",15.0},{"width",Width},{"height",12.0},{"cx",U},{"cy",25.0},{"radius",Kind=="circle"?5.0:(Kind=="roundedRectangle"?3.0:0.0)}};
        if(Kind=="line")Entity.insert({{"x1",U},{"y1",15.0},{"x2",U+Width},{"y2",27.0}});
        ObjectMap Sketch{{"length",Unfold.Length},{"faceHeight",Unfold.Perimeter},{"coordinateSpace",std::string("arc-length-axial")},{"entities",VariantArray{Entity}}};
        iCAX::TubeDesigner::STubeSideSketchOptions Options;Options.BevelService=BevelService.get();
        const auto Ordinary=iCAX::TubeDesigner::ApplyTubeSideSketchCurves(Blank,Sketch,Options);Require(Ordinary.bOK,std::string(Name)+" ordinary: "+Ordinary.Diagnostic);
        for(const std::string Side:{"outer","inner","both"})
        {
            ObjectMap Bevel{{"side",Side},{"angleDegrees",45.0},{"rootFace",0.5},{"enabled",true}};
            if(Segments)Bevel["segmentIndices"]=VariantArray{std::int64_t(0)};
            Entity["bevel"]=Bevel;Sketch["entities"]=VariantArray{Entity};
            const auto Result=iCAX::TubeDesigner::ApplyTubeSideSketchCurves(Blank,Sketch,Options);
            if(!Result.bOK && AllowUnsupported)
            {
                Require(Result.Diagnostic.find("暂不支持：")!=std::string::npos,"Unsupported geometry lacks clear diagnostic");
                Require(Result.Shape.IsNull() && BRepCheck_Analyzer(Blank).IsValid(),"Unsupported bevel modified input or returned partial shape");
                std::cout<<"PASS "<<Name<<" "<<Side<<" unsupported atomicity: "<<Result.Diagnostic<<"\n";continue;
            }
            Require(Result.bOK,std::string(Name)+" "+Side+": "+Result.Diagnostic);
            Require(Volume(Result.Shape)<Volume(Ordinary.Shape)-1e-5,"Sketch bevel did not remove material");
            Require(BRepCheck_Analyzer(Result.Shape).IsValid(),"Sketch bevel shape invalid");
            const auto Neutral=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Result.Shape,"sketch bevel","test");
            const auto Saved=iCAX::GeometryData::Persistence::Serialize(Neutral);
            const auto Loaded=iCAX::GeometryData::Persistence::Deserialize(Saved);
            const auto Rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(Loaded);
            if(!Rebuilt.bOK || !BRepCheck_Analyzer(Rebuilt.Shape).IsValid()) DiagnoseRoundTrip(Result.Shape,Rebuilt);
            Require(Rebuilt.bOK && BRepCheck_Analyzer(Rebuilt.Shape).IsValid(),std::string(Name)+" "+Side+": Sketch bevel BRep round trip failed");
            Require(std::abs(Volume(Rebuilt.Shape)-Volume(Result.Shape))<std::max(1e-3,Volume(Result.Shape)*1e-7),std::string(Name)+" "+Side+": Sketch bevel round-trip volume mismatch native="+std::to_string(Volume(Result.Shape))+" persisted="+std::to_string(Volume(Rebuilt.Shape)));
            Bevel["enabled"]=false;Entity["bevel"]=Bevel;Sketch["entities"]=VariantArray{Entity};
            const auto Disabled=iCAX::TubeDesigner::ApplyTubeSideSketchCurves(Blank,Sketch,Options);
            Require(Disabled.bOK && std::abs(Volume(Disabled.Shape)-Volume(Ordinary.Shape))<1e-4,"Disable did not restore original material");
            std::cout<<"PASS "<<Name<<" "<<Side<<"\n";
        }
    }
}
int main()
{
    try
    {
        iCAX::Services::CServiceProvider Services;
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(Services);
        BevelService=Services.Resolve<iCAX::CAD::ICadBevelService>();
        // Flat slab with an interior cylindrical through-hole: wall thickness
        // is 2. The cone widening is depth*tan(angle) from the original cut.
        const auto Plate=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(40,40,2).Shape(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,20,-1),gp_Dir(0,0,1)),5,4).Shape()).Shape();
        auto Op=Pick(Plate,25,20,2);const auto D=1.5;
        const auto ConeVolume=[&](double Depth,double Angle){const auto W=Depth*std::tan(Angle*3.14159265358979323846/180);return 3.14159265358979323846*(5*W*Depth+W*W*Depth/3);};
        Check(Plate,Op,ConeVolume(D,45),"flat outer 45");
        Op.Side="inner";Check(Plate,Op,ConeVolume(D,45),"flat inner 45");
        Op.Side="both";Check(Plate,Op,2*ConeVolume(D/2,45),"flat both 45");
        Op.Side="outer";Op.AngleDegrees=30;Check(Plate,Op,ConeVolume(D,30),"flat outer 30");
        Op.RootFace=0;Op.AngleDegrees=45;Check(Plate,Op,ConeVolume(2,45),"zero land");
        Op.RootFace=2;Require(!BevelService->ApplyNative(Plate,{Op}).bOK,"Invalid land accepted");
        Op=Pick(Plate,25,20,2);auto Conflict=Op;Conflict.AngleDegrees=30;
        Require(!BevelService->ApplyNative(Plate,{Op,Conflict}).bOK,"Conflicting duplicate bevel accepted");
        const auto Equivalent=BevelService->ApplyNative(Plate,{Op,Op});Require(Equivalent.bOK,"Equivalent duplicate rejected");
        Require(std::abs(Equivalent.RemovedVolume-ConeVolume(D,45))<1e-3,"Equivalent duplicate applied twice");
        auto Missing=Op;Missing.EdgeID=999999;Require(!BevelService->ApplyNative(Plate,{Missing}).bOK,"Missing edge accepted");
        // Neutral IDs are persisted identities, independent of traversal order.
        auto Imported=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Plate,"imported plate","fixture");
        for(auto& E:Imported.Edges)E.Id+=1000;
        for(auto& W:Imported.Wires)for(auto& C:W.Coedges)C.EdgeId+=1000;
        Imported=iCAX::GeometryData::Persistence::Deserialize(iCAX::GeometryData::Persistence::Serialize(Imported));
        const auto ImportedTopology=BevelService->Inspect(Imported);Require(ImportedTopology.bOK,ImportedTopology.Diagnostic);
        Require(std::all_of(ImportedTopology.Edges.begin(),ImportedTopology.Edges.end(),[](const auto& E){return E.EdgeID>1000;}),"Neutral topology IDs changed");
        auto ImportedOp=Op;ImportedOp.EdgeID+=1000;const auto ImportedBevel=BevelService->Apply(Imported,{ImportedOp});
        Require(ImportedBevel.bOK,ImportedBevel.Diagnostic);
        const auto ImportedResult=iCAX::OpenCascade::BuildOpenCascadeShape(ImportedBevel.Geometry);
        Require(ImportedResult.bOK && BRepCheck_Analyzer(ImportedResult.Shape).IsValid(),"Neutral bevel output invalid");
        Require(std::abs(ImportedBevel.RemovedVolume-ConeVolume(D,45))<1e-3,"Neutral bevel changed dimensions");
        auto Malformed=Imported;Malformed.Curves3.clear();
        Require(!BevelService->Inspect(Malformed).bOK && !BevelService->Apply(Malformed,{ImportedOp}).bOK,"Malformed BRep accepted or exception escaped");
        const auto Empty=BevelService->Apply(Imported,{});Require(Empty.bOK && Empty.Geometry.Edges.front().Id==Imported.Edges.front().Id,"Empty recipe changed persisted identity");
        std::cout<<"PASS neutral persisted selection IDs and duplicate/invalid atomicity\n";
        const auto Tube=BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(20,50).Shape(),BRepPrimAPI_MakeCylinder(18,50).Shape()).Shape();
        Op=Pick(Tube,20,0,50);
        Require(!BevelService->ApplyNative(Tube,{Op}).bOK,"Ambiguous reference face accepted");
        Op.ReferenceFaceID=PlanarFaceAtZ(Tube,50);Check(Tube,Op,0,"round tube outer end");
        Op.Side="inner";Check(Tube,Op,0,"round tube inner end");
        Op.Side="both";Check(Tube,Op,0,"round tube both end");
        const auto RectTube=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(40,30,50).Shape(),
            BRepPrimAPI_MakeBox(gp_Pnt(2,2,-1),36,26,52).Shape()).Shape();
        SketchCheck(RectTube,"flat wall rectangle",10,8);
        SketchCheck(RectTube,"flat wall selected segment",10,8,"rectangle",true);
        SketchCheck(RectTube,"flat wall narrow slot",10,1,"rectangle",true);
        SketchCheck(RectTube,"flat wall open kerf",10,8,"line");
        SketchCheck(RectTube,"flat wall rounded hole",8,12,"roundedRectangle");
        SketchCheck(RectTube,"partial tangent rounded hole",8,12,"roundedRectangle",true,true);
        SketchCheck(Tube,"round wall rectangle",10,8);
        SketchCheck(Tube,"round wall circle",25,10,"circle");
        SketchCheck(Tube,"round wall seam",-4,8);
        SketchCheck(Tube,"round wall open kerf",10,8,"line",false,true);
        const auto Rounded=RoundedTube();const auto Corner=CornerMidpoint(Rounded);
        SketchCheck(Rounded,"rounded corner",Corner-1.5,3,"rectangle",false,true);
        SketchCheck(Rounded,"rounded corner junction",Corner-6,12,"rectangle",false,true);
        const auto SmallRounded=RoundedTube(1);const auto SmallCorner=CornerMidpoint(SmallRounded);
        SketchCheck(SmallRounded,"small rounded corner",SmallCorner-.5,1,"rectangle",false,true);
        std::cout<<"ALL PASS\n";return 0;
    }
    catch(const std::exception& E){std::cerr<<"FAIL "<<E.what()<<"\n";return 1;}
}
