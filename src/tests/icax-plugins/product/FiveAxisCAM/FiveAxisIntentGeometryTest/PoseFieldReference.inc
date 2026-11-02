// Test-only reference for the replaced CAM axis evaluator. Intrinsic length/turn
// measures come from the neutral curve service; no CAD or Scene state is edited.
namespace
{
    namespace AxisMath
    {
        using V=std::array<double,3>;using Q=std::array<double,4>;
        double Dot(const V& a,const V& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
        V Cross(const V& a,const V& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
        V Unit(V v){const auto n=std::hypot(v[0],v[1],v[2]);if(!std::isfinite(n)||n<1e-12)throw std::invalid_argument("CAM_POSE_DIRECTION_INVALID");for(auto& x:v)x/=n;return v;}
        Q Rotation(const V& from,const V& to){const auto d=std::clamp(Dot(from,to),-1.,1.);if(d<-.999999){
            const auto cross=Cross(from,to);const auto length=std::hypot(cross[0],cross[1],cross[2]);
            if(length>1e-12){const auto axis=Unit(cross);const auto half=.5*std::atan2(length,d),s=std::sin(half);return {axis[0]*s,axis[1]*s,axis[2]*s,std::cos(half)};}
            // Canonical half-turn axis: identical when both vectors reverse.
            int i=0;for(int j=1;j<3;++j)if(std::abs(from[j])<std::abs(from[i]))i=j;
            V basis{0,0,0};basis[i]=1;auto axis=Unit(Cross(from,basis));
            for(auto x:axis)if(std::abs(x)>1e-12){if(x<0)for(auto& n:axis)n=-n;break;}
            return {axis[0],axis[1],axis[2],0};}const auto c=Cross(from,to);Q q{c[0],c[1],c[2],1+d};double n=0;for(auto x:q)n+=x*x;n=std::sqrt(n);for(auto& x:q)x/=n;return q;}
        Q Blend(Q a,Q b,double t){double dot=0;for(int i=0;i<4;++i)dot+=a[i]*b[i];if(dot<0){for(auto& x:b)x=-x;dot=-dot;}double wa=1-t,wb=t;
            if(dot<.999999){const auto angle=std::acos(std::clamp(dot,-1.,1.)),s=std::sin(angle);wa=std::sin((1-t)*angle)/s;wb=std::sin(t*angle)/s;}
            Q out;double n=0;for(int i=0;i<4;++i){out[i]=a[i]*wa+b[i]*wb;n+=out[i]*out[i];}n=std::sqrt(n);for(auto& x:out)x/=n;return out;}
        V Rotate(const Q& q,const V& v){const V u{q[0],q[1],q[2]};const auto a=Cross(u,v),b=Cross(u,a);V out;for(int i=0;i<3;++i)out[i]=v[i]+2*(q[3]*a[i]+b[i]);return Unit(out);}
    }
    AxisMath::V BaseToolAxis(const Map& pose,int sign,const iCAX::GeometryServices::BoundaryFrame& frame)
    {
        if(FixedToolAxis(pose))return RegistrationPoint(pose.at("direction"));
        if(String(pose,"initialization")!="SurfaceNormal"||RegistrationNumber(pose.at("leadAngleRad"))!=0||RegistrationNumber(pose.at("sideAngleRad"))!=0)throw std::invalid_argument("CAM_GENERATE_POSE_UNSUPPORTED");
        if(pose.contains("axisReversed")&&pose.at("axisReversed").To<bool>())sign=-sign;
        return AxisMath::Unit({frame.Normal.X*sign,frame.Normal.Y*sign,frame.Normal.Z*sign});
    }
    double ToolAxisStationDistance(const CutDefinition& cut,iCAX::Geometry::SCurveLocation at,const iCAX::Geometry::ICurveEditingService& editing)
    {
        double length=0;for(std::size_t i=0;i<at.SegmentIndex;++i)length+=editing.Measure(cut.Curve->Curve3.Segments[i]).LengthMm;
        auto segment=cut.Curve->Curve3.Segments.at(at.SegmentIndex);const auto first=segment.Range.Reversed?segment.Range.Last:segment.Range.First;
        if(std::abs(first-at.NativeParameter)>1e-12){segment.Range.First=std::min(first,at.NativeParameter);segment.Range.Last=std::max(first,at.NativeParameter);length+=editing.Measure(segment).LengthMm;}return length;
    }
    // These are geometry-anchored reference controls, independent of camera,
    // screen density and generation tolerance. Editing one keypoint vector
    // must preserve the neighboring controls instead of deforming the contour.
    std::vector<iCAX::Geometry::SCurveLocation> ToolAxisControlStations(const CutDefinition& cut,const iCAX::Geometry::ICurveEditingService& editing,bool includeManual=true)
    {
        double total=0;for(const auto& segment:cut.Curve->Curve3.Segments)total+=editing.Measure(segment).LengthMm;
        const auto sampled=editing.Sample(cut.Curve->Curve3,std::max(.01,total/3000.),std::max(.05,total/36.));
        std::vector<iCAX::Geometry::SCurveLocation> locations;double since=std::numeric_limits<double>::infinity();
        std::array<double,3> previous{};bool first=true;
        for(std::size_t i=0;i<sampled.Points.size();++i){const auto& p=sampled.Points[i];
            if(!first){double distance=0;for(int j=0;j<3;++j)distance+=std::pow(p.Point[j]-previous[j],2);since+=std::sqrt(distance);}previous=p.Point;first=false;
            const bool segmentEnd=i+1==sampled.Points.size()||sampled.Points[i+1].SegmentIndex!=p.SegmentIndex;
            const bool segmentStart=i==0||sampled.Points[i-1].SegmentIndex!=p.SegmentIndex;
            if(segmentStart||segmentEnd||since>=total/36.){locations.push_back({p.SegmentIndex,p.NativeParameter});since=0;}
        }
        if(includeManual&&String(cut.Pose,"mode")=="Continuous")for(const auto& v:cut.Pose.at("keypoints").To<Array>()){
            const auto point=v.To<Map>();locations.push_back(SplitLocation(cut,point.at("anchor").To<Map>()));
            if(point.contains("influence"))for(const auto& target:point.at("influence").To<Map>().at("targets").To<Array>())locations.push_back(SplitLocation(cut,target.To<Map>().at("anchor").To<Map>()));
        }
        locations.push_back(cut.Curve->Curve3.Closed?SplitLocation(cut,cut.Feature.at("start").To<Map>()):CutEndpoint(cut,true));
        std::sort(locations.begin(),locations.end(),[](const auto& a,const auto& b){return a.SegmentIndex!=b.SegmentIndex?a.SegmentIndex<b.SegmentIndex:a.NativeParameter<b.NativeParameter;});
        locations.erase(std::unique(locations.begin(),locations.end(),[](const auto& a,const auto& b){return a.SegmentIndex==b.SegmentIndex&&std::abs(a.NativeParameter-b.NativeParameter)<1e-12;}),locations.end());
        if(locations.size()>4096)throw std::invalid_argument("CAM_POSE_DISPLAY_SAMPLE_BUDGET");return locations;
    }
    class ToolAxisField
    {
        struct Knot{double Arc=0,Turn=0;AxisMath::Q Rotation{0,0,0,1};Text Transition;bool Smooth=false;};
        const CutDefinition& Cut;const iCAX::Geometry::SBoundaryPath& Path;const iCAX::Geometry::ICurveEditingService& Editing;int Sign;
        std::vector<iCAX::Geometry::SCurveMeasure> Measures;std::vector<double> Arcs,Turns;std::map<std::size_t,std::size_t> Source;
        std::vector<iCAX::Geometry::SCurveLocation> ControlStations;
        std::map<std::pair<std::size_t,double>,std::pair<double,double>> CoordinatesCache;
        std::map<std::pair<std::size_t,double>,iCAX::GeometryServices::BoundaryFrame> BoundaryFrames;
        std::vector<Knot> Knots;
        const iCAX::GeometryServices::BoundaryFrame& SourceFrame(const iCAX::GeometryData::BRepModel& model,
            const iCAX::GeometryServices::IBoundaryLeadService& boundary,const iCAX::Geometry::SCurveLocation& at)
        {
            const auto key=std::make_pair(at.SegmentIndex,at.NativeParameter);
            if(const auto found=BoundaryFrames.find(key);found!=BoundaryFrames.end())return found->second;
            return BoundaryFrames.emplace(key,boundary.Frame(model,CutBoundaryQuery(Cut,at))).first->second;
        }
        std::pair<double,double> Coordinates(std::size_t index,double u)
        {
            const auto cache=std::make_pair(index,u);if(const auto it=CoordinatesCache.find(cache);it!=CoordinatesCache.end())return it->second;
            if(CoordinatesCache.size()>200000)throw std::invalid_argument("CAM_POSE_MEASURE_BUDGET");
            const auto& original=Path.Curve.Segments.at(index);const auto& r=original.Range;
            if(!std::isfinite(u)||u<r.First-1e-12||u>r.Last+1e-12)throw std::invalid_argument("CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");
            const auto first=r.Reversed?r.Last:r.First,last=r.Reversed?r.First:r.Last;std::pair<double,double> out{Arcs.at(index),Turns.at(index)};
            if(std::abs(u-first)<1e-12*(1+r.Last-r.First)){}
            else if(std::abs(u-last)<1e-12*(1+r.Last-r.First)){out.first+=Measures[index].LengthMm;out.second+=Measures[index].TurnRad;}
            else{auto partial=original;partial.Range.First=std::min(first,u);partial.Range.Last=std::max(first,u);const auto measured=Editing.Measure(partial);out.first+=measured.LengthMm;out.second+=measured.TurnRad;}
            CoordinatesCache.emplace(cache,out);return out;
        }
    public:
        ToolAxisField(const CutDefinition& cut,const iCAX::Geometry::SBoundaryPath& path,const iCAX::GeometryData::BRepModel& model,int sign,
            const iCAX::Geometry::ICurveEditingService& editing,const iCAX::GeometryServices::IBoundaryLeadService& boundary,const ToolAxisField* measurementSeed=nullptr):Cut(cut),Path(path),Editing(editing),Sign(sign)
        {
            if(String(cut.Pose,"mode")!="Continuous"||cut.Pose.at("keypoints").To<Array>().empty())return;
            double arc=0,turn=0;
            if(measurementSeed){
                if(&measurementSeed->Cut!=&cut||&measurementSeed->Path!=&path||&measurementSeed->Editing!=&editing)throw std::invalid_argument("CAM_POSE_MEASUREMENT_SEED_MISMATCH");
                // Reuse only geometry measurements of this same frozen path.
                // Every candidate still builds its own authored pose knots.
                Measures=measurementSeed->Measures;Arcs=measurementSeed->Arcs;Turns=measurementSeed->Turns;Source=measurementSeed->Source;CoordinatesCache=measurementSeed->CoordinatesCache;ControlStations=measurementSeed->ControlStations;BoundaryFrames=measurementSeed->BoundaryFrames;
                if(!Measures.empty()){arc=Arcs.back()+Measures.back().LengthMm;turn=Turns.back()+Measures.back().TurnRad;}
            }else for(std::size_t i=0;i<path.Curve.Segments.size();++i){const auto measured=editing.Measure(path.Curve.Segments[i]);
                if(i)turn+=std::acos(std::clamp(AxisMath::Dot(Measures.back().LastTangent,measured.FirstTangent),-1.,1.));
                Arcs.push_back(arc);Turns.push_back(turn);Measures.push_back(measured);arc+=measured.LengthMm;turn+=measured.TurnRad;
                if(path.SourceSegments.at(i))Source.emplace(*path.SourceSegments.at(i),i);
            }
            if(!measurementSeed)ControlStations=ToolAxisControlStations(cut,editing,false);
            const auto metric=String(cut.Pose,"transitionMetric");
            Knots={{0,0,{0,0,0,1},metric},{arc,turn,{0,0,0,1},metric}};
            for(const auto& at:ControlStations){
                const auto c=Coordinates(Source.at(at.SegmentIndex),at.NativeParameter);
                if(std::none_of(Knots.begin(),Knots.end(),[&](const auto& k){return std::abs(k.Arc-c.first)<1e-8;}))Knots.push_back({c.first,c.second,{0,0,0,1},metric});
            }
            std::set<double> used;
            for(const auto& v:cut.Pose.at("keypoints").To<Array>()){
                const auto point=v.To<Map>();const auto at=SplitLocation(cut,point.at("anchor").To<Map>());const auto index=Source.at(at.SegmentIndex);
                const auto coordinate=Coordinates(index,at.NativeParameter);const auto frame=SourceFrame(model,boundary,at);
                const auto target=RegistrationPoint(point.at("direction"));const auto q=AxisMath::Rotation(BaseToolAxis(cut.Pose,sign,frame),target);
                for(auto existing:used)if(std::abs(existing-coordinate.first)<1e-8)throw std::invalid_argument("CAM_POSE_DUPLICATE_KEYPOINT");used.insert(coordinate.first);
                const Knot knot{coordinate.first,coordinate.second,q,String(point,"transition"),point.contains("influence")&&String(point.at("influence").To<Map>(),"interpolation")=="Smooth"};
                const auto existing=std::find_if(Knots.begin(),Knots.end(),[&](const auto& k){return std::abs(k.Arc-coordinate.first)<1e-8;});
                if(existing!=Knots.end())*existing=knot;else Knots.push_back(knot);
                if(cut.Curve->Curve3.Closed&&(coordinate.first<1e-8||arc-coordinate.first<1e-8)){
                    const std::size_t firstIndex=0,lastIndex=cut.Curve->Curve3.Segments.size()-1;const auto& a=cut.Curve->Curve3.Segments[firstIndex].Range;const auto& b=cut.Curve->Curve3.Segments[lastIndex].Range;
                    const auto firstFrame=SourceFrame(model,boundary,{firstIndex,a.Reversed?a.Last:a.First});
                    const auto lastFrame=SourceFrame(model,boundary,{lastIndex,b.Reversed?b.First:b.Last});
                    Knots.front()={0,0,AxisMath::Rotation(BaseToolAxis(cut.Pose,sign,firstFrame),target),knot.Transition,knot.Smooth};
                    Knots[1]={arc,turn,AxisMath::Rotation(BaseToolAxis(cut.Pose,sign,lastFrame),target),knot.Transition,knot.Smooth};
                }
            }
            // Each group owns explicit physical stations. Reference directions
            // are captured by the Scene command, so repeated edits never compound
            // previous rotations. Unselected stations keep their saved direction.
            std::set<double> affected;
            for(const auto& v:cut.Pose.at("keypoints").To<Array>()){
                const auto owner=v.To<Map>();if(!owner.contains("influence"))continue;const auto influence=owner.at("influence").To<Map>();
                const auto delta=AxisMath::Rotation(RegistrationPoint(influence.at("referenceDirection")),RegistrationPoint(owner.at("direction")));
                const bool smooth=String(influence,"interpolation")=="Smooth";
                for(const auto& item:influence.at("targets").To<Array>()){
                    const auto target=item.To<Map>();const auto at=SplitLocation(cut,target.at("anchor").To<Map>());const auto coordinate=Coordinates(Source.at(at.SegmentIndex),at.NativeParameter);
                    const auto same=[&](double x){return std::abs(x-coordinate.first)<1e-8||(cut.Curve->Curve3.Closed&&std::abs(std::abs(x-coordinate.first)-arc)<1e-8);};
                    for(auto x:affected)if(same(x))throw std::invalid_argument("CAM_POSE_INFLUENCE_OVERLAP");affected.insert(coordinate.first);
                    for(const auto& other:cut.Pose.at("keypoints").To<Array>()){
                        const auto p=other.To<Map>();if(!p.contains("influence")&&String(p,"id")!=String(owner,"id"))continue;
                        const auto location=SplitLocation(cut,p.at("anchor").To<Map>());if(same(Coordinates(Source.at(location.SegmentIndex),location.NativeParameter).first))throw std::invalid_argument("CAM_POSE_INFLUENCE_OWNER_SELECTED");
                    }
                    const auto desired=AxisMath::Rotate(AxisMath::Blend({0,0,0,1},delta,RegistrationNumber(target.at("weight"))),RegistrationPoint(target.at("referenceDirection")));
                    const auto put=[&](double a,double t,const iCAX::Geometry::SCurveLocation& location){
                        const auto frame=SourceFrame(model,boundary,location);const Knot knot{a,t,AxisMath::Rotation(BaseToolAxis(cut.Pose,sign,frame),desired),String(owner,"transition"),smooth};
                        const auto it=std::find_if(Knots.begin(),Knots.end(),[&](const auto& k){return std::abs(k.Arc-a)<1e-8;});if(it==Knots.end())Knots.push_back(knot);else *it=knot;
                    };
                    put(coordinate.first,coordinate.second,at);
                    if(cut.Curve->Curve3.Closed&&(coordinate.first<1e-8||arc-coordinate.first<1e-8)){
                        const auto& first=cut.Curve->Curve3.Segments.front().Range;const auto& last=cut.Curve->Curve3.Segments.back().Range;
                        put(0,0,{0,first.Reversed?first.Last:first.First});put(arc,turn,{cut.Curve->Curve3.Segments.size()-1,last.Reversed?last.First:last.Last});
                    }
                }
            }
            std::sort(Knots.begin(),Knots.end(),[](const auto& a,const auto& b){return a.Arc<b.Arc;});
        }
        AxisMath::V AtJoined(std::size_t index,double u,const iCAX::GeometryServices::BoundaryFrame& frame)
        {
            const auto base=BaseToolAxis(Cut.Pose,Sign,frame);if(Knots.empty())return base;const auto c=Coordinates(index,u);
            auto right=std::upper_bound(Knots.begin(),Knots.end(),c.first,[](double x,const auto& k){return x<k.Arc;});if(right==Knots.begin())return AxisMath::Rotate(right->Rotation,base);if(right==Knots.end())return AxisMath::Rotate(Knots.back().Rotation,base);
            const auto& a=*(right-1);const auto& b=*right;double t=(c.first-a.Arc)/(b.Arc-a.Arc);
            if(a.Transition=="DirectionChange"&&b.Turn-a.Turn>1e-10)t=(c.second-a.Turn)/(b.Turn-a.Turn);
            t=std::clamp(t,0.,1.);if(a.Smooth||b.Smooth)t=t*t*(3-2*t);
            return AxisMath::Rotate(AxisMath::Blend(a.Rotation,b.Rotation,std::clamp(t,0.,1.)),base);
        }
        AxisMath::V AtSource(iCAX::Geometry::SCurveLocation at,const iCAX::GeometryServices::BoundaryFrame& frame)
        {if(Knots.empty())return BaseToolAxis(Cut.Pose,Sign,frame);return AtJoined(Source.at(at.SegmentIndex),at.NativeParameter,frame);}
    };
    void InsertToolAxisAnchors(iCAX::Geometry::SCurvePiece& piece,const CutDefinition& cut,const iCAX::Geometry::SBoundaryPath& path,const iCAX::Geometry::ICurveEditingService& editing)
    {
        // Reference controls are real pose-check boundaries in every axis
        // mode. Mode changes must not alter the spatial sampling.
        const auto controls=ToolAxisControlStations(cut,editing);auto original=piece;piece.Curve.Segments.clear();piece.SourceSegmentIndices.clear();
        for(std::size_t i=0;i<original.Curve.Segments.size();++i){auto segment=original.Curve.Segments[i];const auto index=original.SourceSegmentIndices[i];std::vector<double> cuts{segment.Range.First,segment.Range.Last};
            if(path.SourceSegments.at(index))for(const auto& at:controls){
                if(at.SegmentIndex==*path.SourceSegments.at(index)&&at.NativeParameter>segment.Range.First+1e-12&&at.NativeParameter<segment.Range.Last-1e-12)cuts.push_back(at.NativeParameter);
            }
            std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());if(segment.Range.Reversed)std::reverse(cuts.begin(),cuts.end());
            for(std::size_t j=1;j<cuts.size();++j){
                // Keep a source station inside each interval so interpolation
                // repair can add an axis constraint without moving either end
                // or changing the CAD curve / existing generated point list.
                const auto middle=cuts[j-1]+(cuts[j]-cuts[j-1])*.5;
                const std::array<double,3> stations{cuts[j-1],middle,cuts[j]};
                for(std::size_t k=1;k<stations.size();++k){segment.Range.First=std::min(stations[k-1],stations[k]);segment.Range.Last=std::max(stations[k-1],stations[k]);piece.Curve.Segments.push_back(segment);piece.SourceSegmentIndices.push_back(index);}
            }
        }
    }
    template<class Check> void RefineToolAxisInfluence(iCAX::Geometry::SCurvePiece& piece,const CutDefinition& cut,const iCAX::Geometry::SBoundaryPath& path,
        ToolAxisField& field,const iCAX::GeometryData::BRepModel& model,const iCAX::GeometryServices::IBoundaryLeadService& boundary,const Check& check)
    {
        if(String(cut.Pose,"mode")!="Continuous")return;const auto points=cut.Pose.at("keypoints").To<Array>();
        if(std::none_of(points.begin(),points.end(),[](const auto& p){return p.template To<Map>().contains("influence");}))return;
        const double requested=RegistrationNumber(cut.Pose.at("angularToleranceRad")),tolerance=requested>0?std::min(requested,.00174532925199433):.00174532925199433;
        const auto original=piece;piece.Curve.Segments.clear();piece.SourceSegmentIndices.clear();
        for(std::size_t i=0;i<original.Curve.Segments.size();++i){const auto segment=original.Curve.Segments[i];const auto pathIndex=original.SourceSegmentIndices[i];
            const auto axis=[&](double u){const auto frame=CutBoundaryFrame(cut,path,{pathIndex,u},model,boundary);return field.AtJoined(pathIndex,u,frame);};
            const auto subdivide=[&](auto&& self,double first,double last,unsigned depth)->void{
                check();if(piece.Curve.Segments.size()>=100000)throw std::invalid_argument("CAM_POSE_SAMPLING_BUDGET");
                const auto a=axis(first),b=axis(last);const auto delta=AxisMath::Rotation(a,b);bool split=false;
                // A smoothstep has zero midpoint error; quarter probes are
                // necessary to detect its easing on straight/short geometry.
                for(double t:{.25,.5,.75}){const auto actual=axis(first+(last-first)*t),linear=AxisMath::Rotate(AxisMath::Blend({0,0,0,1},delta,t),a);if(AxisMath::Dot(actual,linear)<std::cos(tolerance))split=true;}
                if(split){if(depth>=18||std::abs(last-first)<1e-13)throw std::invalid_argument("CAM_POSE_SAMPLING_TOLERANCE");const auto mid=(first+last)/2;self(self,first,mid,depth+1);self(self,mid,last,depth+1);return;}
                auto part=segment;part.Range.First=std::min(first,last);part.Range.Last=std::max(first,last);piece.Curve.Segments.push_back(part);piece.SourceSegmentIndices.push_back(pathIndex);
            };
            const auto& r=segment.Range;subdivide(subdivide,r.Reversed?r.Last:r.First,r.Reversed?r.First:r.Last,0);
        }
    }
}
