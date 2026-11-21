// Temporary read-only evidence for 005.IGS skin support and detached coedges.
#include <GeomConvert_SurfToAnaSurf.hxx>
#include <GeomAdaptor_Surface.hxx>

namespace source_skin_support_diagnostic {
VariantArray XYZ(const gp_Pnt& p) { return {p.X(),p.Y(),p.Z()}; }

struct Projection { double Distance=1e100,Parameter=0; };
Projection Project(const gp_Pnt& point,const TopoDS_Edge& edge) {
    double first=0,last=0;const auto curve=BRep_Tool::Curve(edge,first,last);
    if(curve.IsNull())return {};
    Projection result{point.Distance(curve->Value(first)),first};
    const auto endGap=point.Distance(curve->Value(last));if(endGap<result.Distance)result={endGap,last};
    GeomAPI_ProjectPointOnCurve projection(point,curve,first,last);
    if(projection.NbPoints()&&projection.LowerDistance()<result.Distance)
        result={projection.LowerDistance(),projection.LowerDistanceParameter()};
    return result;
}

std::uint64_t EdgeId(const TopoDS_Edge& edge,const O::SOpenCascadeBRepBuildResult& built) {
    for(const auto& [id,value]:built.Edges)if(value.IsSame(edge))return id;
    return 0;
}

ObjectMap Boundary(const TopoDS_Face& face,const TopoDS_Edge& edge,
    const O::SOpenCascadeBRepBuildResult& built) {
    BRepAdaptor_Curve curve(edge);BRepAdaptor_Surface surface(face,true);
    const double first=curve.FirstParameter(),last=curve.LastParameter();
    ObjectMap row{{"edgeId",std::to_string(EdgeId(edge,built))},{"orientation",int(edge.Orientation())},
        {"curveType",int(curve.GetType())},{"range",VariantArray{first,last}},
        {"start",XYZ(curve.Value(first))},{"end",XYZ(curve.Value(last))},
        {"edgeToleranceMm",BRep_Tool::Tolerance(edge)},
        {"sameParameter",bool(BRep_Tool::SameParameter(edge))}};
    try {
        BRepAdaptor_Curve2d pc(edge,face);
        const double a=pc.FirstParameter(),b=pc.LastParameter();
        double maximum=0;VariantArray examples;
        for(int i=0;i<=16;++i) {
            const double fraction=double(i)/16;
            const auto uv=pc.Value(a+(b-a)*fraction);
            const auto p=surface.Value(uv.X(),uv.Y());
            maximum=std::max(maximum,p.Distance(curve.Value(first+(last-first)*fraction)));
            if(i==0||i==8||i==16)examples.emplace_back(ObjectMap{
                {"fraction",fraction},{"curve3d",XYZ(curve.Value(first+(last-first)*fraction))},
                {"pcurveOnFace",XYZ(p)}});
        }
        row["pcurveRange"]=VariantArray{a,b};row["maxCurve3dPcurveGapMm"]=maximum;
        row["pcurveSamples"]=examples;
    }catch(const Standard_Failure& e){row["pcurveError"]=std::string(e.GetMessageString());}
    return row;
}

ObjectMap Match(const TopoDS_Edge& source,const TopoDS_Edge& target,
    const std::uint64_t sourceId,const std::uint64_t faceId,const std::uint64_t targetId) {
    BRepAdaptor_Curve a(source),b(target);
    const double first=a.FirstParameter(),last=a.LastParameter();
    const auto start=Project(a.Value(first),target),end=Project(a.Value(last),target);
    double forward=0,reverse=0;
    for(int i=0;i<=16;++i)forward=std::max(forward,Project(a.Value(first+(last-first)*double(i)/16),target).Distance);
    const double lo=std::min(start.Parameter,end.Parameter),hi=std::max(start.Parameter,end.Parameter);
    for(int i=0;i<=16;++i)reverse=std::max(reverse,Project(b.Value(lo+(hi-lo)*double(i)/16),source).Distance);
    return {{"sourceEdgeId",std::to_string(sourceId)},{"skinFaceId",std::to_string(faceId)},
        {"skinEdgeId",std::to_string(targetId)},{"sourceRange",VariantArray{first,last}},
        {"targetRange",VariantArray{b.FirstParameter(),b.LastParameter()}},
        {"mappedTargetRange",VariantArray{start.Parameter,end.Parameter}},
        {"startGapMm",start.Distance},{"endGapMm",end.Distance},
        {"maxSourceToTargetGapMm",forward},{"maxMappedTargetToSourceGapMm",reverse},
        {"sameNativeEdge",bool(source.IsSame(target))}};
}

int Export(const fs::path& source,const fs::path& output) {
    std::ifstream input(source,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read skin support checkpoint");
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    const auto model=G::Persistence::Deserialize(bytes);const auto before=G::Persistence::Serialize(model);
    const auto built=O::ReconstructOpenCascadeShape(model);
    if(!built.bOK)throw std::runtime_error("Cannot reconstruct skin support checkpoint");
    VariantArray faces,matches;
    for(const std::uint64_t faceId:{7,9,33,34}) {
        const auto face=built.Faces.at(faceId);const auto raw=BRep_Tool::Surface(face);
        BRepAdaptor_Surface surface(face,true);ObjectMap row{{"faceId",std::to_string(faceId)},
            {"surfaceType",int(surface.GetType())},{"faceToleranceMm",BRep_Tool::Tolerance(face)}};
        auto spline=Handle(Geom_BSplineSurface)::DownCast(raw);
        if(!spline.IsNull()) {
            row["uDegree"]=spline->UDegree();row["vDegree"]=spline->VDegree();
            row["uPoles"]=spline->NbUPoles();row["vPoles"]=spline->NbVPoles();
            row["uKnots"]=spline->NbUKnots();row["vKnots"]=spline->NbVKnots();
            row["uRational"]=bool(spline->IsURational());row["vRational"]=bool(spline->IsVRational());
            VariantArray poleRows;
            for(int u=1;u<=spline->NbUPoles();++u) {
                if(spline->NbUPoles()>32&&u!=1&&u!=spline->NbUPoles()&&u!=1+spline->NbUPoles()/2)continue;
                const auto a=spline->Pole(u,1),b=spline->Pole(u,spline->NbVPoles());
                double yzGap=0,weightMin=spline->Weight(u,1),weightMax=weightMin;
                for(int v=1;v<=spline->NbVPoles();++v){const auto p=spline->Pole(u,v);
                    yzGap=std::max(yzGap,std::hypot(p.Y()-a.Y(),p.Z()-a.Z()));
                    weightMin=std::min(weightMin,spline->Weight(u,v));weightMax=std::max(weightMax,spline->Weight(u,v));}
                poleRows.emplace_back(ObjectMap{{"uIndex",u},{"first",XYZ(a)},{"last",XYZ(b)},
                    {"maxRowYZGapMm",yzGap},{"weightRange",VariantArray{weightMin,weightMax}}});
            }
            row["poleRowSummary"]=poleRows;
        }
        double u,U,v,V;BRepTools::UVBounds(face,u,U,v,V);row["uvBounds"]=VariantArray{u,U,v,V};
        try {
            GeomConvert_SurfToAnaSurf converter(raw);
            const auto analytic=converter.ConvertToAnalytical(1e-7,u,U,v,V);
            ObjectMap conversion{{"toleranceMm",1e-7},{"converted",!analytic.IsNull()}};
            if(!analytic.IsNull()) {
                conversion["surfaceType"]=int(GeomAdaptor_Surface(analytic).GetType());conversion["gapMm"]=converter.Gap();
                if(auto cylinder=Handle(Geom_CylindricalSurface)::DownCast(analytic);!cylinder.IsNull()){
                    const auto axis=cylinder->Cylinder().Axis();conversion["radiusMm"]=cylinder->Radius();
                    conversion["axisOrigin"]=XYZ(axis.Location());conversion["axisDirection"]=VariantArray{axis.Direction().X(),axis.Direction().Y(),axis.Direction().Z()};}
            }
            row["analyticConversion"]=conversion;
        }catch(const Standard_Failure& e){row["analyticConversionError"]=std::string(e.GetMessageString());}
        VariantArray boundaries;
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(edge))continue;
            boundaries.emplace_back(Boundary(face,edge,built));
            for(const std::uint64_t sourceId:{2,7,11,13,29})
                matches.emplace_back(Match(built.Edges.at(sourceId),edge,sourceId,faceId,EdgeId(edge,built)));
        }
        row["boundaryCoedges"]=boundaries;faces.emplace_back(row);
    }
    const bool unchanged=before==G::Persistence::Serialize(model);
    Save(output,{{"source",Utf8(source)},{"status",std::string(unchanged?"passed":"failed")},
        {"neutralUnchanged",unchanged},{"diagnosticOnly",true},{"diagnosticPointsPerCurve",17},
        {"notice",std::string("Read-only native evidence; diagnostic probes do not generate toolpath points or alter skin classifications")},
        {"faces",faces},{"machiningToSkinBoundaryMatches",matches}});
    return unchanged?0:1;
}
}
