// Neutral convex forbidden volumes. The caller owns path construction and the
// conservative continuous-sweep error bound; this layer owns full-thickness
// intersection and the one connected planar material used by every output.
struct PlateStationError:std::runtime_error {
    size_t StationIndex;
    PlateStationError(size_t station,const char* reason):std::runtime_error(reason),StationIndex(station){}
};
void ValidateKeepOuts(const SectionPlateInput& in){
    if(in.KeepOuts.size()>250000)throw std::invalid_argument("PLATE_KEEPOUT_LIMIT");
    for(const auto& cell:in.KeepOuts){
        if(cell.Vertices.empty()||cell.Vertices.size()>128||!std::isfinite(cell.Margin)||cell.Margin<0||cell.Margin>1e6)throw std::invalid_argument("PLATE_KEEPOUT_INVALID");
        for(auto point:cell.Vertices)for(double value:point)if(!std::isfinite(value)||std::abs(value)>1e9)throw std::invalid_argument("PLATE_KEEPOUT_INVALID");
    }
}
PathsD PlateMaterial(const SectionPlate& plate){
    PathsD paths{JointPath(plate.Outline)};
    for(const auto& hole:plate.Holes){auto path=JointPath(hole);if(IsPositive(path))std::reverse(path.begin(),path.end());paths.push_back(std::move(path));}
    return paths;
}
void AssignPlateMaterial(SectionPlate& plate,const PathsD& paths,const char* error){
    if(std::count_if(paths.begin(),paths.end(),[](const auto& path){return IsPositive(path);})!=1)throw std::runtime_error(error);
    plate.Holes.clear();
    for(const auto& path:paths){std::vector<P2> loop;for(auto p:path)loop.push_back({p.x,p.y});
        // Boolean output already defines the accepted material boundary.
        // Simplifying it can bridge a concave corner and add small wedges
        // above a checked support envelope or inside a forbidden volume.
        if(IsPositive(path))plate.Outline=Normalize(loop,false);else plate.Holes.push_back(Normalize(loop,false));}
}
PathD KeepOutHull(std::vector<P2> points){
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
    if(points.size()<3)return {};
    std::vector<P2> hull;for(auto p:points){while(hull.size()>1&&Cross(hull[hull.size()-2],hull.back(),p)<=0)hull.pop_back();hull.push_back(p);}const auto lower=hull.size();
    for(auto i=points.rbegin()+1;i!=points.rend();++i){while(hull.size()>lower&&Cross(hull[hull.size()-2],hull.back(),*i)<=0)hull.pop_back();hull.push_back(*i);}hull.pop_back();
    return hull.size()>=3?JointPath(hull):PathD{};
}
PathsD PlateKeepOutProjection(const SectionPlateInput& in,const SectionPlate& plate,const std::function<void()>& check){
    PathsD projected;const auto& m=plate.Frame;
    const auto bounds=GetBounds(JointPath(plate.Outline));
    for(const auto& cell:in.KeepOuts){check();
        if(plate.Base&&!cell.AffectsBase)continue;
        // Local infinity-norm expansion contains the requested Euclidean
        // margin. This deliberately errs on the side of removing material.
        const double margin=cell.Margin+.00001,lower=-margin,upper=plate.Thickness+margin;
        std::vector<std::array<double,3>> local;double d0=1e100,d1=-1e100,u0=1e100,u1=-1e100,v0=1e100,v1=-1e100;
        for(auto p:cell.Vertices){for(int k=0;k<3;++k)p[k]-=m[k*4+3];std::array<double,3> q{};for(int k=0;k<3;++k)q[k]=m[k]*p[0]+m[k+4]*p[1]+m[k+8]*p[2];
            local.push_back(q);u0=std::min(u0,q[0]);u1=std::max(u1,q[0]);v0=std::min(v0,q[1]);v1=std::max(v1,q[1]);d0=std::min(d0,q[2]);d1=std::max(d1,q[2]);}
        if(d1<lower||d0>upper||u1+margin<bounds.left||u0-margin>bounds.right||v1+margin<bounds.top-in.BaseThickness||v0-margin>bounds.bottom)continue;
        std::vector<P2> points;
        const auto add=[&](double u,double v){for(double du:{-margin,margin})for(double dv:{-margin,margin})points.push_back({u+du,v+dv});};
        for(auto p:local)if(p[2]>=lower&&p[2]<=upper)add(p[0],p[1]);
        // Every convex-hull edge is a pair of input vertices. Extra pair
        // intersections lie inside the hull, so taking the 2D hull gives the
        // exact projection of the clipped convex body before margin expansion.
        for(size_t a=0;a<local.size();++a)for(size_t b=a+1;b<local.size();++b){const auto p=local[a],q=local[b];if(std::abs(q[2]-p[2])<1e-15)continue;
            for(double plane:{lower,upper}){const double t=(plane-p[2])/(q[2]-p[2]);if(t>=0&&t<=1)add(p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1]));}}
        if(auto hull=KeepOutHull(std::move(points));!hull.empty())projected.push_back(std::move(hull));
    }
    if(projected.empty())return {};
    std::vector<PathsD> groups;for(size_t i=0;i<projected.size();i+=64){check();groups.push_back(Union(PathsD(projected.begin()+i,projected.begin()+std::min(i+64,projected.size())),FillRule::NonZero,6));}
    while(groups.size()>1){std::vector<PathsD> next;for(size_t i=0;i<groups.size();i+=2){check();auto joined=std::move(groups[i]);if(i+1<groups.size())joined.insert(joined.end(),std::make_move_iterator(groups[i+1].begin()),std::make_move_iterator(groups[i+1].end()));next.push_back(Union(joined,FillRule::NonZero,6));}groups=std::move(next);}
    return std::move(groups.front());
}
void CheckPlateWeb(const SectionPlate& plate,double web){
    const auto material=PlateMaterial(plate);
    // Erosion disconnects a load path narrower than the specified band.
    // A tiny tolerance admits an exactly minimum-width designed band.
    const auto core=InflatePaths(material,-std::max(0.,web/2-.0001),JoinType::Miter,EndType::Polygon,2.,6);
    if(std::count_if(core.begin(),core.end(),[](const auto& path){return IsPositive(path);})!=1){if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<"fixture web failure base="<<plate.Base<<" station="<<plate.StationIndex<<" core bodies="<<std::count_if(core.begin(),core.end(),[](const auto& path){return IsPositive(path);})<<std::endl;throw std::runtime_error("PLATE_KEEPOUT_MINIMUM_WEB");}
    PathsD previous;for(const auto& hole:plate.Holes){const auto expanded=InflatePaths(PathsD{JointPath(hole)},web-.0001,JoinType::Miter,EndType::Polygon,2.,6);
        if(!Difference(expanded,PathsD{JointPath(plate.Outline)},FillRule::NonZero,6).empty()||!Intersect(expanded,previous,FillRule::NonZero,6).empty()){if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<"fixture web failure base="<<plate.Base<<" station="<<plate.StationIndex<<" hole="<<previous.size()<<" border="<<!Difference(expanded,PathsD{JointPath(plate.Outline)},FillRule::NonZero,6).empty()<<std::endl;throw std::runtime_error("PLATE_KEEPOUT_MINIMUM_WEB");}previous.push_back(JointPath(hole));}
}
PathsD PlateBearingRegion(const SectionPlate& plate,double web){
    const auto material=PlateMaterial(plate);const double radius=std::max(0.,web/2-.0001);
    const auto core=InflatePaths(material,-radius,JoinType::Miter,EndType::Polygon,2.,6);
    // A narrow finger can disappear during erosion while the lower beam's
    // core stays connected. Its distant tip must not count as a bearing patch.
    return Intersect(material,InflatePaths(core,radius,JoinType::Miter,EndType::Polygon,2.,6),FillRule::NonZero,6);
}
bool PlateBearingContains(const PathsD& paths,P2 point){
    int winding=0;for(const auto& path:paths){const auto location=PointInPolygon(PointD(point[0],point[1]),path);
        if(location==PointInPolygonResult::IsOn)return true;
        if(location==PointInPolygonResult::IsInside)winding+=IsPositive(path)?1:-1;
    }return winding!=0;
}
PathsD LargestPlateMaterial(const PathsD& paths){
    auto largest=paths.end();double largestArea=0;
    for(auto i=paths.begin();i!=paths.end();++i)if(IsPositive(*i)){
        double area=Area(*i);for(const auto& hole:paths)if(!IsPositive(hole)&&PointInPolygon(hole.front(),*i)!=PointInPolygonResult::IsOutside)area+=Area(hole);
        if(area>largestArea){largestArea=area;largest=i;}
    }
    if(largest==paths.end())throw std::runtime_error("PLATE_KEEPOUT_EMPTY");
    PathsD holes;for(const auto& path:paths)if(!IsPositive(path)&&PointInPolygon(path.front(),*largest)!=PointInPolygonResult::IsOutside){auto hole=path;std::reverse(hole.begin(),hole.end());holes.push_back(std::move(hole));}
    return Difference(PathsD{*largest},holes,FillRule::NonZero,6);
}
void ApplyPlateKeepOuts(const SectionPlateInput& in,SectionPlate& plate,const PathsD& forbidden){
    if(forbidden.empty())return;
    const auto material=PlateMaterial(plate),removed=Intersect(material,forbidden,FillRule::NonZero,6);
    if(removed.empty())return;
    // Contour canonicalisation simplifies at 1e-5 mm. A larger machining-side
    // allowance prevents that simplification from filling the forbidden set.
    const auto cut=InflatePaths(forbidden,.0001,JoinType::Miter,EndType::Polygon,2.,6);
    auto remaining=Difference(material,cut,FillRule::NonZero,6);
    // A sparse bearing station must retain its complete connected support.
    // Discarding an offcut here can silently remove an entire side of the
    // workpiece. Let the skeleton choose a neighbouring spare station instead.
    if(in.Automatic&&in.Pattern=="Skeleton"&&!plate.Base&&!plate.Connection&&
       std::count_if(remaining.begin(),remaining.end(),[](const auto& path){return IsPositive(path);})>1)
        throw std::runtime_error("PLATE_KEEPOUT_DISCONNECTED");
    // Disconnected offcuts are discarded, never emitted as another loose
    // solid. Retain the largest connected body, then recheck contacts and feet.
    remaining=LargestPlateMaterial(remaining);
    if(plate.Base){
        // Remove narrow bridges and fingers instead of requiring a different
        // outside margin. Restore only the selected load-bearing core, clipped
        // to the original material: this operation can never refill a keep-out.
        // All feet, tenons and final support are checked against this result.
        const double radius=std::max(0.,in.MinimumWeb/2-.0001);
        const auto core=LargestPlateMaterial(InflatePaths(remaining,-radius,JoinType::Miter,EndType::Polygon,2.,6));
        remaining=LargestPlateMaterial(Intersect(remaining,InflatePaths(core,radius,JoinType::Miter,EndType::Polygon,2.,6),FillRule::NonZero,6));
    }
    AssignPlateMaterial(plate,remaining,"PLATE_KEEPOUT_DISCONNECTED");
    CheckPlateWeb(plate,in.MinimumWeb);
    plate.KeepOutReliefCount=std::count_if(removed.begin(),removed.end(),[](const auto& path){return IsPositive(path);});
    if(!plate.Base){const auto bounds=GetBounds(JointPath(plate.Outline));plate.Span={bounds.left,bounds.right};}
}
void CheckPlateKeepOuts(const SectionPlate& plate,const PathsD& forbidden){
    if(!forbidden.empty()&&!Intersect(PlateMaterial(plate),forbidden,FillRule::NonZero,6).empty())throw std::runtime_error("PLATE_KEEPOUT_CONFLICT");
}
void CheckPlateFoot(const SectionPlateInput& in,const SectionPlate& plate,const SectionPlate& base){
    double bearing=0;const auto baseMaterial=PlateMaterial(base);const auto& m=plate.Frame;
    for(size_t i=0;i<plate.Outline.size();++i){auto a=plate.Outline[i],b=plate.Outline[(i+1)%plate.Outline.size()];
        if(std::abs(a[1])>1e-6||std::abs(b[1])>1e-6)continue;
        const double first=std::min(a[0],b[0]),last=std::max(a[0],b[0]);PathD foot;
        for(auto p:JointRect(first,0,last,plate.Thickness))foot.emplace_back(m[0]*p[0]+m[2]*p[1]+m[3],m[4]*p[0]+m[6]*p[1]+m[7]);
        if(!Difference(PathsD{foot},baseMaterial,FillRule::NonZero,6).empty())throw std::runtime_error("PLATE_KEEPOUT_FOOT_UNSUPPORTED");
        bearing+=last-first;
    }
    if(bearing<in.MinimumWeb)throw std::runtime_error("PLATE_KEEPOUT_FOOT_UNSUPPORTED");
}
