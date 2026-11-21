#include <ExtrusionRecognition/MachiningEdgeAnalysisWork.h>
#include <ExtrusionRecognition/SectionFaceSubset.h>

namespace machining_edge_audit
{
R::SSectionWiresResult RectangleSection(const double angle = 0.0,
    const double yOffset = 0.0, const double zOffset = 0.0)
{
    R::SSectionWiresResult result;
    result.bSuccess = true;
    R::SSectionWire wire;
    wire.bClosed = true;
    std::vector<G::Point2> points;
    for (const auto point : { G::Point2{0,0}, G::Point2{10,0}, G::Point2{10,10}, G::Point2{0,10} })
        points.push_back({ yOffset + std::cos(angle) * point.X - std::sin(angle) * point.Y,
            zOffset + std::sin(angle) * point.X + std::cos(angle) * point.Y });
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        R::SSectionWireEdge edge;
        edge.Start = points[i]; edge.End = points[(i + 1) % points.size()];
        edge.Curve = G::Segment2{ edge.Start, edge.End };
        edge.Range = { 0.0, std::hypot(edge.End.X - edge.Start.X, edge.End.Y - edge.Start.Y) };
        wire.Edges.push_back(edge);
    }
    result.Wires.push_back(std::move(wire));
    return result;
}

R::SSectionFaceClassificationResult AllMachining(const TopoDS_Shape& shape)
{
    R::SSectionFaceClassificationResult result;
    result.bSuccess = true;
    result.SplitGeometry = O::ConvertOpenCascadeShapeToBRep(shape, "", "", 0.001);
    result.MachiningGeometry = result.SplitGeometry;
    for (const auto& face : result.SplitGeometry.Faces) result.MachiningFaceIds.push_back(face.Id);
    return result;
}

std::size_t ConvexityCount(const R::SMachiningEdgeAnalysisResult& result, const R::EEdgeConvexity value)
{
    return std::count_if(result.Edges.begin(), result.Edges.end(), [=](const auto& edge)
        { return edge.bConvexityValid && edge.Convexity == value; });
}

std::size_t SkinCount(const R::SMachiningEdgeAnalysisResult& result, const R::ESkinRelation value)
{
    return std::count_if(result.Edges.begin(), result.Edges.end(), [=](const auto& edge)
        { return edge.bSkinRelationValid && edge.SkinRelation == value; });
}

TopoDS_Face Polygon(const std::vector<gp_Pnt>& points)
{
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto& point : points) polygon.Add(point);
    polygon.Close();
    return BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
}

TopoDS_Shape DetachedFaces(const TopoDS_Shape& shape,const bool reverseLast=false)
{
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);
    std::vector<TopoDS_Shape> sourceFaces;
    for(TopExp_Explorer faces(shape,TopAbs_FACE);faces.More();faces.Next())sourceFaces.push_back(faces.Current());
    for(std::size_t i=0;i<sourceFaces.size();++i)
    {
        auto face=BRepBuilderAPI_Copy(sourceFaces[i],true,false).Shape();
        if(reverseLast&&i+1==sourceFaces.size())face.Reverse();
        builder.Add(result,face);
    }
    return result;
}

struct DetachedSkinFixture
{
    R::SSectionFaceClassificationResult Classification;
    std::uint64_t MachiningEdgeId=0,SkinEdgeId=0,SkinFaceId=0;
};

DetachedSkinFixture ApproximateDetachedSkin(const bool bowed=false,const double extraMachiningGap=0)
{
    const auto skinPlane=Polygon({{0,0,0},{10,0,0},{10,0,10},{0,0,10}});
    const auto skin=BRepBuilderAPI_NurbsConvert(skinPlane,true).Shape();
    TopoDS_Face machining;
    if(bowed)
    {
        TColgp_Array1OfPnt poles(1,3);poles(1)={0,0,0};poles(2)={0,2,5};poles(3)={0,0,10};
        BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(poles)).Edge());
        for(const auto& endpoints:{std::pair{gp_Pnt(0,0,10),gp_Pnt(0,4,10)},
            std::pair{gp_Pnt(0,4,10),gp_Pnt(0,4,0)},std::pair{gp_Pnt(0,4,0),gp_Pnt(0,0,0)}})
            wire.Add(BRepBuilderAPI_MakeEdge(endpoints.first,endpoints.second).Edge());
        machining=BRepBuilderAPI_MakeFace(wire.Wire()).Face();
    }
    else machining=Polygon({{0,0,0},{0,4,0},{0,4,10},{0,0,10}});
    BRep_Builder builder;TopoDS_Compound pair;builder.MakeCompound(pair);
    builder.Add(pair,machining);builder.Add(pair,skin);
    DetachedSkinFixture fixture;fixture.Classification=AllMachining(pair);
    auto& classification=fixture.Classification;
    classification.MachiningFaceIds={classification.SplitGeometry.Faces.front().Id};
    fixture.SkinFaceId=classification.SplitGeometry.Faces.back().Id;
    classification.NonMachiningFaceIds={fixture.SkinFaceId};
    const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
    if(!built.bOK||BRepAdaptor_Surface(built.Faces.at(fixture.SkinFaceId)).GetType()!=GeomAbs_BSplineSurface)
        throw std::runtime_error("Detached source skin fixture must retain its BSpline surface");
    for(auto& edge:classification.SplitGeometry.Edges)
    {
        BRepAdaptor_Curve native(built.Edges.at(edge.Id));
        const auto a=native.Value(native.FirstParameter()),b=native.Value(native.LastParameter());
        if(std::max(std::abs(a.X()),std::abs(b.X()))>1e-8
            ||std::max(std::abs(a.Y()),std::abs(b.Y()))>1e-8
            ||std::abs(std::abs(a.Z()-b.Z())-10)>1e-8)continue;
        bool machiningOwner=false;
        for(const auto& wire:classification.SplitGeometry.Wires)
            if(std::find(classification.SplitGeometry.Faces.front().WireIds.begin(),
                classification.SplitGeometry.Faces.front().WireIds.end(),wire.Id)
                !=classification.SplitGeometry.Faces.front().WireIds.end())
                for(const auto& use:wire.Coedges)machiningOwner=machiningOwner||use.EdgeId==edge.Id;
        if(machiningOwner)fixture.MachiningEdgeId=edge.Id;else fixture.SkinEdgeId=edge.Id;
        const double displacement=.006+(machiningOwner?extraMachiningGap:0);
        for(auto& curve:classification.SplitGeometry.Curves3)if(curve.Id==edge.Curve3Id)
        {
            if(auto line=std::get_if<G::Line3>(&curve.Geometry))line->Axis.Location.Y+=displacement;
            else if(auto spline=std::get_if<G::BSpline3>(&curve.Geometry))for(auto& pole:spline->Poles)pole.Y+=displacement;
            else if(auto nurbs=std::get_if<G::NURBS3>(&curve.Geometry))for(auto& pole:nurbs->Poles)pole.Y+=displacement;
            else if(auto bezier=std::get_if<G::Bezier3>(&curve.Geometry))for(auto& pole:bezier->Poles)pole.Y+=displacement;
            else throw std::runtime_error("Unsupported detached source fixture curve");
        }
        // Only the source 3D approximation changes. Both owners retain their
        // original pcurves; the BSpline source skin remains exactly at Y=0.
        edge.Tolerance=.01;
    }
    if(!fixture.MachiningEdgeId||!fixture.SkinEdgeId||fixture.MachiningEdgeId==fixture.SkinEdgeId)
        throw std::runtime_error("Detached source fixture must have two independent coedges");
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    return fixture;
}

