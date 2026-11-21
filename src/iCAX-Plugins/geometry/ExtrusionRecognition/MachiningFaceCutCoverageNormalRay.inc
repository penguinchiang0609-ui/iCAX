// Shared native event-ray contract. Included after CurveUse, Boundary and Event.
struct Hit final {const Boundary* Target=nullptr;double Parameter=0.,Depth=0.;};
bool RayHits(const CurveUse& source,double u,double direction,const std::vector<Boundary>& boundaries,
    const SMachiningFaceCutCoverageOptions& options,std::vector<Hit>& hits,std::string& reason,bool allowZero=false,
    const Boundary* selectedTarget=nullptr)
{
    gp_Pnt2d point;gp_Vec2d tangent;source.Curve->D1(u,point,tangent);
    if(tangent.SquareMagnitude()<=1.e-30) {reason="normal-strip-native-source-tangent-singular";return false;}
    gp_Vec2d axis(-tangent.Y(),tangent.X());axis.Normalize();axis*=direction;
    const occ::handle<Geom2d_Line> line=new Geom2d_Line(point,gp_Dir2d(axis));
    for(const auto& boundary:boundaries) {
        if(selectedTarget && &boundary!=selectedTarget)continue;
        // The positive self-tangent-cone certificate excludes every remote
        // self intersection with a normal of this entire open source chain.
        if(boundary.Source)continue;
        const auto& use=boundary.Use;
        const occ::handle<Geom2d_TrimmedCurve> trimmed=new Geom2d_TrimmedCurve(use.Curve,use.First,use.Last,true,false);
        Geom2dAPI_InterCurveCurve intersection(trimmed,line,std::min(1.e-10,options.dPositionTolerance*.001));
        if(intersection.NbSegments()) {reason="normal-strip-normal-coincides-with-a-physical-boundary";return false;}
        std::size_t before=hits.size();
        for(int i=1;i<=intersection.NbPoints();++i) {
            const auto& hit=intersection.Intersector().Point(i);
            const double parameter=hit.ParamOnFirst();
            if(parameter<use.First||parameter>use.Last)continue;
            const auto target=use.Curve->Value(parameter);const gp_Vec2d delta(point,target);
            const double depth=delta.Dot(axis);
            if(depth<0.||(depth==0.&&!allowZero))continue;
            const double residual=std::abs(delta.X()*axis.Y()-delta.Y()*axis.X());
            if(residual>options.dPositionTolerance) {reason="normal-strip-native-ray-intersection-residual";return false;}
            hits.push_back({&boundary,parameter,depth});
        }
        if(hits.size()>before+1) {reason="normal-strip-certified-boundary-returned-multiple-native-hits";return false;}
    }
    return true;
}
bool AtEvent(const CurveUse& source,const Event& event,const Boundary& target,double direction,
    const std::vector<Boundary>& boundaries,const SMachiningFaceCutCoverageOptions& options,double& parameter,std::string& reason)
{
    for(const auto& ref:event.Refs)if(ref.Edge==target.Use.EdgeId){parameter=ref.Parameter;return true;}
    // The open component already selected this target under the continuous
    // normal-family certificate. Solve only that SAME branch's closure at
    // the event. A different side may coincide with the endpoint normal;
    // that does not invalidate the selected target's unique native limit.
    // Interior first-hit selection still examines every physical boundary.
    std::vector<Hit> hits;if(!RayHits(source,event.SourceParameter,direction,boundaries,options,hits,reason,true,&target))return false;
    const auto found=std::find_if(hits.begin(),hits.end(),[&](const auto& hit){return hit.Target->Use.EdgeId==target.Use.EdgeId;});
    if(found==hits.end()){reason="normal-strip-native-target-event-parameter-not-solved";return false;}
    parameter=found->Parameter;return true;
}
