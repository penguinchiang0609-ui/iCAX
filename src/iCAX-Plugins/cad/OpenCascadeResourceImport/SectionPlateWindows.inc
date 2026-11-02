// Closed planar openings derived from the finished profile. These geometric
// clearances preserve material; they are not strength or cutting-beam checks.
void ValidateWindows(const SectionPlateInput& in){
    if(in.WindowMode!="None"&&in.WindowMode!="Automatic")throw std::invalid_argument("PLATE_WINDOW_MODE_INVALID");
    if(in.WindowMode=="Automatic"&&(!std::isfinite(in.WindowWeb)||in.WindowWeb<in.Thickness||in.WindowWeb>500))throw std::invalid_argument("PLATE_WINDOW_WEB_INVALID");
}
void ApplyPlateWindows(const SectionPlateInput& in,SectionPlate& plate,const std::vector<SectionPlate>& plates,const std::function<void()>& check){
    if(in.WindowMode=="None"||(plate.Base&&in.SolidBase))return;
    const double web=std::max({in.WindowWeb,in.MinimumWeb,plate.Thickness,in.JointMode=="TabSlot"?in.JointWeb:0.}),tolerance=.0001;
    const PathsD outline{JointPath(plate.Outline)};
    auto available=InflatePaths(outline,-web-tolerance,JoinType::Miter,EndType::Polygon,2.,6);
    if(available.empty())return;
    double bottom=1e100,top=-1e100;for(auto p:plate.Outline){bottom=std::min(bottom,p[1]);top=std::max(top,p[1]);}
    PathsD reserved;
    if(plate.Base){
        // Every support retains a continuous seating band. This also covers
        // tab roots whose mortises occupy only short sections of that band.
        for(const auto& support:plates){check();const auto& m=support.Frame;PathD footprint;
            for(auto p:JointRect(support.Span[0],0,support.Span[1],support.Thickness))footprint.emplace_back(m[0]*p[0]+m[2]*p[1]+m[3],m[4]*p[0]+m[6]*p[1]+m[7]);
            auto band=InflatePaths(PathsD{footprint},web+tolerance,JoinType::Miter,EndType::Polygon,2.,6);reserved.insert(reserved.end(),band.begin(),band.end());
        }
    }else if(in.JointMode=="Weld"){
        const auto& station=in.Stations[plate.StationIndex];
        for(const auto& other:plates){check();const auto& crossing=in.Stations[other.StationIndex];
            if(crossing.AlongX==station.AlongX||crossing.Coordinate+in.Thickness/2<plate.Span[0]||crossing.Coordinate-in.Thickness/2>plate.Span[1]||station.Coordinate+in.Thickness/2<other.Span[0]||station.Coordinate-in.Thickness/2>other.Span[1])continue;
            reserved.push_back(JointPath(JointRect(crossing.Coordinate-in.Thickness/2-web-tolerance,bottom-web,crossing.Coordinate+in.Thickness/2+web+tolerance,top+web)));
        }
    }
    for(const auto& joint:plate.Joints){check();
        auto boundary=joint.Boundary;
        if(joint.Type=="LapTop"||joint.Type=="LapBottom"){
            // A crossing remains a full-height post, including the material
            // immediately below or above the half-lap seating surface.
            double first=1e100,last=-1e100;for(auto p:boundary){first=std::min(first,p[0]);last=std::max(last,p[0]);}
            boundary=JointRect(first,bottom,last,top);
        }
        auto expanded=InflatePaths(PathsD{JointPath(boundary)},web+tolerance,JoinType::Miter,EndType::Polygon,2.,6);
        reserved.insert(reserved.end(),expanded.begin(),expanded.end());
    }
    for(const auto& hole:plate.Holes){auto band=InflatePaths(PathsD{JointPath(hole)},web+tolerance,JoinType::Miter,EndType::Polygon,2.,6);reserved.insert(reserved.end(),band.begin(),band.end());}
    if(!reserved.empty())available=Difference(available,Union(reserved,FillRule::NonZero,6),FillRule::NonZero,6);
    if(available.empty())return;
    if(in.CountLayout){
        // A compact aperture, not a lightened outline: retain most material
        // and every full-height crossing post. Bounds are deliberately capped.
        const double wantedWidth=std::min(plate.Connection?120.:80.,(plate.Span[1]-plate.Span[0])*.45);
        const double wantedHeight=std::min(plate.Connection?12.:25.,(top-std::max(0.,bottom))*.35);
        const double centerX=(plate.Span[0]+plate.Span[1])/2,centerY=(top+std::max(0.,bottom))/2;
        const auto material=PlateMaterial(plate);PathD selected;double bestArea=0,bestDistance=1e100;
        for(const auto& region:available){check();if(!IsPositive(region))continue;const auto bounds=GetBounds(region);
            for(double scale:{1.,.8,.6}){const double width=std::min(wantedWidth,bounds.Width()-2*tolerance)*scale,height=std::min(wantedHeight,bounds.Height()-2*tolerance)*scale;
                if(std::min(width,height)<=2*plate.Thickness+.01)continue;
                for(double fy:{.5,.25,.75,.1,.9})for(double fx:{.5,.25,.75}){check();
                    const double x=bounds.left+width/2+(bounds.Width()-width)*fx,y=bounds.top+height/2+(bounds.Height()-height)*fy;
                    const auto aperture=JointPath(JointRect(x-width/2,y-height/2,x+width/2,y+height/2));const PathsD opening{aperture};
                    if(!Difference(opening,available,FillRule::NonZero,6).empty()||!Difference(InflatePaths(opening,web,JoinType::Miter,EndType::Polygon,2.,6),material,FillRule::NonZero,6).empty())continue;
                    const double area=width*height,distance=(x-centerX)*(x-centerX)+(y-centerY)*(y-centerY);
                    if(area>bestArea+1e-8||(std::abs(area-bestArea)<=1e-8&&distance<bestDistance)){selected=aperture;bestArea=area;bestDistance=distance;}
                }
            }
        }
        available=selected.empty()?PathsD{}:PathsD{std::move(selected)};
    }else{
    // Bound the unsupported opening span while retaining intermediate ribs.
    // Four times the chosen band is a local geometric design rule, not a
    // strength calculation or an algorithm attributed to another CAM system.
    const auto bounds=GetBounds(available);const double maximumSpan=4*web,rib=web+tolerance;
    const auto cells=[&](double first,double last){const double length=last-first,required=std::ceil((length+rib)/(maximumSpan+rib));if(!std::isfinite(required)||required>8192)throw std::runtime_error("PLATE_WINDOW_LIMIT");const int count=std::max(1,static_cast<int>(required));const double width=(length-(count-1)*rib)/count;std::vector<P2> result;for(int i=0;i<count;++i)result.push_back({first+i*(width+rib),first+i*(width+rib)+width});return result;};
    const auto xs=cells(bounds.left,bounds.right),ys=cells(bounds.top,bounds.bottom);
    if(xs.size()*ys.size()>8192)throw std::runtime_error("PLATE_WINDOW_LIMIT");
    PathsD divided;for(auto x:xs)for(auto y:ys){check();auto pieces=Intersect(available,PathsD{JointPath(JointRect(x[0],y[0],x[1],y[1]))},FillRule::NonZero,6);divided.insert(divided.end(),pieces.begin(),pieces.end());}
    available=std::move(divided);
    }
    // Stable ordering makes the conservative minimum-web rejection below
    // deterministic even when several disconnected inset regions are found.
    std::sort(available.begin(),available.end(),[](const auto& a,const auto& b){const auto aa=std::abs(Area(a)),ab=std::abs(Area(b));if(std::abs(aa-ab)>1e-8)return aa>ab;const auto boundsA=GetBounds(a),boundsB=GetBounds(b);return boundsA.left!=boundsB.left?boundsA.left<boundsB.left:boundsA.top<boundsB.top;});
    PathsD holes;for(const auto& hole:plate.Holes)holes.push_back(JointPath(hole));
    for(const auto& candidate:available){check();if(!IsPositive(candidate)||candidate.size()<3)continue;
        const PathsD opening{candidate};
        // A cut must be a simple, wholly available region, never a loop around
        // a reserved island. Discard slivers too narrow for the plate thickness.
        if(!Difference(opening,available,FillRule::NonZero,6).empty()||InflatePaths(opening,-plate.Thickness,JoinType::Miter,EndType::Polygon,2.,6).empty())continue;
        const auto expanded=InflatePaths(opening,web,JoinType::Miter,EndType::Polygon,2.,6);
        if(!Difference(expanded,outline,FillRule::NonZero,6).empty()||(!holes.empty()&&!Intersect(expanded,holes,FillRule::NonZero,6).empty()))continue;
        auto proposed=holes;proposed.push_back(candidate);const auto remaining=Difference(outline,proposed,FillRule::NonZero,6);
        if(std::count_if(remaining.begin(),remaining.end(),[](const auto& path){return IsPositive(path);})!=1||remaining.size()!=proposed.size()+1)throw std::runtime_error("PLATE_WINDOW_DISCONNECTED");
        std::vector<P2> loop;for(auto p:candidate)loop.push_back({p.x,p.y});plate.WindowHoleIndices.push_back(plate.Holes.size());plate.Holes.push_back(Normalize(loop));holes.push_back(JointPath(plate.Holes.back()));
        if(in.CountLayout)break;
    }
}
