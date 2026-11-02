// Neutral 2D fabrication geometry. All topology is computed by Clipper2;
// IDs, resource publication and manufacturing approval remain in the product.
void ValidateJoints(const SectionPlateInput& in){
    if(in.JointMode!="Weld"&&in.JointMode!="TabSlot")throw std::invalid_argument("PLATE_JOINT_MODE_INVALID");
    if(in.JointMode=="Weld"){
        if(in.Automatic&&in.Pattern=="Skeleton"&&(!std::isfinite(in.JointClearance)||!std::isfinite(in.JointWeb)||in.JointClearance<0||in.JointClearance>in.Thickness/2||in.JointWeb<.5||in.JointWeb>50))throw std::invalid_argument("PLATE_JOINT_DIMENSION_INVALID");
        return;
    }
    for(double n:{in.JointClearance,in.TenonWidth,in.TenonPitch,in.JointWeb})if(!std::isfinite(n))throw std::invalid_argument("PLATE_JOINT_DIMENSION_INVALID");
    if(in.JointClearance<0||in.JointClearance>std::min(in.Thickness,in.BaseThickness)/2||in.TenonWidth<in.Thickness||in.TenonWidth>500||in.JointWeb<.5||in.JointWeb>50||in.TenonPitch<in.TenonWidth+2*in.JointWeb+in.JointClearance||in.TenonPitch>1000)throw std::invalid_argument("PLATE_JOINT_DIMENSION_INVALID");
    if(!std::isfinite(in.TenonRadius)||in.TenonRadius<0||in.TenonRadius>std::min((in.TenonWidth-1e-5)/2,in.BaseThickness/2))throw std::invalid_argument("PLATE_TENON_RADIUS_INVALID");
}
void JointProfile(SectionPlate& p,const PathsD& add,const PathsD& cut){
    auto material=Union(PlateMaterial(p),add,FillRule::NonZero,6);
    if(!cut.empty())material=Difference(material,cut,FillRule::NonZero,6);
    AssignPlateMaterial(p,material,"PLATE_JOINT_DISCONNECTED");
}
double ProfileHeight(const SectionPlate& p,double u){
    double height=0;for(size_t i=0;i<p.Outline.size();++i){const auto a=p.Outline[i],b=p.Outline[(i+1)%p.Outline.size()];if(std::abs(b[0]-a[0])<1e-9)continue;
        const double t=(u-a[0])/(b[0]-a[0]);if(t>=-1e-8&&t<=1+1e-8)height=std::max(height,a[1]+t*(b[1]-a[1]));}return height;
}
double JointHeight(const SectionPlate& p,double first,double last){
    double height=std::min(ProfileHeight(p,first),ProfileHeight(p,last));
    for(auto q:p.Outline)if(q[0]>first&&q[0]<last&&q[1]>1e-7)height=std::min(height,q[1]);return height;
}
void UpdatePlateContacts(SectionPlate& plate,const std::vector<SlabEdge>& selected,double minimumWeb,const PathsD* bearing=nullptr){
    plate.Contacts.clear();plate.ContactLength=0;std::array<std::vector<size_t>,256> bins;
    const auto bin=[&](double u){return std::clamp(static_cast<int>((u-plate.Span[0])/(plate.Span[1]-plate.Span[0])*255),0,255);};
    for(size_t i=0;i<selected.size();++i)if(selected[i].NormalZ>=.15&&selected[i].Profile[1][0]>=plate.Span[0]&&selected[i].Profile[0][0]<=plate.Span[1])for(int k=bin(selected[i].Profile[0][0]);k<=bin(selected[i].Profile[1][0]);++k)bins[k].push_back(i);
    for(size_t i=0;i<plate.Outline.size();++i){auto a=plate.Outline[i],b=plate.Outline[(i+1)%plate.Outline.size()];if(std::abs(a[0]-b[0])<1e-7||std::min(a[1],b[1])<=minimumWeb+1e-5)continue;
        bool contact=false;for(double fraction:{.15,.5,.85}){const double u=a[0]+(b[0]-a[0])*fraction,z=plate.Frame[11]+a[1]+(b[1]-a[1])*fraction;
            if(bearing&&!PlateBearingContains(*bearing,{u,z-plate.Frame[11]}))continue;
            for(auto index:bins[bin(u)]){const auto& edge=selected[index];if(u<edge.Profile[0][0]-1e-7||u>edge.Profile[1][0]+1e-7)continue;
                double t=(u-edge.Profile[0][0])/(edge.Profile[1][0]-edge.Profile[0][0]),height=edge.Profile[0][1]+t*(edge.Profile[1][1]-edge.Profile[0][1]);if(std::abs(height-z)>2*plate.TopRetreat+2e-5)continue;
                std::array<double,3> point;for(int k=0;k<3;++k)point[k]=edge.First[k]+t*(edge.Last[k]-edge.First[k]);plate.Contacts.push_back(point);contact=true;break;}
        }if(contact)plate.ContactLength+=std::hypot(a[0]-b[0],a[1]-b[1]);}
}
void ApplyPlateJoints(const SectionPlateInput& in,std::vector<SectionPlate>& plates,SectionPlate& base,const std::map<size_t,PathsD>& forbidden,const std::function<void()>& check){
    std::size_t next=0;const double half=(in.Thickness+in.JointClearance)/2,edge=in.JointWeb+in.JointClearance/2;
    struct Crossing {size_t A,B;bool BottomA;std::vector<P2> BoundaryA,BoundaryB;};
    std::vector<std::vector<Crossing>> crossings(plates.size());
    // Pair plans read the unmodified profiles. Their two matching slots are
    // merged in original (i,j) order, retaining deterministic joint IDs.
    ForPlateIndices(plates.size(),in.MaximumConcurrency,[&](size_t i,size_t){
        const auto& a=plates[i];const auto sa=in.Stations[a.StationIndex];
        for(size_t j=i+1;j<plates.size();++j){check();const auto& b=plates[j];const auto sb=in.Stations[b.StationIndex];if(sa.AlongX==sb.AlongX)continue;
            const double ua=sb.Coordinate,ub=sa.Coordinate;
            if(ua+half<=a.Span[0]||ua-half>=a.Span[1]||ub+half<=b.Span[0]||ub-half>=b.Span[1])continue;
            if(ua-half<a.Span[0]+in.JointWeb||ua+half>a.Span[1]-in.JointWeb)throw PlateStationError(a.StationIndex,"PLATE_LAP_EDGE_WEB");
            if(ub-half<b.Span[0]+in.JointWeb||ub+half>b.Span[1]-in.JointWeb)throw PlateStationError(b.StationIndex,"PLATE_LAP_EDGE_WEB");
            const double height=std::min(JointHeight(a,ua-half,ua+half),JointHeight(b,ub-half,ub+half)),seat=height/2;
            if(seat-in.JointClearance/2<in.JointWeb-1e-7)throw PlateStationError(a.StationIndex,"PLATE_LAP_MINIMUM_WEB");
            if(!in.KeepOuts.empty())for(auto [p,u]:std::array<std::pair<const SectionPlate*,double>,2>{{{&a,ua},{&b,ub}}}){
                const auto band=JointRect(u-half-in.JointWeb,seat-in.JointWeb-in.JointClearance/2,u+half+in.JointWeb,seat+in.JointWeb+in.JointClearance/2);
                if(!Difference(PathsD{JointPath(band)},PlateMaterial(*p),FillRule::NonZero,6).empty())throw PlateStationError(p->StationIndex,"PLATE_KEEPOUT_LAP_WEB");
            }
            const auto boundary=[&](const SectionPlate& p,double u,bool bottom){double ceiling=0;for(auto q:p.Outline)ceiling=std::max(ceiling,q[1]);return JointRect(u-half,bottom?-in.BaseThickness-1:seat-in.JointClearance/2,u+half,bottom?seat+in.JointClearance/2:ceiling+1);};
            crossings[i].push_back({i,j,sa.AlongX,boundary(a,ua,sa.AlongX),boundary(b,ub,sb.AlongX)});
        }
    });
    std::vector<PathsD> cuts(plates.size());std::vector<std::vector<P2>> footCuts(plates.size());
    for(const auto& row:crossings)for(const auto& joint:row){check();++next;
        const auto append=[&](size_t i,size_t mate,bool bottom,const std::vector<P2>& boundary){cuts[i].push_back(JointPath(boundary));if(bottom)footCuts[i].push_back({boundary[0][0],boundary[1][0]});plates[i].Joints.push_back({bottom?"LapBottom":"LapTop",next,plates[mate].StationIndex,false,boundary});};
        append(joint.A,joint.B,joint.BottomA,joint.BoundaryA);append(joint.B,joint.A,!joint.BottomA,joint.BoundaryB);
    }
    struct Tab {PathD Material;std::vector<P2> Hole,Boundary;};
    // A welded skeleton still needs matching half-laps at its crossings;
    // overlapping full plates are not a manufacturable weld assembly.
    if(in.JointMode=="Weld"){
        ForPlateIndices(plates.size(),in.MaximumConcurrency,[&](size_t i,size_t){check();try{if(!cuts[i].empty())JointProfile(plates[i],{},cuts[i]);if(!in.KeepOuts.empty())CheckPlateWeb(plates[i],in.JointWeb);}catch(const std::runtime_error&){throw PlateStationError(plates[i].StationIndex,"PLATE_JOINT_DISCONNECTED");}});
        return;
    }
    struct Plan {std::vector<Tab> Tabs;std::exception_ptr Failure;};
    std::vector<Plan> plans(plates.size());
    ForPlateIndices(plates.size(),in.MaximumConcurrency,[&](size_t i,size_t){
        auto& p=plates[i];auto& plan=plans[i];
        try{check();
            try{if(!cuts[i].empty())JointProfile(p,{},cuts[i]);if(!in.KeepOuts.empty())CheckPlateWeb(p,in.JointWeb);}catch(const std::runtime_error&){throw PlateStationError(p.StationIndex,"PLATE_JOINT_DISCONNECTED");}
            std::vector<P2> intervals{{p.Span[0]+edge,p.Span[1]-edge}};
            for(auto cut:footCuts[i]){std::vector<P2> remaining;for(auto range:intervals){if(cut[1]+edge<=range[0]||cut[0]-edge>=range[1])remaining.push_back(range);else{if(cut[0]-edge>range[0])remaining.push_back({range[0],cut[0]-edge});if(cut[1]+edge<range[1])remaining.push_back({cut[1]+edge,range[1]});}}intervals=std::move(remaining);}
            for(auto interval:intervals){const double available=interval[1]-interval[0];if(available<in.TenonWidth-1e-6)continue;
                const int count=1+static_cast<int>(std::floor((available-in.TenonWidth)/in.TenonPitch));if(count>128)throw std::runtime_error("PLATE_JOINT_LIMIT");
                for(int n=0;n<count;++n){check();const double u=(interval[0]+interval[1])/2+(n-(count-1)/2.)*in.TenonPitch;
                    const auto tip=[&](double top){return JointRect(u-in.TenonWidth/2,-in.BaseThickness,u+in.TenonWidth/2,top);};const auto tab=tip(in.JointWeb);
                    if(!in.KeepOuts.empty()&&(!Intersect(PathsD{JointPath(tab)},forbidden.at(p.StationIndex),FillRule::NonZero,6).empty()||!Difference(PathsD{JointPath(JointRect(u-in.TenonWidth/2,0,u+in.TenonWidth/2,in.JointWeb))},PlateMaterial(p),FillRule::NonZero,6).empty()))continue;
                    std::vector<P2> hole;for(auto local:JointRect(u-(in.TenonWidth+in.JointClearance)/2,-in.JointClearance/2,u+(in.TenonWidth+in.JointClearance)/2,in.Thickness+in.JointClearance/2))hole.push_back({p.Frame[0]*local[0]+p.Frame[2]*local[1]+p.Frame[3],p.Frame[4]*local[0]+p.Frame[6]*local[1]+p.Frame[7]});
                    plan.Tabs.push_back({JointPath(tab),Normalize(hole),tip(0)});
                }
            }
        }catch(...){plan.Failure=std::current_exception();}
    });
    // A base mortise must also clear all earlier mortises. Preserve this shared
    // check in canonical order; independent slot/tab geometry stays parallel.
    std::vector<PathsD> accepted(plates.size());std::exception_ptr mergeFailure;size_t completed=0;
    for(size_t i=0;i<plates.size();++i){auto& p=plates[i];try{check();
        for(const auto& tab:plans[i].Tabs){
            const auto expanded=InflatePaths(PathsD{JointPath(tab.Hole)},in.JointWeb,JoinType::Miter,EndType::Polygon,2.,6);
            if(!Difference(expanded,PlateMaterial(base),FillRule::NonZero,6).empty()){if(!in.KeepOuts.empty())continue;throw std::runtime_error("PLATE_MORTISE_MINIMUM_WEB");}
            if(base.Holes.size()>=512)throw std::runtime_error("PLATE_JOINT_LIMIT");base.Holes.push_back(tab.Hole);accepted[i].push_back(tab.Material);++next;
            p.Joints.push_back({"BaseTenon",next,0,true,tab.Boundary});base.Joints.push_back({"BaseMortise",next,p.StationIndex,false,tab.Hole});
        }
        if(plans[i].Failure)std::rethrow_exception(plans[i].Failure);
        if(accepted[i].empty())throw PlateStationError(p.StationIndex,"PLATE_TENON_NO_SPACE");++completed;
    }catch(...){mergeFailure=std::current_exception();break;}}
    ForPlateIndices(completed,in.MaximumConcurrency,[&](size_t i,size_t){check();try{JointProfile(plates[i],accepted[i],{});}catch(const std::runtime_error&){throw PlateStationError(plates[i].StationIndex,"PLATE_JOINT_DISCONNECTED");}});
    if(mergeFailure)std::rethrow_exception(mergeFailure);
}
