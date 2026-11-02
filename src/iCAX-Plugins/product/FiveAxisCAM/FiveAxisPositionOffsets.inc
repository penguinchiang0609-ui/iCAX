// Authored path-position corrections belong to CAM pose data. The nominal
// CAD/support curve remains the exact source, with local offsets interpolated
// between its source-addressed keypoints for execution and preview alike.
class PositionOffsets {
    using Vector=std::array<double,3>;
    const iCAX::CAM::Intent::SToolpathValue& Value;
    const iCAX::GeometryData::CompositeOffsetCurve3& Curve;
    iCAX::GeometryServices::OffsetTraversal::Evaluator& Evaluator;
    std::vector<double> Prefix;
    std::map<double,Vector> Knots;
    std::map<std::pair<std::size_t,double>,double> Measures;
    double Measure(std::size_t i,double u){
        namespace W=iCAX::GeometryServices::OffsetTraversal;
        if(const auto found=Measures.find({i,u});found!=Measures.end())return found->second;
        const auto start=W::Start(Curve.Segments.at(i));double result=Prefix.at(i);
        if(std::abs(start-u)>1e-12){if(u==W::End(Curve.Segments[i]))result=Prefix[i+1];
            else{iCAX::GeometryData::CompositeOffsetCurve3 part;part.Segments={Curve.Segments[i]};auto& range=W::Range(part.Segments.front());range.First=std::min(start,u);range.Last=std::max(start,u);result+=Evaluator.Length(part);}}
        Measures[{i,u}]=result;return result;
    }
    void Add(const Map& anchor,const Vector& value,bool replace){
        const auto a=iCAX::CAM::Intent::AnchorFromData(anchor);
        const auto found=std::find_if(Value.Geometry.Segments.begin(),Value.Geometry.Segments.end(),[&](const auto& s){return s.Id==a.SegmentId;});
        if(found==Value.Geometry.Segments.end())throw std::invalid_argument("CAM_POSITION_ANCHOR_NEEDS_REBIND");
        const auto x=Measure(found-Value.Geometry.Segments.begin(),a.Parameter);
        if(replace)Knots[x]=value;else Knots.try_emplace(x,value);
    }
public:
    bool HasOffsets()const{return !Knots.empty();}
    PositionOffsets(const iCAX::CAM::Intent::SToolpathValue& value,const iCAX::GeometryData::CompositeOffsetCurve3& curve,
        iCAX::GeometryServices::OffsetTraversal::Evaluator& evaluator):Value(value),Curve(curve),Evaluator(evaluator){
        if(!value.Channels.contains("poseControls"))return;
        const auto groups=ProjectFiveAxisPoseControls(value);if(!groups.contains("active"))return;const auto controls=groups.at("active").To<Array>();
        if(std::none_of(controls.begin(),controls.end(),[](const auto& item){return item.To<Map>().contains("positionOffset");}))return;
        Prefix={0};for(const auto& segment:curve.Segments){iCAX::GeometryData::CompositeOffsetCurve3 one;one.Segments={segment};Prefix.push_back(Prefix.back()+evaluator.Length(one));}
        Knots[0]={};Knots[Prefix.back()]={};
        if(value.Channels.contains("poseField"))for(const auto& item:value.Channels.at("poseField").To<Map>().at("entries").To<Array>()){
            const auto entry=item.To<Map>();if(!entry.contains("range"))continue;const auto range=entry.at("range").To<Map>();Add(range.at("start").To<Map>(),{},false);Add(range.at("end").To<Map>(),{},false);
        }
        for(const auto& item:controls){const auto control=item.To<Map>();Add(control.at("anchor").To<Map>(),control.contains("positionOffset")?RegistrationPoint(control.at("positionOffset")):Vector{},control.contains("positionOffset"));}
    }
    Vector Offset(std::size_t i,double u){
        if(Knots.empty())return {};const auto x=Measure(i,u);const auto b=Knots.lower_bound(x);
        if(b==Knots.begin())return b->second;if(b==Knots.end())return Knots.rbegin()->second;if(std::abs(b->first-x)<1e-10)return b->second;
        const auto a=std::prev(b);const auto t=(x-a->first)/(b->first-a->first);Vector out{};for(int k=0;k<3;++k)out[k]=a->second[k]+(b->second[k]-a->second[k])*t;return out;
    }
    Vector At(std::size_t i,double u){const auto sample=Evaluator.At(Curve.Segments.at(i),u);auto point=GenVec(sample.OffsetPoint);const auto offset=Offset(i,u);for(int k=0;k<3;++k)point[k]+=offset[k];return point;}
    Vector Apply(Vector point,std::size_t i,double u){const auto offset=Offset(i,u);for(int k=0;k<3;++k)point[k]+=offset[k];return point;}
};