DetachedSkinFixture RefinedNativeSplineSkin(const double extraMachiningGap)
{
    auto fixture=ApproximateDetachedSkin(false,extraMachiningGap);
    auto& classification=fixture.Classification;
    const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
    for(auto& edge:classification.SplitGeometry.Edges)
    {
        if(edge.Id!=fixture.MachiningEdgeId&&edge.Id!=fixture.SkinEdgeId)continue;
        double first=0.,last=0.;const auto support=BRep_Tool::Curve(built.Edges.at(edge.Id),first,last);
        const Handle(Geom_Curve) finite=new Geom_TrimmedCurve(support,first,last);
        auto spline=GeomConvert::CurveToBSplineCurve(finite);
        if(edge.Id==fixture.MachiningEdgeId)
        {
            spline->IncreaseDegree(3);
            spline->InsertKnot(first+(last-first)*.37,1,0.,false);
        }
        const auto record=O::ConvertOpenCascadeShapeToBRep(BRepBuilderAPI_MakeEdge(spline,first,last).Edge(),"","",.001);
        if(record.Curves3.size()!=1)throw std::runtime_error("Native spline certificate fixture must retain one unchanged curve reference");
        for(auto& curve:classification.SplitGeometry.Curves3)if(curve.Id==edge.Curve3Id)curve.Geometry=record.Curves3.front().Geometry;
    }
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    return fixture;
}
}

