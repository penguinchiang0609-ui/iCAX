// Included inside MachiningFaceCutCoverageWork.cpp's private namespace.
// The scalar source coordinate remains the original Circle parameter. The
// ray is O + rho*r(u), with signed rho: inward rays may pass through O.
constexpr double RadialPi=3.141592653589793238462643383279502884;

struct RadialBoundary final
{
    std::uint64_t EdgeId=0;
    bool bCircle=false;
    double First=0.,Last=0.;
    gp_Lin2d Line;
    gp_Circ2d Circle;
    TopoDS_Vertex FirstVertex,LastVertex;
    double FirstVertexTolerance=0.,LastVertexTolerance=0.;
};

struct RadialEndpointEvent final
{
    double SourceParameter=0.;
    const RadialBoundary* Target=nullptr;
    double TargetParameter=0.,VertexTolerance=0.;
};

struct RadialSource final
{
    RadialBoundary Boundary;
    double Direction=1.;
    std::vector<RadialEndpointEvent> EndpointEvents;
    bool bCanonicalCornerEvents=false;
};

struct RadialHit final
{
    const RadialBoundary* Target=nullptr;
    double Parameter=0.,Rho=0.;
    int Root=0; // -/+ for a circle; zero for a line or coincident tangent roots.
};

double Cross2(const gp_Vec2d& a,const gp_Vec2d& b) {return a.X()*b.Y()-a.Y()*b.X();}
double RadialNumeric(double scale) {return 128.*std::numeric_limits<double>::epsilon()*std::max(1.,scale);}

gp_Pnt2d RadialPoint(const RadialBoundary& boundary,double parameter)
{
    if(!boundary.bCircle)return boundary.Line.Location().Translated(gp_Vec2d(boundary.Line.Direction())*parameter);
    const auto& circle=boundary.Circle;
    return circle.Location().Translated(circle.Radius()*(gp_Vec2d(circle.Position().XDirection())*std::cos(parameter)
        +gp_Vec2d(circle.Position().YDirection())*std::sin(parameter)));
}

gp_Vec2d RadialDirection(const RadialSource& source,double parameter)
{
    const auto& circle=source.Boundary.Circle;
    return gp_Vec2d(circle.Position().XDirection())*std::cos(parameter)
        +gp_Vec2d(circle.Position().YDirection())*std::sin(parameter);
}

void RadialPeriodic(double base,double period,double first,double last,std::vector<double>& values)
{
    const double tolerance=ParamTolerance(first,last);
    const auto lower=static_cast<long long>(std::ceil((first-base-tolerance)/period));
    const auto upper=static_cast<long long>(std::floor((last-base+tolerance)/period));
    if(upper-lower>8)throw std::runtime_error("circle-native-range-exceeds-supported-period-multiplicity");
    for(auto k=lower;k<=upper;++k) {
        double value=base+static_cast<double>(k)*period;
        if(value<first-tolerance||value>last+tolerance)continue;
        value=std::clamp(value,first,last);values.push_back(value);
    }
}

