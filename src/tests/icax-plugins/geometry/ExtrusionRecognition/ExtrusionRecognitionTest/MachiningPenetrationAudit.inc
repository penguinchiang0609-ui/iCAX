namespace machining_penetration_audit
{
using R::EPenetrationExit;
using R::EPenetrationKind;
using FaceSelector=std::function<bool(const TopoDS_Face&)>;

struct Fixture final
{
    std::string Name;
    TopoDS_Shape Shape;
    R::SSectionWiresResult Section;
    FaceSelector IsMachining;
    EPenetrationKind ExpectedKind;
    std::optional<gp_Pnt> RequiredWitness;
    EPenetrationExit WitnessExit=EPenetrationExit::None;
    std::optional<EPenetrationExit> RequiredExit;
    int MinimumTargetUses=1;
};

TopoDS_Shape Subtract(const TopoDS_Shape& stock,const TopoDS_Shape& tool)
{
    BRepAlgoAPI_Cut cut(stock,tool);
    if(!cut.IsDone()||cut.Shape().IsNull()||!BRepCheck_Analyzer(cut.Shape()).IsValid())
        throw std::runtime_error("Cannot create a valid cut fixture solid");
    return cut.Shape();
}

bool AtHeight(const BRepAdaptor_Surface& surface,double height)
{
    return surface.GetType()==GeomAbs_Plane&&
        std::abs(surface.Plane().Axis().Direction().Z())>1.-Precision::Angular()&&
        std::abs(surface.Plane().Location().Z()-height)<=Precision::Confusion();
}

Fixture Bore(bool through, bool roundedCorner = false)
{
    const double floor=5.;
    const double first=through?-1.:floor;
    const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(5.,5.,first),gp_Dir(0.,0.,1.)),2.,11.-first).Shape();
    auto stock=BRepPrimAPI_MakeBox(10.,10.,10.).Shape();
    auto section=machining_path_audit::RectangleSection();
    if(roundedCorner)
    {
        const std::vector<G::Point2> points{{1,0},{10,0},{10,10},{0,10},{0,1}};
        section=machining_path_audit::PolygonSection(points);
        auto& wire=section.Wires.front();
        const double pi=machining_path_audit::Pi;
        wire.dSignedArea=99.+pi/4.;
        auto& arc=wire.Edges.back();
        arc.Curve=G::Arc2{{{{1,1},{1,0},{0,1}},1},pi,1.5*pi,true};
        arc.Range={pi,1.5*pi};
        const auto point=[](const G::Point2& p){return gp_Pnt(0.,p.X,p.Y);};
        BRepBuilderAPI_MakeWire profile;
        for(std::size_t i=1;i<points.size();++i)
            profile.Add(BRepBuilderAPI_MakeEdge(point(points[i-1]),point(points[i])).Edge());
        const double mid=1.-std::sqrt(0.5);
        const auto corner=GC_MakeArcOfCircle(point(points.back()),gp_Pnt(0.,mid,mid),point(points.front())).Value();
        profile.Add(BRepBuilderAPI_MakeEdge(corner).Edge());
        stock=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(profile.Wire(),true).Face(),gp_Vec(10.,0.,0.)).Shape();
    }
    const std::string name=roundedCorner?"through-bore-with-remote-stock-arc":through?"through-bore":"blind-bore";
    return {name,Subtract(stock,bore),std::move(section),
        [through,floor](const TopoDS_Face& face){
            const BRepAdaptor_Surface surface(face);
            const bool boreWall=surface.GetType()==GeomAbs_Cylinder&&
                surface.Cylinder().Axis().Direction().IsParallel(gp_Dir(0.,0.,1.),Precision::Angular());
            return boreWall||(!through&&AtHeight(surface,floor));},
        through?EPenetrationKind::Through:EPenetrationKind::Blind};
}