int MachiningEdgeAnalysisAudit(const fs::path& directory)
{
    using namespace machining_edge_audit;
    fs::create_directories(directory);
    VariantArray checks;
    const auto check = [&](const bool passed, const std::string& name)
    {
        checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});
        Save(directory / "checks.json", {{"checks",checks}});
        if (!passed) throw std::runtime_error(name);
    };
    try
    {
        R::MachiningEdgeAnalysisWork work;
        const auto section = RectangleSection();
        const auto box = BRepPrimAPI_MakeBox(10,10,10).Shape();
        const auto boxClass = AllMachining(box);
        const auto beforeSplit = G::Persistence::Serialize(boxClass.SplitGeometry);
        const auto beforeMachining = G::Persistence::Serialize(boxClass.MachiningGeometry);
        const auto boxResult = work.Run(boxClass, section);
        check(boxResult.bSuccess && boxResult.bComplete, "box edge analysis succeeds completely");
        check(boxResult.Edges.size() == 12 && ConvexityCount(boxResult,R::EEdgeConvexity::Convex)==12,
            "all twelve outward box edges are convex");
        check(SkinCount(boxResult,R::ESkinRelation::OnSkin)==12, "box boundary edges are all on skin");
        check(beforeSplit==G::Persistence::Serialize(boxClass.SplitGeometry)
            &&beforeMachining==G::Persistence::Serialize(boxClass.MachiningGeometry), "input CAD bytes are unchanged");
        check(std::all_of(boxResult.Edges.begin(),boxResult.Edges.end(),[](const auto& edge)
            {return edge.AngleSamples.size()==9;}), "dihedral uses multiple actual surface normals per edge");

        const auto invertedClass=AllMachining(box.Reversed());
        const auto invertedBytes=G::Persistence::Serialize(invertedClass.SplitGeometry);
        const auto invertedResult=work.Run(invertedClass,section);
        check(invertedResult.bSuccess&&ConvexityCount(invertedResult,R::EEdgeConvexity::Convex)==12
            &&std::any_of(invertedResult.Edges.begin(),invertedResult.Edges.end(),[](const auto& edge)
                {return edge.bOrientationRepaired;})
            &&invertedBytes==G::Persistence::Serialize(invertedClass.SplitGeometry),
            "closed solid material orientation anchors an inverted source without changing its CAD records");

        const auto pocket = BRepAlgoAPI_Cut(box,
            BRepPrimAPI_MakeBox(gp_Pnt(2,3,7),6,4,4).Shape()).Shape();
        const auto pocketClass = AllMachining(pocket);
        const auto pocketResult = work.Run(pocketClass,section);
        check(pocketResult.bSuccess && ConvexityCount(pocketResult,R::EEdgeConvexity::Concave)>0
            &&ConvexityCount(pocketResult,R::EEdgeConvexity::Convex)>0, "pocket separates concave and convex dihedrals");

        const double rotation=0.37;
        gp_Trsf rotate;rotate.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),rotation);
        gp_Trsf translate;translate.SetTranslation(gp_Vec(35,7,-4));
        const auto moved= BRepBuilderAPI_Transform(pocket,translate*rotate,true).Shape();
        const auto movedResult=work.Run(AllMachining(moved),RectangleSection(rotation,7,-4));
        check(movedResult.bSuccess && movedResult.Edges.size()==pocketResult.Edges.size()
            &&ConvexityCount(movedResult,R::EEdgeConvexity::Concave)==ConvexityCount(pocketResult,R::EEdgeConvexity::Concave)
            &&ConvexityCount(movedResult,R::EEdgeConvexity::Convex)==ConvexityCount(pocketResult,R::EEdgeConvexity::Convex),
            "rigid axial rotation and translation preserve material concavity");

        auto shuffled=pocketClass;
        std::reverse(shuffled.SplitGeometry.Faces.begin(),shuffled.SplitGeometry.Faces.end());
        std::reverse(shuffled.MachiningGeometry.Faces.begin(),shuffled.MachiningGeometry.Faces.end());
        const auto shuffledResult=work.Run(shuffled,section);
        check(shuffledResult.bSuccess&&shuffledResult.Edges.size()==pocketResult.Edges.size()
            &&std::equal(shuffledResult.Edges.begin(),shuffledResult.Edges.end(),pocketResult.Edges.begin(),[](const auto& a,const auto& b)
            {return a.EdgeId==b.EdgeId&&a.bConvexityValid==b.bConvexityValid&&a.Convexity==b.Convexity;}),
            "neutral face record traversal order does not change edge identities or signs");

        auto renamed=pocketClass;
        const auto idA=renamed.SplitGeometry.Faces[0].Id,idB=renamed.SplitGeometry.Faces[1].Id;
        const auto rename=[=](std::uint64_t& id){if(id==idA)id=idB;else if(id==idB)id=idA;};
        for(auto* model:{&renamed.SplitGeometry,&renamed.MachiningGeometry})
        {
            for(auto& face:model->Faces)rename(face.Id);
            for(auto& shell:model->Shells)for(auto& id:shell.FaceIds)rename(id);
            for(auto& id:model->RootFaceIds)rename(id);
            for(auto& ref:model->RootShapes)if(ref.Kind==G::EBRepShapeKind::Face)rename(ref.Id);
        }
        for(auto& id:renamed.MachiningFaceIds)rename(id);
        const auto renamedResult=work.Run(renamed,section);
        check(renamedResult.bSuccess&&renamedResult.Edges.size()==pocketResult.Edges.size()
            &&std::equal(renamedResult.Edges.begin(),renamedResult.Edges.end(),pocketResult.Edges.begin(),[](const auto& a,const auto& b)
            {return a.EdgeId==b.EdgeId&&a.bConvexityValid==b.bConvexityValid&&a.Convexity==b.Convexity;}),
            "swapping the first and second face identities preserves dihedral signs");

        BRepBuilderAPI_Sewing sewing(1e-7);
        sewing.Add(Polygon({{0,0,5},{5,0,5},{5,10,5},{0,10,5}}));
        sewing.Add(Polygon({{5,0,5},{10,0,5},{10,10,5},{5,10,5}}));
        sewing.Perform();
        const auto planeResult=work.Run(AllMachining(sewing.SewedShape()),section);
        check(planeResult.bSuccess&&ConvexityCount(planeResult,R::EEdgeConvexity::Coplanar)==1,
            "two connected planar faces have one coplanar shared edge");

        BRep_Builder detachedBuilder;TopoDS_Compound detachedPlanes;detachedBuilder.MakeCompound(detachedPlanes);
        detachedBuilder.Add(detachedPlanes,Polygon({{0,0,5},{5,0,5},{5,10,5},{0,10,5}}));
        detachedBuilder.Add(detachedPlanes,Polygon({{5,0,5},{10,0,5},{10,10,5},{5,10,5}}));
        const auto detachedClass=AllMachining(detachedPlanes);
        const auto detachedBefore=G::Persistence::Serialize(detachedClass.SplitGeometry);
        const auto detachedResult=work.Run(detachedClass,section);
        check(detachedResult.bSuccess&&ConvexityCount(detachedResult,R::EEdgeConvexity::Coplanar)==2
            &&std::count_if(detachedResult.Edges.begin(),detachedResult.Edges.end(),[](const auto& edge)
                {return edge.MatchedEdgeIds.size()==2&&edge.SourceMachiningFaceIds.size()==1&&edge.MachiningFaceIds.size()==2;})==2,
            "detached coplanar boundaries retain both source edge IDs and recover opposite-side adjacency");
        check(detachedBefore==G::Persistence::Serialize(detachedClass.SplitGeometry),
            "geometric adjacency matching leaves source CAD bytes unchanged");

        TopoDS_Compound reversedPlanes;detachedBuilder.MakeCompound(reversedPlanes);
        detachedBuilder.Add(reversedPlanes,Polygon({{0,0,5},{5,0,5},{5,10,5},{0,10,5}}));
        auto reversedSecond=Polygon({{5,0,5},{10,0,5},{10,10,5},{5,10,5}});reversedSecond.Reverse();
        detachedBuilder.Add(reversedPlanes,reversedSecond);
        const auto reversedPlaneResult=work.Run(AllMachining(reversedPlanes),section);
        check(reversedPlaneResult.bSuccess&&ConvexityCount(reversedPlaneResult,R::EEdgeConvexity::Coplanar)==2,
            "coplanar opposite-side interiors remain coplanar with an independently reversed face");

        const auto detachedBoxResult=work.Run(AllMachining(DetachedFaces(box)),section);
        check(detachedBoxResult.bSuccess&&detachedBoxResult.Edges.size()==24
            &&ConvexityCount(detachedBoxResult,R::EEdgeConvexity::Convex)==24,
            "detached convex box boundaries recover convexity using each face's own edge parameter");
        const auto repairedBoxClass=AllMachining(DetachedFaces(box,true));
        const auto repairedBoxBytes=G::Persistence::Serialize(repairedBoxClass.SplitGeometry);
        const auto repairedBoxResult=work.Run(repairedBoxClass,section);
        check(repairedBoxResult.bSuccess&&ConvexityCount(repairedBoxResult,R::EEdgeConvexity::Convex)==24
            &&std::count_if(repairedBoxResult.Edges.begin(),repairedBoxResult.Edges.end(),[](const auto& edge)
                {return edge.bOrientationRepaired&&!edge.OrientationFlippedFaceIds.empty();})==8
            &&repairedBoxBytes==G::Persistence::Serialize(repairedBoxClass.SplitGeometry),
            "an independently reversed detached face is repaired in local analysis and retains explicit evidence");
        const auto detachedPocketResult=work.Run(AllMachining(DetachedFaces(pocket)),section);
        check(detachedPocketResult.bSuccess
            &&ConvexityCount(detachedPocketResult,R::EEdgeConvexity::Concave)==2*ConvexityCount(pocketResult,R::EEdgeConvexity::Concave)
            &&ConvexityCount(detachedPocketResult,R::EEdgeConvexity::Convex)==2*ConvexityCount(pocketResult,R::EEdgeConvexity::Convex),
            "detached concave pocket boundaries recover the original material dihedral signs");

        TopoDS_Compound overlap;detachedBuilder.MakeCompound(overlap);
        detachedBuilder.Add(overlap,Polygon({{0,0,5},{7,0,5},{7,10,5},{0,10,5}}));
        detachedBuilder.Add(overlap,Polygon({{3,0,5},{10,0,5},{10,10,5},{3,10,5}}));
        const auto overlapResult=work.Run(AllMachining(overlap),section);
        check(overlapResult.bSuccess&&ConvexityCount(overlapResult,R::EEdgeConvexity::Free)==8
            &&std::any_of(overlapResult.Edges.begin(),overlapResult.Edges.end(),[](const auto& edge)
                {return !edge.RejectedCoincidentEdgeIds.empty();}),
            "same-side overlapping coplanar faces are not accepted as adjacent faces");

        TopoDS_Compound vertexContact;detachedBuilder.MakeCompound(vertexContact);
        detachedBuilder.Add(vertexContact,Polygon({{0,0,5},{5,0,5},{5,5,5},{0,5,5}}));
        detachedBuilder.Add(vertexContact,Polygon({{5,5,5},{10,5,5},{10,10,5},{5,10,5}}));
        const auto vertexResult=work.Run(AllMachining(vertexContact),section);
        check(vertexResult.bSuccess&&ConvexityCount(vertexResult,R::EEdgeConvexity::Free)==8
            &&std::all_of(vertexResult.Edges.begin(),vertexResult.Edges.end(),[](const auto& edge)
                {return edge.MatchedEdgeIds.size()==1&&edge.PartialOverlapEdgeIds.empty();}),
            "pure vertex contact does not create geometric edge adjacency");

        TopoDS_Compound fragments;detachedBuilder.MakeCompound(fragments);
        detachedBuilder.Add(fragments,Polygon({{0,0,5},{5,0,5},{5,10,5},{0,10,5}}));
        detachedBuilder.Add(fragments,Polygon({{5,0,5},{10,0,5},{10,10,5},{5,10,5},{5,5,5}}));
        const auto fragmentResult=work.Run(AllMachining(fragments),section);
        check(fragmentResult.bSuccess&&fragmentResult.bComplete
            &&ConvexityCount(fragmentResult,R::EEdgeConvexity::Coplanar)==4
            &&std::all_of(fragmentResult.Edges.begin(),fragmentResult.Edges.end(),[](const auto& edge)
                {return edge.bSkinRelationValid&&edge.bConvexityValid&&!edge.Key.empty()
                    &&edge.LastParameter>edge.FirstParameter;}),
            "one long edge versus two fragments is partitioned into four valid coplanar source spans");

        TopoDS_Compound shortSource;detachedBuilder.MakeCompound(shortSource);
        detachedBuilder.Add(shortSource,Polygon({{0,0,5},{5,0,5},{5,10,5},{0,10,5}}));
        detachedBuilder.Add(shortSource,Polygon({{5,0,5},{10,0,5},{10,10,5},{5,10,5},{5,.0008,5}}));
        const auto shortSourceResult=work.Run(AllMachining(shortSource),section);
        const auto inheritedCount=std::count_if(shortSourceResult.Edges.begin(),shortSourceResult.Edges.end(),[](const auto& edge)
            {return edge.SkinRelation==R::ESkinRelation::OnSkin&&edge.Convexity==R::EEdgeConvexity::Coplanar
                &&edge.SkinReason.find("whole-source-edge-skin-relation-inherited-from-")==0;});
        check(shortSourceResult.bSuccess&&shortSourceResult.bComplete&&inheritedCount==1
            &&std::count_if(shortSourceResult.Edges.begin(),shortSourceResult.Edges.end(),[](const auto& edge)
                {return edge.Convexity==R::EEdgeConvexity::Coplanar&&edge.SkinRelation==R::ESkinRelation::OnSkin;})==2
            &&std::count_if(shortSourceResult.Edges.begin(),shortSourceResult.Edges.end(),[](const auto& edge)
                {return edge.Convexity==R::EEdgeConvexity::Coplanar&&edge.SkinRelation==R::ESkinRelation::PartialOnSkin;})==2,
            "a complete short OnSkin source edge transfers only its coincident span relationship to a longer mixed parent");

        const auto lowerMaterial=BRepPrimAPI_MakeBox(10,8,2).Shape();
        const auto upperDetached=DetachedFaces(BRepPrimAPI_MakeBox(gp_Pnt(0,0,2),10,10,2).Shape());
        TopoDS_Compound touchingMaterial;detachedBuilder.MakeCompound(touchingMaterial);
        detachedBuilder.Add(touchingMaterial,lowerMaterial);detachedBuilder.Add(touchingMaterial,upperDetached);
        auto touchingClass=AllMachining(touchingMaterial);
        const auto touchingBuilt=O::ReconstructOpenCascadeShape(touchingClass.SplitGeometry);
        touchingClass.MachiningFaceIds.clear();touchingClass.NonMachiningFaceIds.clear();
        for(const auto& face:touchingClass.SplitGeometry.Faces)
        {
            auto native=touchingBuilt.Faces.at(face.Id);native.Orientation(face.Orientation==G::ETopologyOrientation::Reversed?TopAbs_REVERSED:TopAbs_FORWARD);
            BRepAdaptor_Surface support(native);Bnd_Box bounds;BRepBndLib::AddOptimal(native,bounds,false,false);
            double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);gp_Vec normal(support.Plane().Axis().Direction());
            if(native.Orientation()==TopAbs_REVERSED)normal.Reverse();
            const bool machining=(std::abs(x-10)<1e-8&&std::abs(X-10)<1e-8)
                ||(std::abs(z-2)<1e-8&&std::abs(Z-2)<1e-8&&normal.Z()<-.99);
            (machining?touchingClass.MachiningFaceIds:touchingClass.NonMachiningFaceIds).push_back(face.Id);
        }
        touchingClass.MachiningGeometry=R::detail::SectionFaceSubset(touchingClass.SplitGeometry,touchingClass.MachiningFaceIds);
        touchingClass.NonMachiningGeometry=R::detail::SectionFaceSubset(touchingClass.SplitGeometry,touchingClass.NonMachiningFaceIds);
        const auto touchingBytes=G::Persistence::Serialize(touchingClass.SplitGeometry);
        const auto touchingResult=work.Run(touchingClass,section);
        check(touchingResult.bSuccess&&touchingResult.bComplete&&ConvexityCount(touchingResult,R::EEdgeConvexity::Convex)==2
            &&std::any_of(touchingResult.Edges.begin(),touchingResult.Edges.end(),[](const auto& edge)
                {return edge.Convexity==R::EEdgeConvexity::Free
                    &&edge.AdjacencyReason.find("preserved-complete-native-material-edge-pair")==0
                    &&edge.SourceMachiningFaceIds.size()==1&&edge.MachiningFaceIds.size()==1;})
            &&touchingBytes==G::Persistence::Serialize(touchingClass.SplitGeometry),
            "native machining-to-stock edge ownership prevents a false three-face T junction across touching material boundaries");

        TopoDS_Compound gap;detachedBuilder.MakeCompound(gap);
        detachedBuilder.Add(gap,Polygon({{0,0,5},{5,0,5},{5,10,5},{0,10,5}}));
        detachedBuilder.Add(gap,Polygon({{5.00001,0,5},{10,0,5},{10,10,5},{5.00001,10,5}}));
        const auto gapResult=work.Run(AllMachining(gap),section);
        check(gapResult.bSuccess&&ConvexityCount(gapResult,R::EEdgeConvexity::Free)==8
            &&std::all_of(gapResult.Edges.begin(),gapResult.Edges.end(),[](const auto& edge)
                {return edge.MatchedEdgeIds.size()==1&&edge.PartialOverlapEdgeIds.empty();}),
            "a detached gap above 1e-6 is not silently sewn or accepted as shared adjacency");

        Handle(Geom_CylindricalSurface) curvedSupport=new Geom_CylindricalSurface(
            gp_Ax3(gp_Pnt(0,0,0),gp_Dir(1,0,0)),5);
        TopoDS_Compound smooth;detachedBuilder.MakeCompound(smooth);
        detachedBuilder.Add(smooth,BRepBuilderAPI_MakeFace(curvedSupport,0,1.2,0,10,1e-7).Face());
        detachedBuilder.Add(smooth,BRepBuilderAPI_MakeFace(curvedSupport,1.2,2.4,0,10,1e-7).Face());
        const auto smoothResult=work.Run(AllMachining(smooth),section);
        check(smoothResult.bSuccess&&ConvexityCount(smoothResult,R::EEdgeConvexity::Coplanar)==2,
            "zero local dihedral on smoothly connected curved supports is Coplanar for this edge analysis");

        const auto knifeFixture=[&](const bool reverseMaterial,const bool swapFaces)
        {
            Handle(Geom_CylindricalSurface) support=new Geom_CylindricalSurface(
                gp_Ax3(gp_Pnt(-3,0,0),gp_Dir(0,1,0),gp_Dir(1,0,0)),3);
            auto curved=BRepBuilderAPI_MakeFace(support,-1.5707963267948966,0,0,3,1e-7).Face();
            curved.Reverse();
            auto flat=BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0))),0,3,0,2).Face();
            if(reverseMaterial){curved.Reverse();flat.Reverse();}
            TopoDS_Compound pair;detachedBuilder.MakeCompound(pair);
            if(swapFaces){detachedBuilder.Add(pair,curved);detachedBuilder.Add(pair,flat);}
            else {detachedBuilder.Add(pair,flat);detachedBuilder.Add(pair,curved);}
            return AllMachining(pair);
        };
        bool convexKnifePassed=true,concaveKnifePassed=true;
        for(const bool reverseMaterial:{false,true})for(const bool swapFaces:{false,true})
        {
            const auto fixture=knifeFixture(reverseMaterial,swapFaces);
            const auto sourceBytes=G::Persistence::Serialize(fixture.SplitGeometry);
            const auto analyzed=work.Run(fixture,section);
            const auto expected=reverseMaterial?R::EEdgeConvexity::Concave:R::EEdgeConvexity::Convex;
            const bool passed=analyzed.bSuccess&&ConvexityCount(analyzed,expected)==2
                &&std::count_if(analyzed.Edges.begin(),analyzed.Edges.end(),[](const auto& edge)
                    {return edge.ConvexityReason.find("opposed-normal-second-order-source-material-limit")==0;})==2
                &&sourceBytes==G::Persistence::Serialize(fixture.SplitGeometry);
            if(reverseMaterial)concaveKnifePassed=concaveKnifePassed&&passed;
            else convexKnifePassed=convexKnifePassed&&passed;
        }
        check(convexKnifePassed,"opposed plane-cylinder normals resolve a convex knife edge by the material-side second-order limit independently of face order");
        check(concaveKnifePassed,"reversing the open source material normals gives the complementary concave knife limit independently of face order");

        auto oneFace=boxClass;
        oneFace.MachiningFaceIds={oneFace.SplitGeometry.Faces.front().Id};
        oneFace.NonMachiningFaceIds.clear();
        for(const auto& face:oneFace.SplitGeometry.Faces)if(face.Id!=oneFace.MachiningFaceIds.front())oneFace.NonMachiningFaceIds.push_back(face.Id);
        oneFace.MachiningGeometry=R::detail::SectionFaceSubset(oneFace.SplitGeometry,oneFace.MachiningFaceIds);
        oneFace.NonMachiningGeometry=R::detail::SectionFaceSubset(oneFace.SplitGeometry,oneFace.NonMachiningFaceIds);
        const auto oneResult=work.Run(oneFace,section);
        check(oneResult.bSuccess&&oneResult.Edges.size()==4
            &&ConvexityCount(oneResult,R::EEdgeConvexity::Free)==4,
            "non-machining neighbors are excluded from convexity adjacency");

        auto approximate=oneFace;
        std::uint64_t approximateEdge=0;
        for(const auto& edge:approximate.MachiningGeometry.Edges)
            for(auto& curve:approximate.SplitGeometry.Curves3)if(curve.Id==edge.Curve3Id)
                if(auto line=std::get_if<G::Line3>(&curve.Geometry);line&&std::abs(line->Axis.Direction.Z)>.99
                    &&std::abs(line->Axis.Location.Y)<1e-9)
                {
                    line->Axis.Location.Y+=.006;
                    approximateEdge=edge.Id;
                    break;
                }
        check(approximateEdge!=0,"source membership fixture identifies a shared skin line");
        for(auto& edge:approximate.SplitGeometry.Edges)if(edge.Id==approximateEdge)edge.Tolerance=.01;
        approximate.MachiningGeometry=R::detail::SectionFaceSubset(approximate.SplitGeometry,approximate.MachiningFaceIds);
        approximate.NonMachiningGeometry=R::detail::SectionFaceSubset(approximate.SplitGeometry,approximate.NonMachiningFaceIds);
        const auto approximateResult=work.Run(approximate,section);
        check(approximateResult.bSuccess&&std::any_of(approximateResult.Edges.begin(),approximateResult.Edges.end(),[=](const auto& edge)
            {return edge.EdgeId==approximateEdge&&edge.bSkinRelationValid&&edge.SkinRelation==R::ESkinRelation::OnSkin
                &&edge.bSourceSkinMembership&&edge.MaxSkinDistance>.005;}),
            "shared source skin attribution retains the original approximate deviation");

        const auto detachedSkin=ApproximateDetachedSkin();
        const auto detachedBytes=G::Persistence::Serialize(detachedSkin.Classification.SplitGeometry);
        const auto detachedSkinResult=work.Run(detachedSkin.Classification,section);
        check(detachedSkinResult.bSuccess&&std::any_of(detachedSkinResult.Edges.begin(),detachedSkinResult.Edges.end(),[&](const auto& edge)
            {return edge.EdgeId==detachedSkin.MachiningEdgeId&&edge.bSkinRelationValid
                &&edge.SkinRelation==R::ESkinRelation::OnSkin&&edge.bSourceSkinMembership
                &&std::find(edge.SourceSkinFaceIds.begin(),edge.SourceSkinFaceIds.end(),detachedSkin.SkinFaceId)!=edge.SourceSkinFaceIds.end()
                &&edge.MaxSkinDistance>.005;})
            &&detachedBytes==G::Persistence::Serialize(detachedSkin.Classification.SplitGeometry),
            "detached coincident source coedges inherit BSpline skin ownership while retaining 0.006 mm approximation and unchanged CAD");

        const auto bowedSkin=ApproximateDetachedSkin(true);
        const auto bowedSkinResult=work.Run(bowedSkin.Classification,section);
        check(bowedSkinResult.bSuccess&&std::any_of(bowedSkinResult.Edges.begin(),bowedSkinResult.Edges.end(),[&](const auto& edge)
            {return edge.EdgeId==bowedSkin.MachiningEdgeId&&edge.MaxSkinDistance>.5;})
            &&std::all_of(bowedSkinResult.Edges.begin(),bowedSkinResult.Edges.end(),[&](const auto& edge)
            {return edge.EdgeId!=bowedSkin.MachiningEdgeId
                ||(edge.SkinRelation!=R::ESkinRelation::OnSkin&&!edge.bSourceSkinMembership);}),
            "matching detached skin endpoints do not attribute a curve that bows into the wall interior");

        for(const double gap:{0.,.0005,.02})
        {
            const auto fixture=RefinedNativeSplineSkin(gap);
            const auto original=G::Persistence::Serialize(fixture.Classification.SplitGeometry);
            const auto analyzed=work.Run(fixture.Classification,section);
            bool membership=false,certificate=false,retainedDeviation=false;
            for(const auto& edge:analyzed.Edges)if(edge.EdgeId==fixture.MachiningEdgeId)
            {
                membership=membership||edge.bSourceSkinMembership;
                certificate=certificate||(edge.SkinRelation==R::ESkinRelation::OnSkin
                    &&edge.SkinReason.find("source-skin-native-bspline-whole-domain-certificate")!=std::string::npos
                    &&edge.SkinReason.find("certificate-bound-mm=")!=std::string::npos
                    &&edge.SkinReason.find("accepted-declared-tolerance-mm=")!=std::string::npos);
                retainedDeviation=retainedDeviation||edge.MaxSkinDistance>.005;
            }
            check(analyzed.bSuccess&&original==G::Persistence::Serialize(fixture.Classification.SplitGeometry),
                "native whole-domain skin certificate preserves original CAD at gap "+std::to_string(gap));
            check(gap<=.01?(membership&&certificate&&retainedDeviation):!membership,
                gap==0.?"exact degree elevation and knot subdivision retain detached skin ownership":
                gap<.01?"whole-domain native spline bound accepts declared CAD approximation and retains ideal deviation":
                    "native spline correspondence exceeding either declared edge tolerance is rejected");
        }

        const auto nearSkin=ApproximateDetachedSkin(false,.0005);
        const auto nearSkinResult=work.Run(nearSkin.Classification,section);
        check(nearSkinResult.bSuccess&&std::any_of(nearSkinResult.Edges.begin(),nearSkinResult.Edges.end(),[&](const auto& edge)
            {return edge.EdgeId==nearSkin.MachiningEdgeId&&edge.MaxSkinDistance>.006
                &&edge.SkinRelation==R::ESkinRelation::OnSkin&&edge.bSourceSkinMembership
                &&edge.SkinReason.find("native-line-original-parameter-degree-one")!=std::string::npos;}),
            "native line whole-domain provenance accepts a 0.0005 mm correspondence within both original 0.01 mm edge tolerances and retains ideal-skin deviation");
        const auto distantLine=ApproximateDetachedSkin(false,.02);
        const auto distantLineResult=work.Run(distantLine.Classification,section);
        check(distantLineResult.bSuccess&&std::all_of(distantLineResult.Edges.begin(),distantLineResult.Edges.end(),[&](const auto& edge)
            {return edge.EdgeId!=distantLine.MachiningEdgeId||!edge.bSourceSkinMembership;}),
            "native line provenance rejects a 0.02 mm correspondence exceeding either original 0.01 mm edge tolerance");
        auto strictTargetLine=ApproximateDetachedSkin(false,.0005);
        for(auto& edge:strictTargetLine.Classification.SplitGeometry.Edges)
            if(edge.Id==strictTargetLine.MachiningEdgeId)edge.Tolerance=1e-7;
        strictTargetLine.Classification.MachiningGeometry=R::detail::SectionFaceSubset(
            strictTargetLine.Classification.SplitGeometry,strictTargetLine.Classification.MachiningFaceIds);
        const auto strictTargetLineResult=work.Run(strictTargetLine.Classification,section);
        check(strictTargetLineResult.bSuccess&&std::all_of(strictTargetLineResult.Edges.begin(),strictTargetLineResult.Edges.end(),[&](const auto& edge)
            {return edge.EdgeId!=strictTargetLine.MachiningEdgeId||!edge.bSourceSkinMembership;}),
            "native line provenance uses the stricter original target tolerance instead of the looser 0.01 mm witness tolerance");

        const auto triangle=Polygon({{0,0,0},{0,5,5},{0,10,0}});
        const auto triangleResult=work.Run(AllMachining(triangle),section);
        check(triangleResult.bSuccess&&SkinCount(triangleResult,R::ESkinRelation::PartialOnSkin)==2
            &&SkinCount(triangleResult,R::ESkinRelation::OnSkin)==1,
            "endpoint-only skin contact gives PartialOnSkin and whole-skin has priority");
        const auto innerResult=work.Run(AllMachining(Polygon({{0,2,2},{0,5,2},{0,3,4}})),section);
        check(innerResult.bSuccess&&SkinCount(innerResult,R::ESkinRelation::OffSkin)==3,
            "edges with no skin contact are OffSkin");

        TColgp_Array1OfPnt poles(1,3);poles(1)={0,2,2};poles(2)={0,-2,5};poles(3)={0,2,8};
        Handle(Geom_BezierCurve) curved=new Geom_BezierCurve(poles);
        BRepBuilderAPI_MakeWire touchWire;
        touchWire.Add(BRepBuilderAPI_MakeEdge(curved).Edge());
        touchWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,2,8),gp_Pnt(0,5,5)).Edge());
        touchWire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,5,5),gp_Pnt(0,2,2)).Edge());
        const auto touchResult=work.Run(AllMachining(BRepBuilderAPI_MakeFace(touchWire.Wire()).Face()),section);
        check(touchResult.bSuccess&&touchResult.bComplete
            &&SkinCount(touchResult,R::ESkinRelation::PartialOnSkin)==2
            &&SkinCount(touchResult,R::ESkinRelation::OffSkin)==2
            &&std::count_if(touchResult.Edges.begin(),touchResult.Edges.end(),[](const auto& edge)
                {return std::abs(edge.FirstParameter-.5)<1e-7||std::abs(edge.LastParameter-.5)<1e-7;})==2,
            "interior-only skin tangency creates two Partial spans at the actual contact parameter");

        const auto tinySideResult=work.Run(AllMachining(Polygon({{0,0,5},{0,.005,5},{1,.005,6}})),section);
        check(tinySideResult.bSuccess&&tinySideResult.bComplete&&tinySideResult.Edges.size()==3
            &&SkinCount(tinySideResult,R::ESkinRelation::PartialOnSkin)==2
            &&SkinCount(tinySideResult,R::ESkinRelation::OnSkin)==0,
            "a 0.005 mm endpoint side remains whole Partial without tolerance-generated Skin slivers");

        const auto finiteSkinClass=AllMachining(Polygon({{0,0,-1},{0,0,11},{0,4,5}}));
        const auto finiteSkinResult=work.Run(finiteSkinClass,section);
        std::map<std::uint64_t,std::vector<const R::SMachiningEdgeAnalysis*>> finiteBySource;
        for(const auto& edge:finiteSkinResult.Edges)finiteBySource[edge.EdgeId].push_back(&edge);
        bool foundFiniteSkin=false;
        for(const auto& [edgeId,spans]:finiteBySource)if(spans.size()==3)
            foundFiniteSkin=foundFiniteSkin||(spans[0]->SkinRelation==R::ESkinRelation::PartialOnSkin
                &&spans[1]->SkinRelation==R::ESkinRelation::OnSkin
                &&spans[2]->SkinRelation==R::ESkinRelation::PartialOnSkin
                &&std::abs(spans[0]->LastParameter-spans[0]->FirstParameter-1)<1e-6
                &&std::abs(spans[1]->LastParameter-spans[1]->FirstParameter-10)<1e-6
                &&std::abs(spans[2]->LastParameter-spans[2]->FirstParameter-1)<1e-6);
        check(finiteSkinResult.bSuccess&&finiteSkinResult.bComplete&&foundFiniteSkin,
            "a line overlapping only a finite skin interval is partitioned Partial-Skin-Partial at geometric endpoints");

        const auto cylinderResult=work.Run(AllMachining(BRepPrimAPI_MakeCylinder(2,5).Shape()),section);
        check(cylinderResult.bSuccess&&cylinderResult.bComplete
            &&std::any_of(cylinderResult.ExcludedEdges.begin(),cylinderResult.ExcludedEdges.end(),[](const auto& edge)
                {return edge.ConvexityReason=="parametric-same-face-seam";})
            &&std::all_of(cylinderResult.Edges.begin(),cylinderResult.Edges.end(),[](const auto& edge)
                {return edge.bSkinRelationValid&&edge.bConvexityValid;}),
            "a parametric same-face seam stays in source topology and is excluded from physical edge enum pairs");

        // A periodic inner wall owns a reversed material face but its stored
        // wire/coedges still belong to the unflipped support frame. Mixing
        // those frames makes WireExplorer stop at the twice-used seam.
        TopoDS_Face reversedCylinder;
        const auto cylinderShape=BRepPrimAPI_MakeCylinder(2,5).Shape();
        for(TopExp_Explorer it(cylinderShape,TopAbs_FACE);it.More();it.Next())
            if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder)
                reversedCylinder=TopoDS::Face(it.Current().Oriented(TopAbs_REVERSED));
        const auto reversedCylinderClass=AllMachining(reversedCylinder);
        const auto cylinderBytes=G::Persistence::Serialize(reversedCylinderClass.SplitGeometry);
        const auto reversedCylinderResult=work.Run(reversedCylinderClass,section);
        std::multiset<std::pair<std::uint64_t,bool>> originalCylinderUses,analyzedCylinderUses;
        for(const auto& wire:reversedCylinderClass.SplitGeometry.Wires)
            for(const auto& use:wire.Coedges)
                originalCylinderUses.emplace(use.EdgeId,use.Orientation==G::ETopologyOrientation::Reversed);
        bool cylinderWiresOrdered=true;
        for(const auto& face:reversedCylinderResult.Faces)for(const auto& wire:face.Wires)
        {
            cylinderWiresOrdered=cylinderWiresOrdered&&wire.bOrdered;
            for(const auto& group:wire.Groups)for(const auto& use:group.SourceUses)
                analyzedCylinderUses.emplace(use.EdgeId,use.bReversed);
        }
        check(!reversedCylinder.IsNull()&&reversedCylinderClass.SplitGeometry.Faces.front().Orientation==G::ETopologyOrientation::Reversed
            &&reversedCylinderResult.bSuccess&&reversedCylinderResult.bComplete&&cylinderWiresOrdered
            &&originalCylinderUses.size()==4&&analyzedCylinderUses==originalCylinderUses
            &&std::count_if(reversedCylinderResult.ExcludedEdges.begin(),reversedCylinderResult.ExcludedEdges.end(),[](const auto& edge)
                {return edge.ConvexityReason=="parametric-same-face-seam";})==1
            &&cylinderBytes==G::Persistence::Serialize(reversedCylinderClass.SplitGeometry),
            "reversed periodic cylinder keeps all four support-oriented coedges including both seam uses without changing CAD");

        const auto shared=BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(10,0,0)).Edge();
        BRep_Builder nonManifoldBuilder;TopoDS_Compound nonManifold;nonManifoldBuilder.MakeCompound(nonManifold);
        for(const auto point:{gp_Pnt(0,5,0),gp_Pnt(0,0,5),gp_Pnt(0,-5,5)})
        {
            BRepBuilderAPI_MakeWire wire;wire.Add(shared);
            wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(10,0,0),point).Edge());
            wire.Add(BRepBuilderAPI_MakeEdge(point,gp_Pnt(0,0,0)).Edge());
            nonManifoldBuilder.Add(nonManifold,BRepBuilderAPI_MakeFace(wire.Wire()).Face());
        }
        const auto nonManifoldResult=work.Run(AllMachining(nonManifold),section);
        check(!nonManifoldResult.bSuccess
            &&std::any_of(nonManifoldResult.Diagnostics.begin(),nonManifoldResult.Diagnostics.end(),[](const auto& text)
                {return text.find("non-manifold")!=std::string::npos;}),
            "genuinely three incident source faces reject the analysis input instead of publishing an undefined edge enum");

        R::SMachiningEdgeAnalysisOptions invalid;invalid.dSkinDistanceTolerance=-1;
        check(!work.Run(boxClass,section,invalid).bSuccess,"negative tolerance is rejected");
        invalid={};invalid.nCurveSampleCount=2;
        check(!work.Run(boxClass,section,invalid).bSuccess,"insufficient interior samples are rejected");
        invalid={};invalid.dAngularToleranceRadians=std::numeric_limits<double>::quiet_NaN();
        check(!work.Run(boxClass,section,invalid).bSuccess,"non-finite angular tolerance is rejected");

        std::size_t expectedUses=0,actualUses=0;
        for(const auto& wire:boxClass.MachiningGeometry.Wires)expectedUses+=wire.Coedges.size();
        for(const auto& face:boxResult.Faces)for(const auto& wire:face.Wires)for(const auto& group:wire.Groups)actualUses+=group.SourceUses.size();
        check(expectedUses==actualUses,"geometry grouping preserves every original coedge use");
        Save(directory/"checks.json",{{"status",std::string("passed")},{"checks",checks}});
        return 0;
    }
    catch(const std::exception& e)
    {
        Save(directory/"checks.json",{{"status",std::string("failed")},{"error",std::string(e.what())},{"checks",checks}});
        return 1;
    }
}