bool ReadRadialBoundary(const TopoDS_Face& face,const TopoDS_Edge& edge,std::uint64_t edgeId,
    double first,double last,RadialBoundary& result,std::string& reason)
{
    BRepAdaptor_Curve native(edge);
    if(native.GetType()!=GeomAbs_Line&&native.GetType()!=GeomAbs_Circle) {
        reason="plane-circle-boundary-native-3d-curve-is-not-line-or-circle:E"+std::to_string(edgeId);return false;
    }
    if(!BRep_Tool::SameParameter(edge)||!BRep_Tool::SameRange(edge)) {
        reason="plane-circle-boundary-does-not-preserve-native-pcurve-parameter:E"+std::to_string(edgeId);return false;
    }
    double pf=0.,pl=0.;const auto curve=BRep_Tool::CurveOnSurface(edge,face,pf,pl);
    if(curve.IsNull()) {reason="plane-circle-boundary-has-no-native-pcurve:E"+std::to_string(edgeId);return false;}
    const double tolerance=ParamTolerance(pf,pl);
    if(!std::isfinite(first)||!std::isfinite(last)||first>=last||first<pf-tolerance||last>pl+tolerance) {
        reason="plane-circle-native-boundary-range-outside-pcurve:E"+std::to_string(edgeId);return false;
    }
    Geom2dAdaptor_Curve pcurve(curve,first,last);
    if((native.GetType()==GeomAbs_Circle&&pcurve.GetType()!=GeomAbs_Circle)
        ||(native.GetType()==GeomAbs_Line&&pcurve.GetType()!=GeomAbs_Line)) {
        reason="plane-circle-boundary-pcurve-is-not-matching-native-line-or-circle:E"+std::to_string(edgeId);return false;
    }
    result.EdgeId=edgeId;result.First=first;result.Last=last;
    result.bCircle=native.GetType()==GeomAbs_Circle;
    if(result.bCircle) {
        result.Circle=pcurve.Circle();
        if(last-first>2.*RadialPi+ParamTolerance(first,last)) {
            reason="circle-native-boundary-spans-more-than-one-period:E"+std::to_string(edgeId);return false;
        }
    } else result.Line=pcurve.Line();
    double ef=0.,el=0.;BRep_Tool::Range(edge,ef,el);TopoDS_Vertex vf,vl;TopExp::Vertices(edge,vf,vl,false);
    if(std::abs(first-ef)<=ParamTolerance(ef,el)&&!vf.IsNull()) {
        result.FirstVertex=vf;result.FirstVertexTolerance=std::max(BRep_Tool::Tolerance(edge),BRep_Tool::Tolerance(vf));
        if(native.Value(ef).Distance(BRep_Tool::Pnt(vf))>result.FirstVertexTolerance+RadialNumeric(native.Value(ef).Distance(gp_Pnt()))) {
            reason="circle-native-start-vertex-outside-declared-brep-tolerance:E"+std::to_string(edgeId);return false;
        }
    }
    if(std::abs(last-el)<=ParamTolerance(ef,el)&&!vl.IsNull()) {
        result.LastVertex=vl;result.LastVertexTolerance=std::max(BRep_Tool::Tolerance(edge),BRep_Tool::Tolerance(vl));
        if(native.Value(el).Distance(BRep_Tool::Pnt(vl))>result.LastVertexTolerance+RadialNumeric(native.Value(el).Distance(gp_Pnt()))) {
            reason="circle-native-end-vertex-outside-declared-brep-tolerance:E"+std::to_string(edgeId);return false;
        }
    }
    return true;
}

int RadialEndpointRoot(const RadialSource& source,double sourceParameter,const RadialBoundary& target,
    double targetParameter,double vertexTolerance)
{
    if(!target.bCircle)return 0;
    const auto origin=source.Boundary.Circle.Location();const auto ray=RadialDirection(source,sourceParameter);
    const double rho=gp_Vec2d(origin,RadialPoint(target,targetParameter)).Dot(ray);
    const double along=gp_Vec2d(origin,target.Circle.Location()).Dot(ray),difference=rho-along;
    // Only a proven native endpoint may use its own declared BRep tolerance.
    // This admits the same topological tangent vertex's two numerical roots;
    // it is never a general first-hit distance or event-merging tolerance.
    if(std::abs(difference)<=std::max(vertexTolerance,RadialNumeric(std::max(std::abs(rho),std::abs(along)))))return 0;
    return difference<0.?-1:1;
}

void RadialAddHit(const RadialBoundary& target,double parameter,double rho,int root,std::vector<RadialHit>& hits,
    bool nativeEndpoint=false)
{
    const double tolerance=ParamTolerance(target.First,target.Last);
    if(parameter<target.First-tolerance||parameter>target.Last+tolerance)return;
    parameter=std::clamp(parameter,target.First,target.Last);
    for(auto& hit:hits)if(hit.Target==&target&&std::abs(hit.Parameter-parameter)<=tolerance
        &&std::abs(hit.Rho-rho)<=RadialNumeric(std::max(std::abs(hit.Rho),std::abs(rho)))) {
        if(nativeEndpoint){hit.Parameter=parameter;hit.Rho=rho;}
        if(hit.Root!=root)hit.Root=0;return;
    }
    hits.push_back({&target,parameter,rho,root});
}

