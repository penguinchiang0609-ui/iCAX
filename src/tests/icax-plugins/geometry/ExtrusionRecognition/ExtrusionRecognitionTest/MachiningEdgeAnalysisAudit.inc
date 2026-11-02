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