Fixture EndCut(bool tongue)
{
    const double cutX=10.;
    const double floor=tongue?7.:5.;
    const std::vector<G::Point2> points=tongue
        ?std::vector<G::Point2>{{0,7},{4,7},{4,0},{6,0},{6,7},{10,7},{10,10},{0,10}}
        :std::vector<G::Point2>{{0,0},{10,0},{10,10},{0,10}};
    std::vector<gp_Pnt> outline;
    for(const auto& point:points)outline.emplace_back(0.,point.X,point.Y);
    const auto stock=BRepPrimAPI_MakePrism(machining_path_audit::PolygonFace(outline),gp_Vec(13.,0.,0.)).Shape();
    const auto tool=BRepPrimAPI_MakeBox(gp_Pnt(cutX,-1.,floor),4.,12.,11.-floor).Shape();
    return {tongue?"tee-web-tongue":"rectangular-half-step",Subtract(stock,tool),
        machining_path_audit::PolygonSection(points),
        [cutX,floor](const TopoDS_Face& face){
            const BRepAdaptor_Surface surface(face);
            if(surface.GetType()!=GeomAbs_Plane)return false;
            const auto plane=surface.Plane();
            if(std::abs(plane.Axis().Direction().X())>1.-Precision::Angular())
                return std::abs(plane.Location().X()-cutX)<=Precision::Confusion();
            if(!AtHeight(surface,floor))return false;
            // The original flange underside survives before the cut at the
            // same height as the new top of the retained material.
            Bnd_Box bounds;
            BRepBndLib::AddOptimal(face,bounds,false,false);
            return bounds.CornerMax().X()>cutX+Precision::Confusion();
        },
        tongue?EPenetrationKind::Through:EPenetrationKind::Blind,
        gp_Pnt(cutX,5.,10.),tongue?EPenetrationExit::ContinuedStockSkin:EPenetrationExit::StockSkin};
}

Fixture AnnularEndCut()
{
    using namespace machining_path_audit;
    const double outerRadius=5.,innerRadius=3.,length=10.;
    const gp_Ax2 outerAxis(gp_Pnt(0.,5.,5.),gp_Dir(1.,0.,0.));
    const gp_Ax2 innerAxis(gp_Pnt(-1.,5.,5.),outerAxis.Direction());
    const auto stock=BRepPrimAPI_MakeCylinder(outerAxis,outerRadius,length).Shape();
    const auto cavity=BRepPrimAPI_MakeCylinder(innerAxis,innerRadius,length+2.).Shape();
    auto section=CircleSection(outerRadius);
    section.Wires.front().dSignedArea=Pi*outerRadius*outerRadius;
    auto hole=CircleSection(innerRadius).Wires.front();
    hole.bInner=true;
    hole.dSignedArea=-Pi*innerRadius*innerRadius;
    hole.Edges.front().bReversed=true;
    section.Wires.push_back(std::move(hole));
    return {"full-annular-end-cut",Subtract(stock,cavity),std::move(section),
        [](const TopoDS_Face& face){
            const BRepAdaptor_Surface surface(face);
            return surface.GetType()==GeomAbs_Plane&&
                surface.Plane().Axis().Direction().IsParallel(gp_Dir(1.,0.,0.),Precision::Angular());
        },EPenetrationKind::Through,{},EPenetrationExit::None,EPenetrationExit::FullSectionTermination,4};
}

bool PointOnInterval(const BRepAdaptor_Curve& curve,double first,double last,const gp_Pnt& witness)
{
    if(curve.GetType()!=GeomAbs_Line)return false;
    const auto start=curve.Value(first),end=curve.Value(last);
    const gp_Vec ab(start,end),ap(start,witness);
    const double length=ab.Magnitude();
    if(length<=Precision::Confusion())return false;
    const double fraction=ab.Dot(ap)/ab.SquareMagnitude();
    return fraction>=0.&&fraction<=1.&&ab.Crossed(ap).Magnitude()/length<=Precision::Confusion();
}

bool Target(const Fixture& fixture,const BRepAdaptor_Curve& curve,double first,double last)
{
    if(!fixture.RequiredWitness)return curve.GetType()==GeomAbs_Circle;
    if(curve.GetType()!=GeomAbs_Line)return false;
    const auto onTopCut=[&](const gp_Pnt& point){
        return std::abs(point.X()-fixture.RequiredWitness->X())<=Precision::Confusion()&&
            std::abs(point.Z()-fixture.RequiredWitness->Z())<=Precision::Confusion();};
    return onTopCut(curve.Value(first))&&onTopCut(curve.Value(last));
}