void RadialIntersections(const RadialSource& source,double sourceParameter,const RadialBoundary& target,
    std::vector<RadialHit>& hits,bool& ambiguous)
{
    const auto origin=source.Boundary.Circle.Location();const auto ray=RadialDirection(source,sourceParameter);
    if(!target.bCircle) {
        const auto direction=gp_Vec2d(target.Line.Direction());const gp_Vec2d delta(origin,target.Line.Location());
        const double denominator=Cross2(ray,direction);
        if(std::abs(denominator)<=RadialNumeric(1.)) {
            if(std::abs(Cross2(delta,ray))<=RadialNumeric(delta.Magnitude()))ambiguous=true;
        } else {
            const double rho=Cross2(delta,direction)/denominator,parameter=Cross2(delta,ray)/denominator;
            RadialAddHit(target,parameter,rho,0,hits);
        }
    } else {
        const auto& circle=target.Circle;const gp_Vec2d delta(origin,circle.Location());
        const double along=delta.Dot(ray),constant=delta.SquareMagnitude()-circle.Radius()*circle.Radius();
        double discriminant=along*along-constant;
        const double arithmetic=RadialNumeric(along*along+std::abs(constant));
        if(discriminant>=-arithmetic) {
            discriminant=std::max(0.,discriminant);const double root=std::sqrt(discriminant);
            for(const int sign:{-1,1}) {
                const double rho=along+static_cast<double>(sign)*root;
                const auto point=origin.Translated(ray*rho);const gp_Vec2d relative(circle.Location(),point);
                const double angle=std::atan2(relative.Dot(gp_Vec2d(circle.Position().YDirection())),
                    relative.Dot(gp_Vec2d(circle.Position().XDirection())));
                std::vector<double> parameters;RadialPeriodic(angle,2.*RadialPi,target.First,target.Last,parameters);
                for(const auto parameter:parameters)RadialAddHit(target,parameter,rho,root<=RadialNumeric(std::abs(along))?0:sign,hits);
            }
        }
    }
    // An event constructed from an actual native endpoint retains that exact
    // endpoint parameter even at a nearly double quadratic tangent root.
    for(const auto parameter:{target.First,target.Last}) {
        const auto point=RadialPoint(target,parameter);const gp_Vec2d relative(origin,point);
        if(std::abs(Cross2(relative,ray))>RadialNumeric(relative.Magnitude()))continue;
        const double rho=relative.Dot(ray);int root=0;
        if(target.bCircle) {
            const double along=gp_Vec2d(origin,target.Circle.Location()).Dot(ray);
            const double difference=rho-along;
            root=std::abs(difference)<=RadialNumeric(std::max(std::abs(rho),std::abs(along)))?0:(difference<0.?-1:1);
        }
        RadialAddHit(target,parameter,rho,root,hits,true);
    }
}

bool RadialSameRoot(int first,int second) {return first==second||first==0||second==0;}

struct RadialTangentLine final
{
    const RadialBoundary* Boundary=nullptr;
    double VertexTolerance=0.;
};

std::vector<RadialTangentLine> RadialTangentLines(const RadialBoundary& circle,double parameter,
    const TopoDS_Vertex& vertex,double circleTolerance,const std::vector<RadialBoundary>& targets)
{
    std::vector<RadialTangentLine> result;if(vertex.IsNull())return result;
    const auto center=circle.Circle.Location(),point=RadialPoint(circle,parameter);
    for(const auto& line:targets)if(!line.bCircle) {
        for(const bool first:{true,false}) {
            const auto& end=first?line.FirstVertex:line.LastVertex;
            if(end.IsNull()||!end.IsSame(vertex))continue;
            const double lineParameter=first?line.First:line.Last;
            const double lineTolerance=first?line.FirstVertexTolerance:line.LastVertexTolerance;
            const auto direction=gp_Vec2d(line.Line.Direction());
            const auto foot=line.Line.Location().Translated(direction*gp_Vec2d(line.Line.Location(),center).Dot(direction));
            const double arithmetic=RadialNumeric(std::max(circle.Circle.Radius(),center.Distance(line.Line.Location())));
            if(std::abs(center.Distance(foot)-circle.Circle.Radius())>std::max(circleTolerance,lineTolerance)+arithmetic
                ||point.Distance(foot)>circleTolerance+arithmetic
                ||RadialPoint(line,lineParameter).Distance(foot)>lineTolerance+arithmetic)continue;
            result.push_back({&line,lineTolerance});
        }
    }
    return result;
}

