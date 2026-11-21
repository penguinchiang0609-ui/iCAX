#include "core.hpp"
#include "recovered_offset.hpp"
#include "recovered_catalog.hpp"
#include "ordinary_runtime.hpp"
#include "recovered_extremal.hpp"
#include "recovered_engine.hpp"
#include "recovered_shaker_terrain.hpp"
#include "ordinary_nomap.hpp"
#include "recovered_part_geometry.hpp"
#include "ordinary_pair_map.hpp"
#include "recovered_part_normalization.hpp"
namespace study {
namespace {
// Physical Y positions in this host use 1e-3 ticks. A fractional sheet or
// multihead lane must end at the last tick inside its actual boundary.
int64_t stockUpperY(double maximum){return static_cast<int64_t>(std::floor((maximum+1e-7)*1000.));}
}
recovered::ShakerTerrain ordinaryTerrain(const Input &input,const Output &output,int64_t pitch,Budget &budget,int32_t minimum) {
    require(pitch>0,PN2S_INVALID,"Shaker terrain requires a positive context pitch");
    const double strips=std::ceil(input.width/(double(pitch)/1e6));
    require(strips>=1 && strips<=200000,PN2S_RESOURCE_LIMIT,"Shaker terrain strip-count limit");
    const double rawUpper=std::ceil((input.height-1e-7)*1000.);
    require(rawUpper>=0 && rawUpper<=INT32_MAX,PN2S_UNSUPPORTED,"Shaker terrain Y range");
    const int32_t upper=int32_t(rawUpper);Rows free(size_t(strips),Row{{0,upper}});
    std::map<std::pair<size_t,size_t>,Raster> footprints;
    for(const auto &p:output.placements) {
        budget.check();const auto &part=input.parts.at(p.group);
        require(part.protection==0,PN2S_UNSUPPORTED,"Ordinary terrain requires zero protection and a prepared single layer");
        auto pose=part.poses.at(p.poseIndex);if(std::isfinite(p.solvedAngle))pose.angle=p.solvedAngle;
        const auto polygon=transform(part.polygon,part.pivot,pose);const auto box=bounds(polygon);
        const auto key=std::pair{p.group,p.poseIndex};auto found=footprints.find(key);
        if(found==footprints.end()) {
            auto contours=ordinaryPreparedContours(part,pose);for(auto &ring:contours)for(auto &q:ring){q.x-=box.x0;q.y-=box.y0;}
            found=footprints.emplace(key,rasterFootprint(contours,pitch,budget)).first;
        }
        const int64_t normalizedX=p.x+rounded(box.x0*1e6);int64_t strip=normalizedX/pitch;
        if(normalizedX<0 && normalizedX%pitch)--strip;
        const int64_t y=p.y+rounded(box.y0*1000.);
        for(size_t row=0;row<found->second.rows.size();++row) {
            const int64_t index=strip+int64_t(row);if(index<0 || uint64_t(index)>=free.size())continue;
            Row shifted;for(const auto &interval:found->second.rows[row])shifted.push_back({interval[0]+y,interval[1]+y});
            free[size_t(index)]=difference(free[size_t(index)],shifted);
        }
    }
    return recovered::shakerTerrainFromFreeRows(free,0,upper,minimum);
}
int ordinaryRun(const Input &input, Budget &budget, OrdinaryState &state, const OrdinaryControl *control, ReferenceProbe *probe) {
    require(input.gap == 0, PN2S_UNSUPPORTED, "Map/Greedy supports zero spacing only");
    if(!input.pointGroups.empty())validatePointsProximityInput(input);
    struct Geometry {
        Polygon points, normalized;
        std::vector<Polygon> contours,normalizedContours;
        double width, height, value;
        int64_t ox, oy;
        size_t group, pose;
    };
    std::vector<Geometry> geometries;
    std::vector<recovered::ValuePart> valueParts;
    std::vector<double> minimumWidths;
    std::vector<std::vector<size_t>> catalog(input.parts.size());
    auto geometry = [&](Polygon p, size_t group, size_t pose,std::vector<Polygon> contours={}) {
        auto b = bounds(p);
        Geometry g;g.points=std::move(p);g.width=b.x1-b.x0;g.height=b.y1-b.y0;g.value=area(g.points);
        g.ox=rounded(b.x0*1e6);g.oy=rounded(b.y0*1000);g.group=group;g.pose=pose;
        g.contours=contours.empty()?std::vector<Polygon>{g.points}:std::move(contours);
        for(const auto &ring:g.contours){Polygon normalized;for(const auto q:ring)normalized.push_back({q.x-b.x0,q.y-b.y0});g.normalizedContours.push_back(std::move(normalized));}
        g.normalized=g.normalizedContours.front();
        geometries.push_back(std::move(g));
        return geometries.size() - 1;
    };
    int total = 0;
    double totalArea = 0, meanW = 0, meanH = 0, widthSum = 0;
    for (size_t t = 0; t < input.parts.size(); t++) {
        auto &p = input.parts[t];
        require(p.protection==0,PN2S_UNSUPPORTED,"Ordinary prepared geometry requires zero protection");
        auto b = bounds(p.polygon);
        total += p.quantity;
        double a = material_area(p);
        totalArea += a * p.quantity;
        widthSum += (b.x1 - b.x0) * p.quantity;
        double ws = 0, hs = 0;
        recovered::ValuePart vp;
        vp.area = a;
        vp.bboxArea = (b.x1 - b.x0) * (b.y1 - b.y0);
        vp.quantity = p.quantity;
        for (size_t j = 0; j < p.polygon.size(); ++j) {
            auto u = p.polygon[j], v = p.polygon[(j + 1) % p.polygon.size()];
            vp.perimeterField += std::hypot(v.x - u.x, v.y - u.y);
        }
        for(const auto &hole:p.holes)for(size_t j=0;j<hole.size();++j){const auto u=hole[j],v=hole[(j+1)%hole.size()];vp.perimeterField+=std::hypot(v.x-u.x,v.y-u.y);}
        for (size_t i = 0; i < p.poses.size(); i++) {
            require(p.poses[i].upper==p.poses[i].angle,PN2S_UNSUPPORTED,"Ordinary prepared geometry requires fixed poses");
            auto contours=ordinaryPreparedContours(p,p.poses[i]);
            for(const auto &ring:contours)for (auto q : ring)
                if (!grid_exact(q.x, 1e6) || !grid_exact(q.y, 1000))
                    throw Failure(PN2S_UNSUPPORTED,"Pose is not exactly representable on the recovered host grid: group="+
                        std::to_string(t)+", pose="+std::to_string(i)+", x="+std::to_string(q.x)+", y="+std::to_string(q.y));
            auto points=contours.front();
            size_t id = geometry(std::move(points), t, i,std::move(contours));
            geometries[id].value = a;
            catalog[t].push_back(id);
            ws += geometries[id].width;
            hs += geometries[id].height;
            vp.variantSizes.push_back({geometries[id].width, geometries[id].height});
            vp.maxWidth = std::max(vp.maxWidth, geometries[id].width);
            vp.maxHeight = std::max(vp.maxHeight, geometries[id].height);
        }
        meanW += p.quantity * ws / static_cast<double>(p.poses.size());
        meanH += p.quantity * a * hs / static_cast<double>(p.poses.size());
        valueParts.push_back(std::move(vp));
        double minimumWidth=std::numeric_limits<double>::max();
        for(size_t module:catalog[t])minimumWidth=std::min(minimumWidth,geometries[module].width);
        minimumWidths.push_back(catalog[t].empty()?b.x1-b.x0:minimumWidth);
    }
    const size_t partGeometryCount=geometries.size();
    if (total == 0)
        return 0;
    meanW /= total;
    meanH /= totalArea;
    int64_t pitch = input.pitch != 0 ? rounded(input.pitch * 1e6)
                                     : rounded(std::min(input.width / 3, widthSum / total) / 50 * 1e6);
    if (input.pitch == 0 && pitch <= 0)
        pitch = 1000000;
    require(pitch > 0, PN2S_INVALID, "Pitch rounds to zero");
    double step = static_cast<double>(pitch) / 1e6;
    auto rect = [&](double w, double h) { return geometry({{0, 0}, {w, 0}, {w, h}, {0, h}}, 0, 0); };
    recovered::ReferenceKind referenceKind = recovered::ReferenceKind::Normal;
    recovered::Random rng(input.randomSeed);
    if (control) {
        require(control->values.size() == input.parts.size(), PN2S_INVALID, "Explicit Offset values must cover every group");
        require(control->placementLimit >= -1 && control->extremaCount > 0, PN2S_INVALID, "Invalid explicit Offset limits");
        referenceKind = control->reference;
        for (auto &g : geometries) g.value = control->values[g.group];
    } else if (input.recoveredOffset) {
        referenceKind = recovered::selectReference(rng.unit());
        auto complexity = recovered::drawComplexity([&] { return rng.unit(); });
        auto weighted = recovered::values(valueParts, totalArea, complexity);
        for (auto &g : geometries) g.value = weighted[g.group];
    }
    const auto mean = recovered::effectiveAverage(valueParts, totalArea, {meanW, meanH});
    auto referencePoints = control && control->referenceGeometry?*control->referenceGeometry:
        recovered::referenceShape(mean, input.height, referenceKind);
    if (control) for (auto &point : referencePoints) {
        point.x *= control->referenceScale; point.y *= control->referenceScale;
    }
    size_t primary = geometry(std::move(referencePoints), 0, 0),
           secondary = rect(4 * step, step);
    std::set<size_t> extendedReferenceIds;
    if(referenceKind==recovered::ReferenceKind::Normal &&
        (!control || (!control->referenceGeometry && control->referenceScale==1 && control->extendReferenceSheet)))
        extendedReferenceIds.insert(primary);
    std::optional<size_t> auxiliary;
    const bool useAuxiliary=control?control->auxiliaryReference:input.recoveredOffset;
    const double auxiliaryScale=control?control->auxiliaryReferenceScale:1;
    if(useAuxiliary) {
        const double w=((mean.x*.25)*5)*auxiliaryScale,h=((mean.y*.25)*.5)*auxiliaryScale;
        auxiliary=geometry({{0,-h/2},{w,-h/2},{w,h/2},{0,h/2}},0,0);
        if(auxiliaryScale==1)extendedReferenceIds.insert(*auxiliary);
    }
    recovered::ScoreState scoreState;
    struct Placed {
        size_t gid;
        int64_t strip, y, phase = 0;
    };
    std::vector<Placed> placed;
    std::vector<int> used(input.parts.size());
    std::vector<int> quantities;
    for (auto &p : input.parts) quantities.push_back(p.quantity);
    if (!state.catalog) state.catalog.emplace(catalog, quantities);
    auto &recoveredCatalog = *state.catalog;
    if(!state.placementHost) {
        std::vector<size_t> instanceGroups;for(const auto &instance:recoveredCatalog.instances)instanceGroups.push_back(instance.group);
        auto host=std::make_unique<OrdinaryState::PlacementHost>(instanceGroups);host->modules.resize(input.parts.size());
        for(size_t group=0;group<catalog.size();++group)for(size_t module:catalog[group]) {
            const auto &g=geometries[module];const auto box=bounds(g.points);recovered::WarmPlacement prototype;
            prototype.geometryOwner=std::make_shared<const Polygon>(g.points);prototype.area58=material_area(input.parts[group]);
            prototype.leftE0=g.ox;prototype.rightF0=rounded(box.x1*1e6);
            require(g.oy>=INT32_MIN && g.oy<=INT32_MAX && box.y1*1000>=INT32_MIN && box.y1*1000<=INT32_MAX,PN2S_UNSUPPORTED,"Placement bounds exceed original 32-bit Y range");
            prototype.bottomE8=int32_t(g.oy);prototype.topF8=int32_t(rounded(box.y1*1000));
            for(const auto &ring:g.contours)for(size_t j=0;j<ring.size();++j){const auto a=ring[j],b=ring[(j+1)%ring.size()];prototype.geometry60+=std::hypot(b.x-a.x,b.y-a.y);}
            host->modules[group].push_back(std::move(prototype));
        }
        for(const auto &part:input.parts)for(int instance=0;instance<part.quantity;++instance)
            host->requiredArea+=material_area(part);
        require(state.output.placements.empty(),PN2S_INTERNAL,"Uninitialized placement lifecycle with existing output");
        state.placementHost=std::move(host);
    }
    const bool useCatalog = input.recoveredOffset || control;
    std::vector<size_t> keys(input.parts.size());
    std::iota(keys.begin(), keys.end(), 0);
    if (control && (control->keysExplicit || !control->keys.empty())) keys = control->keys;
    auto &out = state.output;
    const size_t initialPlaced = out.placements.size();
    require(initialPlaced == 0 || out.pitchX == pitch, PN2S_INVALID, "Continuation changed grid pitch");
    out.pitchX = pitch;
    for (const auto &p : out.placements) {
        require(p.group < catalog.size() && p.poseIndex < catalog[p.group].size(), PN2S_INTERNAL, "Bad continuation placement");
        const size_t gid = catalog[p.group][p.poseIndex];
        const auto &g = geometries[gid];
        const auto normalizedX = p.x + g.ox;
        auto strip = normalizedX / pitch, phase = normalizedX % pitch;
        if (phase < 0) { --strip; phase += pitch; }
        placed.push_back({gid, strip, p.y + g.oy, phase});
        ++used[p.group];
    }
    std::unique_ptr<OrdinaryNoMapProvider> noMap;
    std::vector<int8_t> noMapDecisions(partGeometryCount,-1);
    auto noMapProvider=[&](size_t requestedModule)->OrdinaryNoMapProvider *{
        if(noMapDecisions.at(requestedModule)==0)return nullptr;
        if(noMapDecisions[requestedModule]<0){
            // Connect sufficient original3e2150 branches using the real
            // Context eligibility cache and original5316a0 geometry producer.
            // Unavailable earlier module/compound flags can only select NoMap
            // sooner in these branches; their values are not invented here.
            if(!state.mapEligible){noMapDecisions[requestedModule]=0;return nullptr;}
            const bool mapEligible=state.mapEligible();
            bool tooComplex=false;
            if(mapEligible)tooComplex=recovered::partGeometryTooComplex(recovered::partGeometryStatistics(geometries[requestedModule].contours));
            if(mapEligible&&!tooComplex){noMapDecisions[requestedModule]=0;return nullptr;}
            OrdinaryPositionnerFactoryInput factory;factory.mapEligible=[mapEligible]{return mapEligible;};
            factory.geometryTooComplex=[tooComplex]{return tooComplex;};
            require(ordinaryPositionnerKind(factory)==OrdinaryPositionnerKind::NoMap,PN2S_INTERNAL,
                    "Original positionner factory rejected its proven NoMap branch");
            noMapDecisions[requestedModule]=1;
        }
        if(noMap)return noMap.get();
        require(input.pointGroups.empty()&&(!control||!control->filterModuleRows),PN2S_UNSUPPORTED,
                "NoMap process-constrained range provider is not restored");
        std::vector<OrdinaryNoMapModule> modules;
        for(size_t module=0;module<partGeometryCount;++module){
            const auto &part=input.parts.at(geometries[module].group);
            require(part.protection==0,PN2S_UNSUPPORTED,"NoMap ordinary bridge requires a zero-protection prepared layer");
            const auto prepared=recovered::partNormalizeCleanContours(geometries[module].contours);
            modules.push_back({recovered::preparedPartModuleGeometry(prepared,std::nullopt,{0,0,0}),int64_t(module)});
        }
        noMap=std::make_unique<OrdinaryNoMapProvider>(OrdinaryNoMapGrid{input.width,input.height,pitch},modules,budget);
        for(const auto &p:out.placements){const auto module=catalog[p.group][p.poseIndex];
            noMap->append({module,p.x,int32_t(p.y),(p.x+geometries[module].ox)%pitch!=0});}
        return noMap.get();
    };
    const bool preparedPairMaps=std::any_of(input.parts.begin(),input.parts.end(),[](const auto &part){return !part.holes.empty();});
    std::unique_ptr<OrdinaryPairMapProvider> preparedPairs;
    std::map<std::tuple<size_t, size_t, int64_t>, Raster> cache;
    auto pairMap = [&](size_t a, size_t b, int64_t phase = 0) -> const Raster & {
        if(preparedPairMaps){
            if(!preparedPairs){std::vector<OrdinaryNoMapModule> modules;modules.reserve(geometries.size());
                for(size_t module=0;module<geometries.size();++module)modules.push_back({recovered::preparedPartModuleGeometry(geometries[module].normalizedContours,std::nullopt,{0,0,0}),int64_t(module)});
                preparedPairs=std::make_unique<OrdinaryPairMapProvider>(modules,pitch,budget);}
            return preparedPairs->pair(a,b,false);
        }
        auto key = std::make_tuple(a, b, phase);
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
        budget.check();
        Polygon minus;
        for (auto q : geometries[b].normalized)
            minus.push_back({-q.x, -q.y});
        auto conv = convolution(geometries[a].normalized, minus, budget);
        if (phase != 0)
            for (auto &ring : conv) for (auto &point : ring) point.x += double(phase) / 1e6;
        return cache.emplace(key, raster(conv, pitch, budget)).first->second;
    };
    auto freeMap = [&](size_t gid, bool excludePlaced = true, std::optional<size_t> ordinal = {}) {
        auto &g = geometries[gid];
        double nd = std::floor((input.width - g.width) / step + 1);
        if (extendedReferenceIds.contains(gid))
            nd = std::floor((input.width + 2 * g.width) / step + 1);
        if (std::abs(input.width - g.width) < 1e-9)
            nd = 1;
        double ymax = input.height - g.height;
        if (ymax < 0 && ymax >= -1e-7)
            ymax = 0;
        if (nd < 1 || ymax < 0)
            return Rows(1);
        require(nd <= 20000, PN2S_RESOURCE_LIMIT, "Map strip-count limit (20000)");
        Rows rows(static_cast<size_t>(nd),
                  Row{{0,stockUpperY(ymax)}});
        if (excludePlaced) for (auto p : placed) {
            const auto &pair = pairMap(p.gid, gid, preparedPairMaps?0:p.phase);
            const int copies=preparedPairMaps&&p.phase!=0?2:1;
            for(int copy=0;copy<copies;++copy){
              int64_t sx = p.strip - pair.offsetStrip+copy, sy = p.y - pair.offsetY;
              for (size_t j = 0; j < pair.rows.size(); j++) {
                budget.check();
                int64_t i = sx + static_cast<int64_t>(j);
                if (i < 0 || i >= static_cast<int64_t>(rows.size()) ||
                    rows[static_cast<size_t>(i)].empty())
                    continue;
                Row shifted;
                for (auto r : pair.rows[j])
                    shifted.push_back({r[0] + sy, r[1] + sy});
                rows[static_cast<size_t>(i)] = difference(rows[static_cast<size_t>(i)], shifted);
              }
            }
        }
        if(gid<partGeometryCount){
            if(!input.pointGroups.empty())filterPointsProximityRows(input,state,g.group,g.pose,ordinal,rows,pitch,g.ox,g.oy);
            if(control && control->filterModuleRows)control->filterModuleRows(g.group,g.pose,ordinal,rows,pitch,g.ox,g.oy);
        }
        return rows;
    };
    auto firstPosition = [&](size_t module,std::optional<size_t> ordinal = {})->std::optional<recovered::FirstPosition> {
        if(module<partGeometryCount)if(auto provider=noMapProvider(module)){
            const auto range=provider->range(module);if(range.first>=range.end)return {};
            return recovered::FirstPosition{range.first,rounded(geometries[module].width*1e6)};
        }
        const auto rows=freeMap(module,true,ordinal);size_t first=0;
        while(first<rows.size()&&rows[first].empty())++first;
        if(first==rows.size())return std::nullopt;
        return recovered::FirstPosition{int64_t(first),rounded(geometries[module].width*1e6)};
    };
    auto maximumRight = [&] {
        int64_t right=0;
        for(const auto &p:placed)right=std::max(right,p.strip*pitch+p.phase+rounded(geometries[p.gid].width*1e6));
        return right;
    };
    const int64_t initialAuxiliaryCount=auxiliary?recovered::closedCount(freeMap(*auxiliary,false)):0;
    auto integral = [&](const Rows &free, const Raster &pair, int64_t strip, int64_t y) {
        int64_t sx = strip - pair.offsetStrip, sy = y - pair.offsetY;
        double length = 0;
        for (size_t j = 0; j < pair.rows.size(); j++) {
            budget.check();
            int64_t i = sx + static_cast<int64_t>(j);
            if (i < 0 || i >= static_cast<int64_t>(free.size()))
                continue;
            const auto &a = free[static_cast<size_t>(i)];
            const auto &b = pair.rows[j];
            size_t ai = 0, bi = 0;
            while (ai < a.size() && bi < b.size()) {
                auto l = std::max(a[ai][0], b[bi][0] + sy), u = std::min(a[ai][1], b[bi][1] + sy);
                if (u > l)
                    length += static_cast<double>(u - l);
                if (a[ai][1] < b[bi][1] + sy)
                    ai++;
                else
                    bi++;
            }
        }
        return step * (length / 1000);
    };
    if (probe) {
        probe->initialCount = recovered::closedCount(freeMap(primary, false));
        probe->currentCount = recovered::closedCount(freeMap(primary));
        probe->height = geometries[primary].height;
        if(probe->wantFirstPositions) {
            probe->firstRightStrip.assign(input.parts.size(),{});
            for(size_t group=0;group<catalog.size();++group) {
                if(used[group]>=input.parts[group].quantity)continue;
                for(size_t module:catalog[group]) {
                    const auto requested=probe->firstPositionOrdinals.find(group);
                    const auto first=firstPosition(module,requested==probe->firstPositionOrdinals.end()?std::optional<size_t>{}:requested->second);if(!first)continue;
                    const auto edge=int32_t(first->strip+(first->quantizedWidth+pitch-1)/pitch);
                    auto &best=probe->firstRightStrip[group];if(!best||edge<*best)best=edge;
                }
            }
        }
    }
    while (out.placements.size() < static_cast<size_t>(total) &&
           (!control || control->placementLimit < 0 || out.placements.size() - initialPlaced < static_cast<size_t>(control->placementLimit))) {
        budget.check();
        if(control && control->prepareEvaluation)control->prepareEvaluation(int32_t(out.placements.size()));
        const int64_t currentMaximumRight=maximumRight();
        recovered::Initialization initialization;
        initialization.auxReference=auxiliary.has_value();initialization.targetX=control?control->targetX:-1;
        initialization.maxPlacedX=currentMaximumRight;initialization.pitchX=pitch;
        std::optional<size_t> lastPart;
        for(size_t group=0;group<input.parts.size();++group)if(used[group]<input.parts[group].quantity &&
            (!lastPart || minimumWidths[group]>minimumWidths[*lastPart] ||
                (minimumWidths[group]==minimumWidths[*lastPart]&&group>*lastPart)))lastPart=group;
        initialization.hasLastRecord=initialization.hasLastPart=lastPart.has_value();
        if(auxiliary) {
            recovered::FrontInput front;front.placedArea=state.placementHost->informerArea;
            front.sheetWidth=input.width;front.sheetHeight=input.height;
            front.auxMetadataHeight=geometries[*auxiliary].height;front.auxWidth=geometries[*auxiliary].width;
            front.targetField14b8=state.placementHost->requiredArea;front.queueAggregate20=state.placementHost->queueAggregate20;
            front.initialCount=initialAuxiliaryCount;front.currentCount=recovered::closedCount(freeMap(*auxiliary));
            front.pitchX=pitch;front.auxIsSpecial=extendedReferenceIds.contains(*auxiliary);
            front.available=valueParts;
            for(size_t group=0;group<front.available.size();++group)front.available[group].quantity-=used[group];
            initialization.targetEstimate=recovered::currentFront(front);
            if(probe)probe->front=std::move(front);
        }
        if(lastPart && (auxiliary || initialization.targetX>=0)) {
            initialization.lastMaxWidth=valueParts[*lastPart].maxWidth;
            initialization.lastMinWidth=minimumWidths[*lastPart];
            for(size_t group:keys) {
                const auto box=bounds(input.parts.at(group).polygon);
                initialization.groupWidths.push_back(box.x1-box.x0);
            }
            for(size_t module:catalog[*lastPart])initialization.firstPositions.push_back(firstPosition(module));
        }
        recovered::initialize(initialization,scoreState);
        if(probe){probe->initialization=initialization;probe->scoreState=scoreState;}
        struct Candidate {
            size_t gid;
            int64_t strip, y;
            size_t instance;
        };
        std::vector<Candidate> candidates;
        std::vector<recovered::CandidateRef> directory;
        if (useCatalog) directory = recoveredCatalog.available(keys, input.randomSeed);
        else for (size_t t = 0; t < input.parts.size(); ++t) {
            if (used[t] < input.parts[t].quantity)
                for (size_t gid : catalog[t]) directory.push_back({gid, 0});
        }
        for (auto ref : directory) {
            const size_t candidateBegin=candidates.size();
            if(auto provider=noMapProvider(ref.module)){
                ++state.noMapCandidateQueries;
                auto range=provider->range(ref.module);
                const auto &candidateGeometry=geometries[ref.module];
                if(control&&control->columnBounds){const auto width=rounded(candidateGeometry.width*1e6);
                    const int32_t cap=std::min((*control->columnBounds)[0],(*control->columnBounds)[1]-int32_t((width+pitch-1)/pitch));
                    if(cap!=-1)range.end=std::min(range.end,std::max(0,cap));}
                if(control&&control->fixedFront>0){const auto end=recovered::fixedFrontStripEnd(control->fixedFront,
                        candidateGeometry.ox,rounded(bounds(candidateGeometry.points).x1*1e6),pitch);
                    if(end!=-1)range.end=std::min(range.end,int32_t(std::max<int64_t>(0,end)));}
                if(range.first<range.end){
                    if(control&&!control->directionVectors.empty()){
                        const int64_t upper=stockUpperY(input.height-candidateGeometry.height);
                        for(const auto p:provider->directions(ref.module,range,control->directionVectors,0,int32_t(std::max<int64_t>(0,upper))))
                            candidates.push_back({ref.module,p[0],p[1],ref.instance});
                    }else{
                        recovered::ExtremalInput scan;scan.first=range.first;scan.end=range.end;
                        scan.count=control?control->extremaCount:4;
                        size_t direction=0;
                        for(const auto [lower,fromLow]:{std::pair{true,true},{false,true},{true,false},{false,false}}){
                            if(control&&!control->directions[direction++])continue;
                            scan.lower=lower;scan.fromLow=fromLow;
                            for(const auto p:provider->extrema(ref.module,scan))candidates.push_back({ref.module,p[0],p[1],ref.instance});
                        }
                    }
                }
                if(candidates.size()==candidateBegin&&placed.empty()){
                    recovered::FirstPlacementInput first;first.width=input.width;first.height=input.height;first.pitch=pitch;
                    first.partBounds=bounds(candidateGeometry.points);
                    if(const auto p=recovered::firstPlacement(first))candidates.push_back({ref.module,(*p)[0],(*p)[1],ref.instance});
                }
                continue;
            }
            auto rows = freeMap(ref.module,true,useCatalog?recoveredCatalog.instances[ref.instance].ordinal:size_t(used[geometries[ref.module].group]));
            if(control && control->columnBounds) {
                const auto width=rounded(geometries[ref.module].width*1e6);
                const int32_t cap=std::min((*control->columnBounds)[0],(*control->columnBounds)[1]-int32_t((width+pitch-1)/pitch));
                if(cap!=-1)rows.resize(std::min(rows.size(),size_t(std::max(0,cap))));
            }
            if(control && control->fixedFront>0) {
                const auto &g=geometries[ref.module];
                const auto end=recovered::fixedFrontStripEnd(control->fixedFront,g.ox,rounded(bounds(g.points).x1*1e6),pitch);
                if(end!=-1)rows.resize(std::min(rows.size(),static_cast<size_t>(std::max<int64_t>(0,end))));
            }
            if(control && !control->directionVectors.empty()) {
                const double ymax=input.height-geometries[ref.module].height;
                const int64_t upper=stockUpperY(ymax);
                for(auto p:recovered::directionPoints(rows,pitch,control->directionVectors,0,std::max<int64_t>(0,upper)))
                    candidates.push_back({ref.module,p[0],p[1],ref.instance});
                continue;
            }
            size_t direction = 0;
            recovered::ExtremalInput scan;
            scan.end=static_cast<int32_t>(rows.size());
            while(scan.first<scan.end && rows[size_t(scan.first)].empty())++scan.first;
            scan.count=control?control->extremaCount:4;
            scan.maxSteps=control && control->extremaMaxSteps>=0?control->extremaMaxSteps:scan.end-scan.first;
            int64_t right=0;
            for(const auto &p:placed)right=std::max(right,p.strip*pitch+p.phase+rounded(geometries[p.gid].width*1e6));
            scan.supplementEnd=std::clamp(static_cast<int32_t>(right/pitch+(right%pitch!=0)),scan.first,scan.end);
            for (auto [lower, fromLow] :
                 {std::pair{true, true}, {false, true}, {true, false}, {false, false}}) {
                if (control && !control->directions[direction++]) continue;
                scan.lower=lower;scan.fromLow=fromLow;
                for (auto p : recovered::extremalPoints(rows,scan))
                    candidates.push_back({ref.module, p[0], p[1], ref.instance});
            }
            // This host accepts no process/placement constraints. The original
            // fallback is per module, only before the first placed piece.
            if(candidates.size()==candidateBegin && placed.empty()) {
                recovered::FirstPlacementInput first;
                first.width=input.width;first.height=input.height;first.pitch=pitch;
                first.partBounds=bounds(geometries[ref.module].points);
                if(auto p=recovered::firstPlacement(first))
                    candidates.push_back({ref.module,(*p)[0],(*p)[1],ref.instance});
            }
        }
        if (candidates.empty())
            break;
        auto primaryFree = freeMap(primary);
        std::optional<recovered::ShakerTerrain> stairs;
        std::map<size_t,Raster> occupied;
        if(input.lOffcut) {
            recovered::SupervisorShakerInput gate;
            gate.width=input.width;gate.height=input.height;gate.placedArea=totalArea;
            gate.placedCount=total;gate.specialTechnology3c=true;
            if(recovered::smallNestingShakeGate(gate)) {
                const double minimum=input.minimumLOffcutHeight*1000.;
                stairs=ordinaryTerrain(input,state.output,pitch,budget,recovered::truncateInt32(minimum<0?minimum-.5:minimum+.5));
            }
        }
        Rows secondaryFree;
        bool secondaryReady = false, hasBest = false;
        Candidate best{};
        double bestScore = 0;
        for (auto c : candidates) {
            budget.check();
            auto &g = geometries[c.gid];
            recovered::ScoreInput scoreInput;
            scoreInput.primaryCost = integral(primaryFree, pairMap(c.gid, primary), c.strip, c.y);
            scoreInput.value = g.value;
            if(control && control->dynamicValue)scoreInput.value=control->dynamicValue(g.value);
            scoreInput.candidateRightX = c.strip * pitch + rounded(bounds(g.points).x1 * 1e6) - g.ox;
            scoreInput.candidateWidth = g.width;
            scoreInput.sheetHeight = input.height;
            scoreInput.requiredValue = recovered::requiredValue(valueParts[g.group]);
            scoreInput.maxPlacedX=currentMaximumRight;
            scoreInput.placedValue=state.placementHost->informerArea;
            if(stairs) {
                auto found=occupied.find(c.gid);
                if(found==occupied.end())found=occupied.emplace(c.gid,rasterFootprint(g.normalizedContours,pitch,budget)).first;
                scoreInput.staircase=true;scoreInput.pitch230=pitch;
                scoreInput.staircaseLoss=recovered::shakerTerrainLoss(*stairs,found->second.rows,int32_t(c.strip),int32_t(c.y),1);
            }
            double score = recovered::score(scoreInput, scoreState);
            const size_t candidateOrdinal=useCatalog?recoveredCatalog.instances[c.instance].ordinal:size_t(used[g.group]);
            const Placement scoredPlacement{g.group,candidateOrdinal,g.pose,c.strip*pitch-g.ox,c.y-g.oy};
            if(control && control->evaluatePlacement)score=control->evaluatePlacement(scoredPlacement,score);
            if (std::abs(score) < 1e-9) {
                if (!secondaryReady) {
                    secondaryFree = freeMap(secondary);
                    secondaryReady = true;
                }
                scoreInput.secondaryCost = integral(secondaryFree, pairMap(c.gid, secondary), c.strip, c.y);
                scoreState.mix = .001;
                if(control && control->dynamicValue)scoreInput.value=control->dynamicValue(g.value);
                score = recovered::score(scoreInput, scoreState);
                if(control && control->evaluatePlacement)score=control->evaluatePlacement(scoredPlacement,score);
                scoreState.mix = 0;
            }
            require(std::isfinite(score), PN2S_INTERNAL, "Nonfinite candidate score");
            if (!hasBest || (std::abs(score - bestScore) >= 1e-5 && score < bestScore)) {
                best = c;
                bestScore = score;
                hasBest = true;
            }
        }
        const auto &g = geometries[best.gid];
        const size_t ordinal = useCatalog ? recoveredCatalog.instances[best.instance].ordinal
                                                    : static_cast<size_t>(used[g.group]);
        ++used[g.group];
        ordinaryCommit(state,{g.group, ordinal, g.pose,best.strip*pitch-g.ox,best.y-g.oy});
        if(noMap){noMap->append({best.gid,best.strip*pitch-g.ox,int32_t(best.y-g.oy),false});
            if(noMapDecisions[best.gid]==1)++state.noMapCommitted;}
        placed.push_back({best.gid, best.strip, best.y});
    }
    return static_cast<int>(out.placements.size() - initialPlaced);
}
Output ordinary(const Input &input, Budget &budget) {
    if (input.recoveredOffset) return ordinaryOffset(input, offsetProgram(input), budget);
    OrdinaryState state;
    ordinaryRun(input, budget, state);
    return std::move(state.output);
}
void ordinaryRestore(OrdinaryState &state, size_t count) {
    require(state.catalog && state.placementHost && count <= state.output.placements.size(), PN2S_INTERNAL, "Invalid ordinary checkpoint");
    while (state.output.placements.size() > count) {
        const auto p = state.output.placements.back();
        const auto record = state.catalog->groups.at(p.group).at(0);
        const auto instance = state.catalog->records.at(record).instances.at(p.ordinal);
        auto &host=*state.placementHost;
        require(!host.nesting.placements.empty() && size_t(host.nesting.placements.back()->instance18)==instance,PN2S_INTERNAL,"Ordinary lifecycle order differs from output");
        require(host.lifecycle.remove(host.nesting.placements.back()),PN2S_INTERNAL,"Ordinary lifecycle removal failed");
        state.catalog->setPlaced(instance, false);
        state.output.placements.pop_back();
    }
    // Pair maps are immutable; free maps are rebuilt from placements at next use.
    // Neither catalog cursors nor a subnester's private evaluation object is rewound.
}
void ordinaryCommit(OrdinaryState &state, const Placement &p) {
    require(state.catalog.has_value() && state.placementHost, PN2S_INTERNAL, "Ordinary state not initialized");
    const auto record = state.catalog->groups.at(p.group).at(0);
    const auto instance = state.catalog->records.at(record).instances.at(p.ordinal);
    require(instance<=INT32_MAX && p.y>=INT32_MIN && p.y<=INT32_MAX,PN2S_UNSUPPORTED,"Placement identity or Y exceeds original 32-bit range");
    auto &host=*state.placementHost;
    auto placement=std::make_shared<recovered::WarmPlacement>(host.modules.at(p.group).at(p.poseIndex));
    placement->instance18=int32_t(instance);placement->instance40=instance;placement->x48=p.x;placement->y50=int32_t(p.y);
    placement->rounded2c = state.output.pitchX > 0 && (placement->leftE0 + p.x) % state.output.pitchX != 0;
    // The current strip geometry adapter has already admitted the candidate.
    // Supplier constraint families are not silently replaced here.
    require(host.lifecycle.commit(placement),PN2S_INTERNAL,"Ordinary lifecycle rejected duplicate placement");
    state.catalog->setPlaced(instance, true);
    state.output.placements.push_back(p);
}
} // namespace study