ObjectMap Run(const Fixture& fixture,const fs::path& directory)
{
    auto classification=machining_path_audit::AllMachining(fixture.Shape);
    const auto source=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
    if(!source.bOK||source.Shape.IsNull())
        throw std::runtime_error("Cannot reconstruct penetration fixture: "+fixture.Name);
    classification.MachiningFaceIds.clear();
    for(const auto& [id,face]:source.Faces)
    {
        auto& ids=fixture.IsMachining(face)?classification.MachiningFaceIds:classification.NonMachiningFaceIds;
        ids.push_back(id);
    }
    classification.MachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.MachiningFaceIds);
    classification.NonMachiningGeometry=R::detail::SectionFaceSubset(classification.SplitGeometry,classification.NonMachiningFaceIds);
    const auto split=G::Persistence::Serialize(classification.SplitGeometry);
    const auto machining=G::Persistence::Serialize(classification.MachiningGeometry);
    const auto stock=G::Persistence::Serialize(classification.NonMachiningGeometry);
    const auto edges=R::MachiningEdgeAnalysisWork{}.Run(classification,fixture.Section);
    const auto paths=R::MachiningPathAnalysisWork{}.Run(classification,fixture.Section,edges);
    const auto result=R::MachiningPenetrationWork{}.Run(classification,fixture.Section,edges,paths);
    const auto file=directory/(fixture.Name+".json");
    const auto artifacts=machining_penetration_export::Export(result,source,file,fixture.Name,true,
        classification.MachiningFaceIds);
    const bool unchanged=split==G::Persistence::Serialize(classification.SplitGeometry)&&
        machining==G::Persistence::Serialize(classification.MachiningGeometry)&&
        stock==G::Persistence::Serialize(classification.NonMachiningGeometry);

    int matching=0,mismatches=0;
    bool witnessed=false;
    std::map<std::pair<std::string,std::uint64_t>,std::vector<std::pair<double,double>>> classified;
    for(const auto& span:result.Spans)
    {
        if(!source.Edges.contains(span.EdgeId))continue;
        const BRepAdaptor_Curve curve(source.Edges.at(span.EdgeId));
        if(!Target(fixture,curve,span.FirstParameter,span.LastParameter))continue;
        if(span.Kind==fixture.ExpectedKind&&(!fixture.RequiredExit||span.Exit==*fixture.RequiredExit))++matching;
        else ++mismatches;
        if(fixture.RequiredWitness&&PointOnInterval(curve,span.FirstParameter,span.LastParameter,*fixture.RequiredWitness)&&
            span.Kind==fixture.ExpectedKind&&span.Exit==fixture.WitnessExit)witnessed=true;
        classified[{span.PathKey,span.EdgeId}].emplace_back(span.FirstParameter,span.LastParameter);
    }
    int targetUses=0,incompleteUses=0;
    for(const auto& path:paths.Edges)
    {
        if(!path.bSelectedPath||!path.bOnSkinPath)continue;
        for(const auto& use:path.SourceUses)
        {
            const BRepAdaptor_Curve curve(source.Edges.at(use.EdgeId));
            if(!Target(fixture,curve,use.FirstParameter,use.LastParameter))continue;
            ++targetUses;
            auto intervals=classified[{path.Key,use.EdgeId}];
            std::sort(intervals.begin(),intervals.end());
            double covered=use.FirstParameter;
            const double tolerance=Precision::PConfusion();
            for(const auto& [first,last]:intervals)
            {
                if(last<covered-tolerance)continue;
                if(first>covered+tolerance)break;
                covered=std::max(covered,last);
            }
            if(covered<use.LastParameter-tolerance)++incompleteUses;
        }
    }
    const bool passed=unchanged&&edges.bSuccess&&paths.bSuccess&&result.bSuccess&&
        matching>0&&mismatches==0&&targetUses>=fixture.MinimumTargetUses&&incompleteUses==0&&
        (!fixture.RequiredWitness||witnessed);
    return {{"name",fixture.Name},{"passed",passed},{"sourceUnchanged",unchanged},
        {"edgeSuccess",edges.bSuccess},{"pathSuccess",paths.bSuccess},{"penetrationSuccess",result.bSuccess},
        {"expectedKind",std::string(machining_penetration_export::KindName(fixture.ExpectedKind))},
        {"requiredWitness",fixture.RequiredWitness.has_value()},{"witnessPassed",witnessed},
        {"matchingSpans",matching},{"mismatchingSpans",mismatches},{"targetSourceUses",targetUses},
        {"incompleteSourceUses",incompleteUses},{"artifactAudit",artifacts},
        {"edgeDiagnostics",Strings(edges.Diagnostics)},{"pathDiagnostics",Strings(paths.Diagnostics)},
        {"penetrationDiagnostics",Strings(result.Diagnostics)}};
}
}

int MachiningPenetrationAudit(const fs::path& directory)
{
    using namespace machining_penetration_audit;
    bool passed=true;
    VariantArray fixtures;
    for(const auto& fixture:{Bore(false),Bore(true),Bore(true,true),EndCut(false),EndCut(true),AnnularEndCut()})
    {
        auto row=Run(fixture,directory);
        passed=row.at("passed").To<bool>()&&passed;
        fixtures.emplace_back(std::move(row));
    }
    Save(directory/"summary.json",{{"passed",passed},{"fixtures",std::move(fixtures)}});
    return passed?0:1;
}
