// Select a sparse frame from already checked profiles. Spare stations have no
// final solid/export cost and let a missing contact or joint use a neighbouring position.
std::vector<size_t> SkeletonSequence(std::vector<std::pair<double,size_t>> available,
                                   double maximumSpan,double first,double last,bool bearing){
    if(available.size()<2)throw std::runtime_error("PLATE_SKELETON_INSUFFICIENT");
    std::sort(available.begin(),available.end());
    if(bearing&&(available.front().first-first>maximumSpan/2+1e-6||last-available.back().first>maximumSpan/2+1e-6))
        throw std::runtime_error("PLATE_SKELETON_INSUFFICIENT");
    const double required=bearing?last-maximumSpan/2:available.back().first;
    std::vector<size_t> selected{available.front().second};size_t current=0;
    while(available[current].first<required-1e-6||selected.size()<2){
        size_t next=current;
        while(next+1<available.size()&&available[next+1].first-available[current].first<=maximumSpan+1e-6)++next;
        if(next==current)throw std::runtime_error("PLATE_SKELETON_INSUFFICIENT");
        current=next;selected.push_back(available[current].second);
    }
    return selected;
}
std::vector<SectionPlate> SelectCountSkeletonPlates(const SectionPlateInput& in,const std::vector<SectionPlate>& candidates){
    using Groups=std::vector<std::vector<size_t>>;
    Groups bearings(in.SupportCount),rails(in.ConnectionCount);
    const auto displacement=[&](size_t index){const auto& s=in.Stations[candidates[index].StationIndex];return std::abs(s.Coordinate-s.NominalCoordinate);};
    for(size_t i=0;i<candidates.size();++i){const auto& p=candidates[i];const auto& s=in.Stations[p.StationIndex];
        auto& groups=p.Connection?rails:bearings;if(s.LayoutIndex>=groups.size())throw std::invalid_argument("PLATE_COUNT_INVALID");
        if(p.Connection||!p.Contacts.empty())groups[s.LayoutIndex].push_back(i);
    }
    const auto order=[&](Groups& groups){for(auto& group:groups){if(group.empty())throw std::runtime_error("PLATE_COUNT_LAYOUT_INFEASIBLE");std::stable_sort(group.begin(),group.end(),[&](auto a,auto b){return displacement(a)<displacement(b);});}};
    order(bearings);order(rails);
    const auto crosses=[&](size_t b,size_t r){const auto& p=candidates[b];const auto& rail=candidates[r];const auto& bs=in.Stations[p.StationIndex];const auto& rs=in.Stations[rail.StationIndex];const double band=in.Thickness/2+in.JointWeb+in.JointClearance/2;
        return rs.Coordinate>p.Span[0]+band&&rs.Coordinate<p.Span[1]-band&&bs.Coordinate>rail.Span[0]+band&&bs.Coordinate<rail.Span[1]-band;};
    std::vector<size_t> selectedRails,answer;size_t attempts=0;
    std::function<bool(size_t,const Groups&)> choose=[&](size_t group,const Groups& available){
        if(++attempts>4096)return false;
        if(group==rails.size()){answer=selectedRails;for(const auto& choices:available)answer.push_back(choices.front());return true;}
        for(auto rail:rails[group]){auto next=available;bool possible=true;for(auto& choices:next){choices.erase(std::remove_if(choices.begin(),choices.end(),[&](auto b){return !crosses(b,rail);}),choices.end());if(choices.empty()){possible=false;break;}}
            if(possible){selectedRails.push_back(rail);if(choose(group+1,next))return true;selectedRails.pop_back();}}
        return false;
    };
    if(!choose(0,bearings))throw std::runtime_error("PLATE_COUNT_LAYOUT_INFEASIBLE");
    std::sort(answer.begin(),answer.end());std::vector<SectionPlate> result;for(auto i:answer)result.push_back(candidates[i]);return result;
}
std::vector<SectionPlate> SelectSkeletonPlates(const SectionPlateInput& in,const std::vector<SectionPlate>& candidates,
                                              double x0,double y0,double x1,double y1){
    if(in.CountLayout)return SelectCountSkeletonPlates(in,candidates);
    const auto primary=std::find_if(in.Stations.begin(),in.Stations.end(),[](const auto& s){return !s.Connection;});
    if(primary==in.Stations.end())throw std::runtime_error("PLATE_SKELETON_INSUFFICIENT");
    std::vector<std::pair<double,size_t>> bearings;
    for(size_t i=0;i<candidates.size();++i)if(!candidates[i].Connection&&!candidates[i].Contacts.empty())
        bearings.emplace_back(in.Stations[candidates[i].StationIndex].Coordinate,i);
    auto selected=SkeletonSequence(std::move(bearings),in.MaximumSupportSpan,primary->AlongX?x0:y0,primary->AlongX?x1:y1,true);
    std::vector<std::pair<double,size_t>> connections;
    for(size_t i=0;i<candidates.size();++i)if(candidates[i].Connection){
        const auto& rail=candidates[i];const auto& station=in.Stations[rail.StationIndex];size_t crossings=0;
        for(auto index:selected){const auto& plate=candidates[index];const auto& support=in.Stations[plate.StationIndex];
            const double band=in.Thickness/2+in.JointWeb+in.JointClearance/2;
            if(station.Coordinate>plate.Span[0]+band&&station.Coordinate<plate.Span[1]-band&&
               support.Coordinate>rail.Span[0]+band&&support.Coordinate<rail.Span[1]-band)++crossings;
        }
        if(crossings==selected.size())connections.emplace_back(station.Coordinate,i);
    }
    const auto rails=SkeletonSequence(std::move(connections),2*in.MaximumSupportSpan,0,0,false);
    selected.insert(selected.end(),rails.begin(),rails.end());std::sort(selected.begin(),selected.end());
    std::vector<SectionPlate> result;for(auto index:selected)result.push_back(candidates[index]);return result;
}
void RecordSkeletonCoverage(const SectionPlateInput& in,const std::vector<SectionPlate>& plates,SectionPlate& base,
                            double x0,double y0,double x1,double y1){
    std::vector<double> coordinates;bool alongX=true;
    for(const auto& plate:plates)if(!plate.Connection&&!plate.Contacts.empty()){
        const auto& station=in.Stations[plate.StationIndex];alongX=station.AlongX;coordinates.push_back(station.Coordinate);
    }
    if(coordinates.size()<2)throw std::runtime_error("PLATE_SKELETON_INSUFFICIENT");
    std::sort(coordinates.begin(),coordinates.end());
    for(size_t i=1;i<coordinates.size();++i)base.MaximumUnsupportedSpan=std::max(base.MaximumUnsupportedSpan,coordinates[i]-coordinates[i-1]);
    base.MaximumEndOverhang=std::max(coordinates.front()-(alongX?x0:y0),(alongX?x1:y1)-coordinates.back());
    if(in.CountLayout){
        if(coordinates.size()!=in.SupportCount||std::count_if(plates.begin(),plates.end(),[](const auto& p){return p.Connection;})!=in.ConnectionCount)throw std::runtime_error("PLATE_COUNT_LAYOUT_INFEASIBLE");
    }else if(base.MaximumUnsupportedSpan>in.MaximumSupportSpan+1e-6||base.MaximumEndOverhang>in.MaximumSupportSpan/2+1e-6)
        throw std::runtime_error("PLATE_SKELETON_INSUFFICIENT");
}