struct RadialCornerAnchor final
{
    TopoDS_Vertex Vertex;
    double SourceParameter=0.;
};

// Different-radius corner vertices are not the same topological vertex.
// They define the same normal event only with three independent native facts:
// concentric analytic circles, each vertex within its declared BRep tolerance
// of its incident line's exact circle tangency, and strictly parallel lines.
// A non-tangent side truncation does not satisfy this certificate.
std::vector<RadialCornerAnchor> RadialCornerAnchors(const RadialSource& source,
    const std::vector<RadialBoundary>& targets)
{
    std::vector<RadialCornerAnchor> result;const auto& native=source.Boundary;
    for(const bool sourceFirst:{true,false}) {
        const auto& vertex=sourceFirst?native.FirstVertex:native.LastVertex;
        if(vertex.IsNull())continue;
        const double parameter=sourceFirst?native.First:native.Last;
        const double tolerance=sourceFirst?native.FirstVertexTolerance:native.LastVertexTolerance;
        const auto lines=RadialTangentLines(native,parameter,vertex,tolerance,targets);
        if(lines.empty())continue;
        for(const auto& target:targets)if(target.bCircle)for(const bool targetFirst:{true,false}) {
            const auto& targetVertex=targetFirst?target.FirstVertex:target.LastVertex;
            if(targetVertex.IsNull()||targetVertex.IsSame(vertex))continue;
            const double targetParameter=targetFirst?target.First:target.Last;
            const double targetTolerance=targetFirst?target.FirstVertexTolerance:target.LastVertexTolerance;
            if(native.Circle.Location().Distance(target.Circle.Location())>std::max(tolerance,targetTolerance))continue;
            const auto targetLines=RadialTangentLines(target,targetParameter,targetVertex,targetTolerance,targets);
            bool parallel=false;
            for(const auto& line:lines)for(const auto& other:targetLines)
                if(std::abs(Cross2(gp_Vec2d(line.Boundary->Line.Direction()),gp_Vec2d(other.Boundary->Line.Direction())))<=1.e-10)
                    parallel=true;
            if(parallel)result.push_back({targetVertex,parameter});
        }
    }
    return result;
}

bool RadialEndpoint(const RadialSource& source,double parameter,const RadialHit& middle,
    double& targetParameter,std::string& reason)
{
    double endpointNearest=std::numeric_limits<double>::infinity();bool endpointFound=false;
    for(const auto& event:source.EndpointEvents) {
        if(event.SourceParameter!=parameter||event.Target!=middle.Target
            ||!RadialSameRoot(RadialEndpointRoot(source,parameter,*event.Target,event.TargetParameter,event.VertexTolerance),middle.Root))continue;
        const double distance=std::abs(event.TargetParameter-middle.Parameter);
        if(distance<endpointNearest){endpointNearest=distance;targetParameter=event.TargetParameter;endpointFound=true;}
    }
    if(endpointFound)return true;
    std::vector<RadialHit> hits;bool ambiguous=false;RadialIntersections(source,parameter,*middle.Target,hits,ambiguous);
    double nearest=std::numeric_limits<double>::infinity();bool found=false,tie=false;
    for(const auto& hit:hits)if(RadialSameRoot(hit.Root,middle.Root)) {
        const double distance=std::abs(hit.Parameter-middle.Parameter);
        if(distance<nearest-ParamTolerance(middle.Target->First,middle.Target->Last)) {
            nearest=distance;targetParameter=hit.Parameter;found=true;tie=false;
        } else if(std::abs(distance-nearest)<=ParamTolerance(middle.Target->First,middle.Target->Last)
            &&std::abs(hit.Parameter-targetParameter)>ParamTolerance(middle.Target->First,middle.Target->Last))tie=true;
    }
    if(!found||tie) {reason="circle-target-native-parameter-at-angular-event-not-unique";return false;}
    return true;
}

