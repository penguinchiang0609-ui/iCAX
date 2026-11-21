// Include inside api.cpp's anonymous namespace, after readPoses/publishMulti.
// api_material_types.hpp must have been included at global C++ scope.
bool apiNeedsMaterial(Session &s,std::span<const Id> stocks,std::span<const Id> instances={}) {
    if(s.materialGap)return true;
    for(const auto stock:stocks)if(lookup(s.sheets,stock).materialRoute)return true;
    for(const auto instance:instances)if(lookup(s.parts,instance).materialRoute)return true;
    return false;
}
bool apiNeedsMaterial(Session &s,Id stock,const PN2_Part *parts,int count) {
    require(count>=0&&count<=10000&&(!count||parts),PN2S_INVALID,"Invalid material demand array");
    if(s.materialGap||lookup(s.sheets,stock).materialRoute)return true;
    for(int index=0;index<count;++index)if(lookup(s.parts,id(parts[index])).materialRoute)return true;
    return false;
}
Polygon apiMaterialRing(const PN2_Point *points,int count) {
    require(points&&count>=3&&count<=8193,PN2S_INVALID,"Material contour requires 3..8192 vertices");
    Polygon ring(points,points+count);
    if(ring.size()>3&&ring.front().x==ring.back().x&&ring.front().y==ring.back().y)ring.pop_back();
    require(ring.size()<=8192,PN2S_INVALID,"Material contour vertex limit");
    for(const auto point:ring)validateContourPoint(point);
    if(area(ring)<0)std::reverse(ring.begin(),ring.end());
    icax::iplane::geometry::validateMaterial(ring,{});
    return ring;
}
Polygon apiMaterialOuter(const Sheet &sheet) {
    if(!sheet.outer.empty())return sheet.outer;
    return {{0,0},{sheet.width,0},{sheet.width,sheet.height},{0,sheet.height}};
}
icax::iplane::geometry::Board apiMaterialBoard(const Sheet &sheet) {
    return {apiMaterialOuter(sheet),sheet.defects,sheet.uniformBorderGap,sheet.defectOffsets};
}
Id apiMaterialAddSheet(Session &s,Polygon polygon,double borderGap,std::optional<Box> originalBounds={}) {
    require(std::isfinite(borderGap)&&borderGap>=0&&borderGap<=1e6,PN2S_INVALID,"Invalid polygonal sheet border gap");
    icax::iplane::geometry::validateMaterial(polygon,{});
    const auto box=originalBounds.value_or(bounds(polygon));
    require(box.x0==0&&box.y0==0,PN2S_INVALID,"Polygonal sheet bounding-box origin must be (0,0)");
    require(box.x1>0&&box.y1>0&&box.x1<=1e6&&box.y1<=1e6,PN2S_INVALID,"Invalid polygonal sheet dimensions");
    require(s.sheets.size()<10000,PN2S_RESOURCE_LIMIT,"Sheet limit");
    Sheet sheet{box.x1,box.y1};sheet.outer=std::move(polygon);sheet.uniformBorderGap=borderGap;sheet.materialRoute=true;
    const auto key=token();s.sheets.emplace(key,std::move(sheet));return key;
}
Id apiMaterialContourSheet(Session &s,PN2_Contour contour,double borderGap) {
    const auto &source=lookup(s.contours,id(contour));
    const auto exact=recovered::richStraightBounds(*source.curve);
    require(!exact.empty,PN2S_INVALID,"Empty sheet contour");
    // Stock is sampled inwards; exclusions are sampled outwards below.
    auto polygon=contourPolygon(source,false);
    return apiMaterialAddSheet(s,std::move(polygon),borderGap,
        Box{exact.coordinates[0],exact.coordinates[1],exact.coordinates[2],exact.coordinates[3]});
}
void apiMaterialAddDefect(Session &s,Id sheetId,Polygon polygon,double offset) {
    require(std::isfinite(offset)&&offset>=0&&offset<=1e6,PN2S_INVALID,"Invalid sheet defect offset");
    auto replacement=lookup(s.sheets,sheetId);
    require(replacement.defects.size()<512,PN2S_RESOURCE_LIMIT,"Maximum 512 sheet defects");
    const auto outer=apiMaterialOuter(replacement);
    // The live sheet already owns strictly validated geometry. The shared
    // incremental validator checks the new ring, its new relationships and
    // complete-material area without rechecking the old outer or old pairs.
    icax::iplane::geometry::validateAdditionalHole(outer,replacement.defects,polygon);
    replacement.defects.push_back(std::move(polygon));replacement.defectOffsets.push_back(offset);
    replacement.materialRoute=true;lookup(s.sheets,sheetId)=std::move(replacement);
}
struct ApiMaterialDemand {
    Input input;
    std::vector<std::vector<Id>> instances;
};
ApiMaterialDemand apiMaterialDemand(Session &s,std::span<const Id> instances,Budget &budget) {
    require(instances.size()<=500,PN2S_RESOURCE_LIMIT,"Material route supports at most 500 concrete parts");
    require(!s.lOffcut&&s.multiheads.hardware.heads48<2,PN2S_UNSUPPORTED,
        "Material route does not implement L-offcut or simultaneous cutting-head technology");
    require(std::isfinite(s.gap)&&s.gap>=0&&s.gap<=1e6,PN2S_INVALID,"Invalid material gap");
    ApiMaterialDemand demand;demand.input.gap=s.gap;demand.input.randomSeed=s.randomSeed;
    std::vector<Part> types;std::set<Id> unique;
    for(const auto key:instances){
        budget.check(true);require(unique.insert(key).second,PN2S_INVALID,"Repeated material part instance");
        const auto &part=lookup(s.parts,key);const auto &shape=lookup(s.shapes,part.shape);
        require(shape.protection==0,PN2S_UNSUPPORTED,"Material route protection-offset technology is not connected");
        size_t group=0;for(;group<types.size()&&!same(types[group],part);++group){}
        if(group==types.size()){
            require(!part.poses.empty()&&part.poses.size()<=64,PN2S_INVALID,"Material route requires 1..64 orientation constraints");
            icax::iplane::geometry::validateMaterial(shape.polygon,shape.holes,&budget);
            types.push_back(part);demand.instances.emplace_back();
            demand.input.parts.push_back({shape.polygon,shape.pivot,part.poses,0,shape.holes,shape.protection});
        }
        demand.instances[group].push_back(key);++demand.input.parts[group].quantity;
    }
    return demand;
}
std::shared_ptr<const MaterialResultMetadata> apiMaterialMetadata(const icax::iplane::batch::Result &source) {
    auto info=std::make_shared<MaterialResultMetadata>();info->stopped=source.stopped;
    info->trials=uint64_t(source.statistics.constructionTrials)+uint64_t(source.statistics.replacementTrials)+
        uint64_t(source.statistics.consolidationTrials)+uint64_t(source.statistics.repackTrials);
    std::set<std::string> seen;
    for(const auto &warning:source.warnings)if(!warning.empty()&&seen.insert(warning).second){
        if(!info->warning.empty())info->warning+='\n';info->warning+=warning;
    }
    if(info->stopped&&info->warning.empty())info->warning="Material search stopped after retaining verified placements.";
    return info;
}
void apiMaterialRequireEvaluated(const icax::iplane::batch::Result &source,const MaterialResultMetadata &,Budget &budget) {
    if(!source.boards.empty()||!source.stopped)return;
    // Valid input can exhaust a child search before finding an accepted part.
    // Publish its explicit stopped/all-unplaced outcome only while the caller's
    // budget still permits publication; this makes no claim of infeasibility.
    budget.check(true);
}
Result apiMaterialResult(Session &s,Id sheetId,const ApiMaterialDemand &demand,const Output &output,
    const std::shared_ptr<const MaterialResultMetadata> &metadata,std::set<Id> &seen) {
    Result result;result.sheet=sheetId;result.materialInfo=metadata;
    for(const auto &placed:output.placements){
        require(placed.group<demand.instances.size()&&placed.ordinal<demand.instances[placed.group].size(),
            PN2S_INTERNAL,"Material optimizer returned an unknown concrete instance");
        const auto &part=demand.input.parts[placed.group];
        require(placed.poseIndex<part.poses.size(),PN2S_INTERNAL,"Material optimizer returned an unknown orientation");
        auto pose=part.poses[placed.poseIndex];
        const auto angle=std::isfinite(placed.solvedAngle)?placed.solvedAngle:pose.angle;
        require(std::isfinite(angle)&&angle>=pose.angle-1e-8&&angle<=pose.upper+1e-8,
            PN2S_INTERNAL,"Material optimizer exceeded its source angular interval");pose.angle=angle;
        const Point position{std::isfinite(placed.exactX)?placed.exactX:double(placed.x)/1e6,
                             std::isfinite(placed.exactY)?placed.exactY:double(placed.y)/1000.};
        require(std::isfinite(position.x)&&std::isfinite(position.y),PN2S_INTERNAL,"Material optimizer returned a nonfinite translation");
        const auto concrete=demand.instances[placed.group][placed.ordinal];
        require(seen.insert(concrete).second,PN2S_INTERNAL,"Material optimizer repeated a concrete instance");
        result.placed.push_back({concrete,position,{int(pose.flip),angle,angle}});
        for(const auto point:transform(part.polygon,part.pivot,pose))result.length=std::max(result.length,point.x+position.x);
    }
    (void)s;return result;
}
Result apiMaterialSolve(Session &s,Id sheetId,const PN2_Part *parts,int count,Budget &budget) {
    require(count>=0&&count<=500&&(!count||parts),PN2S_INVALID,"Invalid material demand size");
    std::vector<Id> ids;ids.reserve(size_t(count));for(int index=0;index<count;++index)ids.push_back(id(parts[index]));
    const auto demand=apiMaterialDemand(s,ids,budget);
    auto board=apiMaterialBoard(lookup(s.sheets,sheetId));
    icax::iplane::geometry::validateMaterial(board.outer,board.holes,&budget);
    const auto optimized=icax::iplane::batch::optimize(demand.input,{{std::move(board),1}},{},budget);
    const auto metadata=apiMaterialMetadata(optimized);apiMaterialRequireEvaluated(optimized,*metadata,budget);
    require(optimized.boards.size()<=1,PN2S_INTERNAL,"Single-sheet material solve used additional stock");
    std::set<Id> seen;
    if(optimized.boards.empty())return apiMaterialResult(s,sheetId,demand,{},metadata,seen);
    require(optimized.boards.front().stock==0&&optimized.boards.front().stockOrdinal==0,PN2S_INTERNAL,"Single material stock identity changed");
    return apiMaterialResult(s,sheetId,demand,optimized.boards.front().output,metadata,seen);
}
Id apiMaterialMulti(Session &s,const std::vector<Id> &stocks,const std::vector<int> &inventory,
    const std::vector<Id> &instances,Budget &budget) {
    require(!stocks.empty()&&stocks.size()<=100&&stocks.size()==inventory.size(),PN2S_INVALID,"Invalid material stock inventory");
    const auto demand=apiMaterialDemand(s,instances,budget);
    std::vector<icax::iplane::batch::Stock> stock;std::set<Id> unique;size_t physicalCount=0;
    for(size_t index=0;index<stocks.size();++index){
        require(unique.insert(stocks[index]).second&&inventory[index]>=0&&inventory[index]<=100,PN2S_INVALID,"Invalid or repeated material stock type");
        physicalCount+=size_t(inventory[index]);require(physicalCount<=100,PN2S_RESOURCE_LIMIT,"Material route supports at most 100 physical boards");
        auto board=apiMaterialBoard(lookup(s.sheets,stocks[index]));
        icax::iplane::geometry::validateMaterial(board.outer,board.holes,&budget);stock.push_back({std::move(board),inventory[index]});
    }
    const auto optimized=icax::iplane::batch::optimize(demand.input,stock,{},budget);
    const auto metadata=apiMaterialMetadata(optimized);apiMaterialRequireEvaluated(optimized,*metadata,budget);
    std::set<Id> seen;std::set<std::pair<size_t,size_t>> physical;std::vector<Result> boards;
    for(const auto &board:optimized.boards){
        require(board.stock<stocks.size()&&board.stockOrdinal<size_t(inventory[board.stock])&&
            physical.emplace(board.stock,board.stockOrdinal).second,PN2S_INTERNAL,"Material optimizer exceeded concrete stock inventory");
        boards.push_back(apiMaterialResult(s,stocks[board.stock],demand,board.output,metadata,seen));
    }
    Multi multi;multi.materialInfo=metadata;
    for(const auto [group,ordinal]:optimized.unplaced){
        require(group<demand.instances.size()&&ordinal<demand.instances[group].size(),PN2S_INTERNAL,"Material optimizer returned unknown missing demand");
        const auto key=demand.instances[group][ordinal];require(seen.insert(key).second,PN2S_INTERNAL,"Material missing demand duplicates a placed instance");multi.unplaced.push_back(key);
    }
    require(seen==std::set<Id>(instances.begin(),instances.end()),PN2S_INTERNAL,"Material batch did not conserve all concrete SDK identities");
    return publishMulti(s,std::move(boards),std::move(multi));
}