bool RadialSourceParameter(const RadialSource& source,const RadialHit& branch,double targetParameter,
    double first,double last,double& sourceParameter,std::string& reason)
{
    bool endpointFound=false;
    for(const auto& event:source.EndpointEvents) {
        if(event.Target!=branch.Target||event.TargetParameter!=targetParameter
            ||event.SourceParameter<first||event.SourceParameter>last
            ||!RadialSameRoot(RadialEndpointRoot(source,event.SourceParameter,*event.Target,targetParameter,event.VertexTolerance),branch.Root))continue;
        if(endpointFound&&event.SourceParameter!=sourceParameter) {
            reason="circle-topological-endpoint-source-parameter-not-unique";return false;
        }
        sourceParameter=event.SourceParameter;endpointFound=true;
    }
    if(endpointFound)return true;
    const auto point=RadialPoint(*branch.Target,targetParameter);
    const gp_Vec2d relative(source.Boundary.Circle.Location(),point);
    if(relative.Magnitude()<=RadialNumeric(source.Boundary.Circle.Radius())) {
        reason="circle-source-native-parameter-at-center-not-unique";return false;
    }
    const double angle=std::atan2(relative.Dot(gp_Vec2d(source.Boundary.Circle.Position().YDirection())),
        relative.Dot(gp_Vec2d(source.Boundary.Circle.Position().XDirection())));
    std::vector<double> candidates;RadialPeriodic(angle,RadialPi,first,last,candidates);
    bool found=false;
    for(const auto candidate:candidates) {
        std::vector<RadialHit> hits;bool ambiguous=false;RadialIntersections(source,candidate,*branch.Target,hits,ambiguous);
        for(const auto& hit:hits) {
            if(!RadialSameRoot(hit.Root,branch.Root)
                ||std::abs(hit.Parameter-targetParameter)>8.*ParamTolerance(branch.Target->First,branch.Target->Last))continue;
            if(found&&std::abs(sourceParameter-candidate)>ParamTolerance(first,last)) {
                reason="circle-source-native-parameter-at-target-event-not-unique";return false;
            }
            sourceParameter=candidate;found=true;
        }
    }
    if(!found)reason="circle-source-native-parameter-at-target-event-not-solved";
    return found;
}

bool AppendRadialArrival(const RadialSource& source,const RadialHit& branch,double first,double last,
    const SMachiningPathEdgeAnalysis& path,const std::vector<const SMachiningEdgeAnalysis*>& attributes,
    std::vector<SMachiningFaceCutArrival>& arrivals,std::string& reason)
{
    double a=0.,b=0.,sourceAtA=first,sourceAtB=last;
    if(!RadialEndpoint(source,first,branch,a,reason)||!RadialEndpoint(source,last,branch,b,reason))return false;
    if(a>b){std::swap(a,b);std::swap(sourceAtA,sourceAtB);}
    if(!(b>a)) {reason="circle-first-boundary-collapses-to-a-native-point";return false;}
    std::vector<std::pair<double,double>> covered;
    for(const auto* attr:attributes) {
        if(attr->EdgeId!=branch.Target->EdgeId||!HasFace(*attr,path.FaceId))continue;
        const double f=std::max(a,attr->FirstParameter),l=std::min(b,attr->LastParameter);
        if(!(l>f))continue;
        SMachiningFaceCutArrival arrival;arrival.FaceId=path.FaceId;arrival.SourcePathKey=path.Key;
        arrival.SourceEdgeId=source.Boundary.EdgeId;arrival.TargetEdgeId=branch.Target->EdgeId;
        arrival.TargetFirstParameter=f;arrival.TargetLastParameter=l;arrival.TargetSpanKey=attr->Key;
        arrival.TargetSkinRelation=attr->SkinRelation;arrival.TargetConvexity=attr->Convexity;
        arrival.bTargetSkinRelationValid=attr->bSkinRelationValid;arrival.bTargetConvexityValid=attr->bConvexityValid;
        if(f==a)arrival.SourceFirstParameter=sourceAtA;
        else if(!RadialSourceParameter(source,branch,f,first,last,arrival.SourceFirstParameter,reason))return false;
        if(l==b)arrival.SourceLastParameter=sourceAtB;
        else if(!RadialSourceParameter(source,branch,l,first,last,arrival.SourceLastParameter,reason))return false;
        arrival.Method="plane-circle-radial-first-physical-boundary";
        arrival.Reason="first-positive-signed-radial-boundary-on-current-finite-face";
        arrivals.push_back(std::move(arrival));covered.emplace_back(f,l);
    }
    std::sort(covered.begin(),covered.end());double end=a;
    for(const auto& range:covered) {if(range.first>end+ParamTolerance(a,b))break;end=std::max(end,range.second);}
    if(end<b-ParamTolerance(a,b)) {reason="circle-target-native-interval-has-no-complete-edge-analysis-attributes";return false;}
    return true;
}

bool MapRadialSource(const TopoDS_Face& face,const O::SOpenCascadeBRepBuildResult& built,
    const SMachiningFaceEdgeAnalysis& boundary,const SMachiningPathEdgeAnalysis& path,
    const std::vector<const SMachiningEdgeAnalysis*>& attributes,const SMachiningFaceCutCoverageOptions& options,
    std::vector<SMachiningFaceCutArrival>& arrivals,std::string& method,std::string& reason)
{
    method="plane-circle-radial-first-physical-boundary";
    BRepAdaptor_Surface surface(face,true);
    if(surface.GetType()!=GeomAbs_Plane||path.AxisMode!=EMachiningPathAxisMode::PlaneTangentCrossNormal) {
        reason="circle-driver-requires-native-plane-and-recorded-plane-axis-mode";return false;
    }
    if(path.Samples.size()!=2||!path.Samples[0].bToolAxisValid||!path.Samples[1].bToolAxisValid) {
        reason="source-has-no-two-recorded-ready-tool-axes";return false;
    }
    const auto plane=surface.Plane();const gp_Vec px(plane.Position().XDirection()),py(plane.Position().YDirection());
    std::map<std::uint64_t,std::set<bool>> directions;std::vector<RadialBoundary> targets;std::set<std::uint64_t> seen;
    bool sourceWire=false;
    for(const auto& wire:boundary.Wires) {
        if(!wire.bOrdered) {reason="face-has-unordered-physical-boundary-wire";return false;}
        if(wire.WireId==path.WireId)sourceWire=true;
        for(const auto& group:wire.Groups)for(const auto& use:group.SourceUses) {
            if(wire.WireId==path.WireId)directions[use.EdgeId].insert(use.bReversed);
            if(!seen.insert(use.EdgeId).second)continue;
            const auto edge=built.Edges.find(use.EdgeId);
            if(edge==built.Edges.end()) {reason="circle-physical-target-native-edge-missing";return false;}
            if(BRep_Tool::Degenerated(edge->second)||BRep_Tool::IsClosed(edge->second,face))continue;
            double f=0.,l=0.;BRep_Tool::Range(edge->second,f,l);RadialBoundary target;
            if(!ReadRadialBoundary(face,edge->second,use.EdgeId,f,l,target,reason))return false;
            targets.push_back(std::move(target));
        }
    }
    if(!sourceWire||targets.empty()) {reason="circle-driver-has-no-proven-ordered-source-wire-or-target-boundary";return false;}
    std::vector<RadialSource> sources;
    for(const auto& use:path.SourceUses) {
        if(!directions.contains(use.EdgeId)||directions.at(use.EdgeId).size()!=1
            ||!directions.at(use.EdgeId).contains(use.bReversed)) {reason="circle-source-relative-wire-direction-not-proven";return false;}
        const auto edge=built.Edges.find(use.EdgeId);if(edge==built.Edges.end()) {reason="circle-source-native-edge-missing";return false;}
        RadialSource source;
        if(!ReadRadialBoundary(face,edge->second,use.EdgeId,use.FirstParameter,use.LastParameter,source.Boundary,reason))return false;
        if(!source.Boundary.bCircle) {reason="plane-mixed-line-circle-logical-driver-pending";return false;}
        if(source.Boundary.Circle.Radius()<=options.dPositionTolerance) {reason="circle-source-radius-below-position-resolution";return false;}
        const auto& axes=source.Boundary.Circle.Position();
        const double hand=Cross2(gp_Vec2d(axes.XDirection()),gp_Vec2d(axes.YDirection()))>=0.?1.:-1.;
        source.Direction=-hand*(use.bReversed?-1.:1.);sources.push_back(std::move(source));
    }
    if(sources.empty()) {reason="selected-source-has-no-native-range";return false;}
    // Recorded axes remain authoritative. Determine their common correction to
    // the oriented relative-wire family, then prove that both endpoints agree.
    double correction=0.;
    for(const auto& sample:path.Samples) {
        auto axis=Vec(sample.ToolAxis);if(axis.SquareMagnitude()<=1.e-24) {reason="source-recorded-tool-axis-is-zero";return false;}
        axis.Normalize();gp_Vec2d projected(axis.Dot(px),axis.Dot(py));
        if(projected.Magnitude()<std::cos(options.dAngularToleranceRadians)) {reason="recorded-circle-axis-is-not-in-the-plane";return false;}
        projected.Normalize();bool found=false;
        for(const auto& source:sources) {
            const auto& native=source.Boundary;
            if(native.EdgeId!=sample.SourceEdgeId||sample.Parameter<native.First-ParamTolerance(native.First,native.Last)
                ||sample.Parameter>native.Last+ParamTolerance(native.First,native.Last))continue;
            const auto radial=RadialDirection(source,sample.Parameter);const double dot=radial.Dot(projected);
            if(std::abs(dot)<std::cos(options.dAngularToleranceRadians)) {reason="recorded-circle-axis-is-not-native-radial";return false;}
            const double value=(dot>=0.?1.:-1.)*source.Direction;
            if(correction!=0.&&correction!=value) {reason="recorded-circle-endpoint-axis-senses-disagree";return false;}
            correction=value;found=true;
        }
        if(!found) {reason="recorded-circle-endpoint-source-use-not-found";return false;}
    }
    for(auto& source:sources)source.Direction*=correction;
    bool complete=true;
    for(auto& source:sources) {
        const auto& native=source.Boundary;const auto origin=native.Circle.Location();
        const auto cornerAnchors=RadialCornerAnchors(source,targets);
        source.bCanonicalCornerEvents=!cornerAnchors.empty();
        std::vector<double> events{native.First,native.Last};
        const auto addEndpoint=[&](const RadialBoundary& target,double targetParameter,
            const TopoDS_Vertex& vertex,double vertexTolerance) {
            double base=0.;bool anchored=false;
            if(!vertex.IsNull()) {
                if(!native.FirstVertex.IsNull()&&vertex.IsSame(native.FirstVertex)){base=native.First;anchored=true;}
                else if(!native.LastVertex.IsNull()&&vertex.IsSame(native.LastVertex)){base=native.Last;anchored=true;}
                if(!anchored)for(const auto& anchor:cornerAnchors)if(vertex.IsSame(anchor.Vertex)) {
                    base=anchor.SourceParameter;anchored=true;break;
                }
            }
            if(!anchored) {
                gp_Pnt2d point=RadialPoint(target,targetParameter);
                if(!vertex.IsNull()) {
                    const gp_Vec displacement(plane.Location(),BRep_Tool::Pnt(vertex));
                    point=gp_Pnt2d(displacement.Dot(px),displacement.Dot(py));
                }
                const gp_Vec2d direction(origin,point);
                if(direction.SquareMagnitude()<=RadialNumeric(native.Circle.Radius())*RadialNumeric(native.Circle.Radius()))return;
                base=std::atan2(direction.Dot(gp_Vec2d(native.Circle.Position().YDirection())),
                    direction.Dot(gp_Vec2d(native.Circle.Position().XDirection())));
            }
            std::vector<double> parameters;RadialPeriodic(base,RadialPi,native.First,native.Last,parameters);
            for(const auto parameter:parameters) {
                events.push_back(parameter);source.EndpointEvents.push_back({parameter,&target,targetParameter,vertexTolerance});
            }
        };
        for(const auto& target:targets) {
            addEndpoint(target,target.First,target.FirstVertex,target.FirstVertexTolerance);
            addEndpoint(target,target.Last,target.LastVertex,target.LastVertexTolerance);
            if(target.bCircle) {
                const gp_Vec2d centers(origin,target.Circle.Location());const double distance=centers.Magnitude();
                if(distance>=target.Circle.Radius()&&distance>0.) {
                    const double angle=std::atan2(centers.Dot(gp_Vec2d(native.Circle.Position().YDirection())),
                        centers.Dot(gp_Vec2d(native.Circle.Position().XDirection())));
                    const double tangent=std::asin(std::min(1.,target.Circle.Radius()/distance));
                    for(const auto base:{angle-tangent,angle+tangent}) {
                        std::vector<double> candidates;RadialPeriodic(base,RadialPi,native.First,native.Last,candidates);
                        for(const auto parameter:candidates) {
                            const auto ray=RadialDirection(source,parameter);
                            const auto point=origin.Translated(ray*centers.Dot(ray));
                            const gp_Vec2d relative(target.Circle.Location(),point);
                            const double targetAngle=std::atan2(relative.Dot(gp_Vec2d(target.Circle.Position().YDirection())),
                                relative.Dot(gp_Vec2d(target.Circle.Position().XDirection())));
                            std::vector<double> targetParameters;
                            RadialPeriodic(targetAngle,2.*RadialPi,target.First,target.Last,targetParameters);
                            if(std::any_of(targetParameters.begin(),targetParameters.end(),[&](double value){return value!=target.First&&value!=target.Last;}))
                                events.push_back(parameter);
                        }
                    }
                }
            }
        }
        std::sort(events.begin(),events.end());
        events.erase(std::unique(events.begin(),events.end()),events.end());
        for(std::size_t i=1;i<events.size();++i) {
            const double a=events[i-1],b=events[i];if(!(b>a))continue;
            const double middle=a+(b-a)*.5;RadialHit winner;bool found=false,ambiguous=false;
            double nearest=std::numeric_limits<double>::infinity();
            for(const auto& target:targets) {
                std::vector<RadialHit> hits;bool overlap=false;RadialIntersections(source,middle,target,hits,overlap);
                if(overlap) {ambiguous=true;reason="circle-ray-overlaps-a-physical-boundary";break;}
                for(const auto& hit:hits) {
                    // Exclude only this source occurrence's actual zero-depth
                    // entry root. The same edge's other angular branch remains.
                    if(target.EdgeId==native.EdgeId
                        &&std::abs(std::remainder(hit.Parameter-middle,2.*RadialPi))<=ParamTolerance(native.First,native.Last))continue;
                    const double distance=source.Direction*(hit.Rho-native.Circle.Radius());
                    const double numerical=RadialNumeric(std::max(std::abs(hit.Rho),native.Circle.Radius()));
                    if(distance<=numerical)continue;
                    if(distance<nearest-numerical) {nearest=distance;winner=hit;found=true;ambiguous=false;}
                    else if(std::abs(distance-nearest)<=numerical&&found
                        &&(hit.Target->EdgeId!=winner.Target->EdgeId
                            ||std::abs(hit.Parameter-winner.Parameter)>ParamTolerance(target.First,target.Last)))ambiguous=true;
                }
            }
            if(!found||ambiguous) {
                reason=ambiguous?"circle-first-physical-boundary-is-geometrically-ambiguous":"circle-no-positive-physical-boundary-in-recorded-radial-direction";
                complete=false;continue;
            }
            if(!AppendRadialArrival(source,winner,a,b,path,attributes,arrivals,reason))complete=false;
        }
    }
    if(complete) {
        reason="all-native-circle-source-intervals-reach-first-physical-boundary;recorded-endpoint-axis-sense-preserved";
        if(std::any_of(sources.begin(),sources.end(),[](const auto& source){return source.bCanonicalCornerEvents;}))
            reason+=";native-g1-parallel-corner-end-events-canonicalized";
    }
    return complete;
}
