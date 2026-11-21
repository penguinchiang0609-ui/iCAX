#include "ordinary_supervisor.hpp"
#include "ordinary_chris.hpp"
#include "ordinary_chris6.hpp"
#include "ordinary_escher.hpp"
#include "ordinary_unplaced.hpp"
#include "ordinary_shaker_constraints.hpp"
#include "ordinary_general_tiling.hpp"
#include "ordinary_predefined_validation.hpp"
#include "ordinary_multiheads_manager.hpp"
#include "ordinary_pair_map.hpp"
#include "recovered_multiheads_dispatch.hpp"
#include "recovered_multiheads_infer.hpp"
#include "recovered_history_initialization.hpp"
#include "rectangle_runtime.hpp"
#include "ordinary_rectangle_tiling.hpp"
#include "recovered_rich_conversion.hpp"
#include "shaker_runtime.hpp"
#include "recovered_shaker_terrain.hpp"
#include <bit>

namespace study {
namespace {
using namespace recovered;
double secondsLeft(const Budget &budget) {
    return std::chrono::duration<double>(budget.deadline - std::chrono::steady_clock::now()).count();
}
double placementX(const Placement &p) { return std::isfinite(p.exactX) ? p.exactX : double(p.x) / 1e6; }
double placementY(const Placement &p) { return std::isfinite(p.exactY) ? p.exactY : double(p.y) / 1000.; }
Pose placementPose(const Input &input, const Placement &p) {
    auto pose = input.parts.at(p.group).poses.at(p.poseIndex);
    if (std::isfinite(p.solvedAngle)) pose.angle = p.solvedAngle;
    return pose;
}
int64_t maximumRight(const Input &input, const Output &output) {
    int64_t result = 0;
    for (const auto &p : output.placements) {
        const auto &part = input.parts.at(p.group);
        const auto box = bounds(transform(part.polygon, part.pivot, placementPose(input, p)));
        result = std::max(result, rounded((placementX(p) + box.x1) * 1e6));
    }
    return result;
}
bool predefinedLayoutValid(const Input &input,const Output &baseline,const Output &candidate,
                          const OrdinaryPredefinedProgram &program,bool requireOriginal=true) {
    Output retained=baseline;
    if(!requireOriginal)std::erase_if(retained.placements,[&](const auto &original){return
        std::none_of(candidate.placements.begin(),candidate.placements.end(),[&](const auto &p){return p.group==original.group&&p.ordinal==original.ordinal;});});
    if(!validatePredefinedContinuation(input,retained,candidate).valid)return false;
    for(const auto &lock:ordinaryPredefinedLocks(retained,program,program.moveDefault))if(lock.fixed){
        const auto &original=retained.placements.at(lock.placementIndex);
        const auto found=std::find_if(candidate.placements.begin(),candidate.placements.end(),[&](const auto &p){return p.group==original.group&&p.ordinal==original.ordinal;});
        if(found==candidate.placements.end())return false;
        const auto a=placementPose(input,original),b=placementPose(input,*found);
        if(placementX(original)!=placementX(*found)||placementY(original)!=placementY(*found)||a.angle!=b.angle||a.flip!=b.flip)return false;
    }
    return true;
}
void copyOutputRelations(std::span<const WarmPlacementPtr> source,OrdinaryState &target){
    auto &owners=target.placementHost->nesting.placements;
    require(source.size()==owners.size(),PN2S_INTERNAL,"Snapshot relation owner count changed");
    std::map<uintptr_t,std::shared_ptr<ProcessPlacement>> byInstance;
    for(const auto &owner:owners)byInstance.emplace(owner->instance40,owner);
    const PlacementCopyLookup lookup=[&](uintptr_t identity)->std::optional<std::shared_ptr<ProcessPlacement>>{
        const auto found=byInstance.find(identity);if(found==byInstance.end())return {};return found->second;};
    for(size_t index=0;index<source.size();++index){
        require(source[index]->instance40==owners[index]->instance40,PN2S_INTERNAL,"Snapshot relation instance order changed");
        copyPlacementRelations(*source[index],owners[index],lookup);copyPlacementProcesses(*source[index],owners[index],lookup);
        owners[index]->attributes68=source[index]->attributes68;owners[index]->marker28=source[index]->marker28;
    }
    multiheadsShakerConstraints(owners);
}
Output optimize(const Input &input, const Output &source, Budget &budget, int32_t operation,
                int64_t pitch230, double contextHeight220, bool *accepted = nullptr,
                const ShakerTerrain *terrain=nullptr,size_t *guideCount=nullptr,size_t *pointRows=nullptr,
                const OrdinaryPredefinedProgram *predefined=nullptr,std::span<const WarmPlacementPtr> owners={},
                size_t *headRows=nullptr) {
    if (accepted) *accepted = false;
    if (source.placements.empty() || secondsLeft(budget) <= 0.) return source;
    ShakerInputScene scene; ConvolutionCurves curves;
    shakerConfigureOrdinaryRelations(scene.relations);
    std::map<size_t, size_t> geometries;
    for (const auto &p : source.placements) {
        budget.check(true);
        const auto &part = input.parts.at(p.group);
        auto geometry = geometries.find(p.group);
        if (geometry == geometries.end()) {
            ShakerPolygonInput polygon;
            for (const auto point : part.polygon) polygon.outer.push_back({point.x, point.y});
            for(const auto &hole:part.holes){polygon.holes.emplace_back();for(const auto point:hole)polygon.holes.back().push_back({point.x,point.y});}
            const auto index = shakerCreateGeometry(scene, {part.pivot.x, part.pivot.y});
            shakerAppendPolygonComponents(scene, index, polygon, curves);
            geometry = geometries.emplace(p.group, index).first;
        }
        const auto pose = placementPose(input, p);
        const auto index = shakerCreatePart(scene, geometry->second, {placementX(p), placementY(p)},
            pose.angle * pi / 180., {uint8_t(pose.flip), 0});
        // Each ordinary pose is a finite allowed angle, so rotation is locked.
        shakerSetMobility(scene.parts[index], {0, 0, 1}, scene.parts[index].bounds,
                          {pose.angle * pi / 180., pose.angle * pi / 180.});
    }
    if(predefined)for(const auto &lock:ordinaryPredefinedLocks(source,*predefined,predefined->moveDefault))if(lock.fixed){
        auto &part=scene.parts.at(lock.placementIndex);
        shakerSetMobility(part,{1,1,1},part.bounds,{part.angle,part.angle});
    }
    if(operation==10 || (operation==2 && input.lOffcut)) {
        require(terrain!=nullptr,PN2S_INVALID,"Operation ten terrain provider");
        for(const auto &guide:shakerTerrainGuides(*terrain,double(pitch230)/1e6)) {
            ShakerPolygonInput polygon;polygon.type=-2;
            for(const auto point:guide)polygon.outer.push_back({point.x,point.y});
            const auto shape=shakerCreateGeometry(scene,{});
            shakerAppendPolygonComponents(scene,shape,polygon,curves);
            const auto part=shakerCreatePart(scene,shape,{},0);
            auto bounds=scene.parts[part].bounds;
            if(operation==10) {
                bounds[0]=-input.width;
                shakerSetMobility(scene.parts[part],{0,1,1},bounds,{0,0});
                const auto box=study::bounds(guide);scene.parts[part].objective={box.y1-box.y0,0,0};
            } else shakerSetMobility(scene.parts[part],{1,1,1},bounds,{0,0});
            if(guideCount)++*guideCount;
        }
    }
    const double span = std::max(input.width, input.height);
    shakerCreateBoundaryGeometry(scene, span * 4, span * 4, span);
    const double right = double(maximumRight(input, source)) / 1e6;
    require(shakerSetBounds(scene, 0, right + 5e-7, 0, input.height + 5e-7) == 0,
            PN2S_INVALID, "Default supervisor post-optimization bounds");
    const double solverSeconds=secondsLeft(budget);
    if(!(solverSeconds>0.))return source;
    ShakerRunResult run;
    auto constraints=ordinaryShakerPointConstraints(input,source);
    const auto headConstraints=owners.empty()?std::vector<ShakerPairConstraint>{}:multiheadsShakerConstraints(owners);
    constraints.insert(constraints.end(),headConstraints.begin(),headConstraints.end());
    if (operation == 2) run = compactShakerFixedScene(scene, 0, 2, solverSeconds,0,constraints);
    else {
        require(operation == 1 || operation==10, PN2S_UNSUPPORTED, "Default supervisor Shaker operation provider");
        if(operation==10)shakerResetBoundaryObjectives(scene.parts,scene.boundaries);
        else run.status = shakerConfigureDefault(scene.parts, scene.boundaries, 2);
        if (!run.status) {
            const double scale=shakerSceneScale(scene.parts,scene.geometry,scene.boundaries);
            const auto options = operation==10 ? ShakerIterationOptions{25,2,solverSeconds,scale*.0001,
                    scale*(1./30.),scale*.01,.01,false,false} :
                supervisorShakerOneOptions(solverSeconds,double(pitch230)/1e6,contextHeight220,scale,true);
            run = runShakerScene(scene, options,0,constraints);
        }
    }
    if(pointRows&&!input.pointGroups.empty())*pointRows+=run.constraintRows;
    if(headRows&&!headConstraints.empty())*headRows+=run.constraintRows;
    if (run.status || !run.iteration.success) return source;
    Output candidate = source;
    for (size_t index = 0; index < candidate.placements.size(); ++index) {
        auto &p = candidate.placements[index];
        p.x = rounded(scene.parts[index].position[0] * 1e6);
        p.y = rounded(scene.parts[index].position[1] * 1000.);
        // Subsequent ordinary construction consumes the original integer grid.
        p.exactX = p.exactY = p.solvedAngle = NAN;
    }
    if (maximumRight(input, candidate) > maximumRight(input, source) ||
        !(predefined?predefinedLayoutValid(input,source,candidate,*predefined):valid_layout(input,candidate)) ||
        !validPointsProximity(input,candidate,input.pointGroups)) return source;
    if (accepted) *accepted = true;
    return candidate;
}
struct SupervisorNesting final : WarmNesting {
    OrdinaryState state;
    CandidateMetrics metrics;
    LayoutSignature signature;
    std::map<int32_t, Placement> instancePlacements;
    std::optional<ShakerTerrain> terrain;
    StripQuantityCache stripQuantities;
    double informer20 = 0, costBenefit = 0;
    std::array<double, 3> processQuantities{};
};
class SupervisorHost final : public NesterWorkerHost, public ConstructionHost {
    struct Manager {
        StrategyManager descriptor;
        std::unique_ptr<CandidatePool<TrialRecordPtr>> pool;
    };
    const Input &input;
    Budget &parent;
    const SupervisorProgram &program;
    SupervisorTrace &trace;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now(), phaseStarted = started;
    std::unique_ptr<Budget> phaseBudget;
    double totalArea = 0, phaseSeconds = 0;
    int32_t population = 0, activeId = 0, phaseValue = 0;
    OrdinaryContextMeasures measures;
    Output predefinedBaseline;
    std::vector<EligibilityPart> eligibilityParts;
    std::vector<RectangleTilingModel> rectangleModels;
    FactoryApplicabilityContext applicability;
    FactoryPreset preset;
    std::map<int, Manager> managers;
    CandidatePool<TrialRecordPtr> global;
    std::shared_ptr<TrialNesting> currentNesting;
    Random scheduleRandom{0};
    ScheduleState schedule;
    std::vector<StrategyEntry> entries;
    std::vector<double> weights;
    ReverseRunner runner;
    bool expired = false, endedFast = false, presetDirty = true, strategySupported = true;
    bool validCandidate(const Output &candidate,bool requireOriginal=true)const {
        return program.predefined?predefinedLayoutValid(input,predefinedBaseline,candidate,*program.predefined,requireOriginal):valid_layout(input,candidate);
    }
    HistoryControl history;
    std::optional<TrialRecordPtr> selectedWarm;
    std::optional<double> expectedShakerGain;
    SupervisorNesting &nesting() { return dynamic_cast<SupervisorNesting &>(*currentNesting); }
    const SupervisorNesting &recordNesting(const TrialRecordPtr &record) const {
        return dynamic_cast<const SupervisorNesting &>(*record->nesting);
    }
    void correctedLength(SupervisorNesting &n,CorrectedLengthCache &cache) {
        CandidateLengthInput length;length.maximumRight=n.maximumRight;
        length.width08=input.width;length.height10=input.height;
        length.config10=program.materialUnitCost10;
        length.config3c=program.engineBudget.specialTechnology3c;
        length.quantity3cc260=[&] {return candidateQuantity260(n.maximumRight,0,input.width,input.height,
            length.config3c,n.stripQuantities,[](auto &){throw Failure(PN2S_INTERNAL,"Missing actual terrain quantity cache");});};
        length.quantity3cbe50=[&]{
            std::vector<std::shared_ptr<ProcessPlacement>> placements;
            placements.reserve(n.placements.size());
            for(const auto &placement:n.placements)placements.push_back(placement);
            n.processQuantities=candidateProcessQuantities(placements);
            n.costBenefit=candidateQuantityE50(n.processQuantities,n.informer20,
                program.masterCuttingUnitCost18,program.additionalHeadCuttingUnitCost20);
            return n.costBenefit;
        };
        evaluateCorrectedLength(cache,length,[]{});n.metrics.correctedLength=cache.value;
    }
    static SupervisorTrace::CandidateCost costRecord(const SupervisorNesting &n) {
        return {n.metrics.sourceId,n.metrics.trial,rawCandidateLength(n.maximumRight,0),
            n.metrics.correctedLength,n.informer20,n.costBenefit,n.processQuantities};
    }
    void inferRectangleSisters(OrdinaryState &state) {
        require(state.catalog&&state.placementHost,PN2S_INTERNAL,"Rectangle sister inference requires actual placement owners");
        auto &owners=state.placementHost->nesting.placements;
        require(owners.size()==state.output.placements.size(),PN2S_INTERNAL,"Rectangle sister owner count differs from output");
        std::vector<MultiheadsInferPlacement> placements;placements.reserve(owners.size());
        for(size_t index=0;index<owners.size();++index){
            const auto &placed=state.output.placements[index];const auto &owner=owners[index];
            const auto record=state.catalog->groups.at(placed.group).at(placed.poseIndex);
            const auto module=state.catalog->records.at(record).module;
            require(module<=INT64_MAX,PN2S_INTERNAL,"Rectangle sister module key exceeds the recovered integer domain");
            placements.push_back({owner->x48,int64_t(module),owner->y50,owner->group58&&!owner->group58->members.empty()});
        }
        ++trace.rectangleInferenceCalls;
        for(const auto &members:multiheadsInferGroups(placements,program.multiheads)){
            auto group=std::make_shared<PlacementGroup>();
            for(const auto member:members)group->members.push_back(owners.at(member));
            for(const auto member:members){auto &owner=owners.at(member);owner->group58=group;owner->groupFirst=group->members.front();}
            ++trace.rectangleInferredGroups;
        }
        multiheadsShakerConstraints(owners);
    }
    bool less(const TrialRecordPtr &a, const TrialRecordPtr &b) const {
        const auto &first = recordNesting(a), &second = recordNesting(b);
        return candidateLess(first.metrics, second.metrics,
            [&] { return layoutDifference(first.signature, second.signature); });
    }
    bool related(const TrialRecordPtr &first, const TrialRecordPtr &second) const {
        const auto &a = recordNesting(first), &b = recordNesting(second);
        std::vector<RelatedPlacement> placements;
        for (size_t index = 0; index < b.placements.size(); ++index) {
            const auto &p = b.placements[index];
            placements.push_back({b.signature.placements.at(index), p->quantity220, p->area58, p->auxiliary3c1});
        }
        return relatedCandidates(a.signature, b.signature.sheetIdentity, input.width, input.height, b.metrics.metric, placements);
    }
    void initialize(SupervisorNesting &n) {
        n.state.mapEligible=[this]{return recovered::mapEligible(applicability.map,applicability.mapInput);};
        OrdinaryControl initial; initial.placementLimit = 0;
        for (const auto &part : input.parts) initial.values.push_back(material_area(part));
        ordinaryRun(input, *phaseBudget, n.state, &initial);
        // 3d1c10/3d2050 update the raw all-head perimeter in lifecycle order.
        // It must not be reconstructed from the current (possibly reordered)
        // placement vector or reduced to the current sister leaders.
        n.informer20=0;
        n.state.placementHost->lifecycle.observers.push_back({
            [&n](const auto &p){n.informer20=(p->auxiliary3c1?0.:p->geometry60)+n.informer20;},
            [&n](const auto &p){n.informer20=n.informer20-(p->auxiliary3c1?0.:p->geometry60);}});
        n.totalValue78 = totalArea; n.box08 = input.width; n.box10 = input.height;
    }
    void sync(SupervisorNesting &n, int source) {
        if (n.state.placementHost) {
            const auto &host = *n.state.placementHost;
            n.placements = host.nesting.placements;
            n.quantity18 = host.informerArea;
            n.placedCount13d8 = int32_t(n.placements.size());
            n.maximumRight = maximumRight(input, n.state.output);
            n.instancePlacements.clear();
            for (size_t index = 0; index < n.state.output.placements.size(); ++index)
                n.instancePlacements.emplace(n.placements[index]->instance18, n.state.output.placements[index]);
        }
        std::vector<CandidateIntegerTerm> integerTerms;
        for (const auto &p : n.placements) integerTerms.push_back({p->y50, p->topF8});
        n.integerMeasure = candidateInteger(integerTerms, 0);
        n.metrics = {n.quantity18, rawCandidateLength(n.maximumRight, 0), n.integerMeasure, 0, source, n.trial13bc};
        n.signature = {};
        n.signature.sheetIdentity = reinterpret_cast<uintptr_t>(&input);
        n.signature.placementCount = n.placedCount13d8; n.signature.maximumRight = n.maximumRight;
        n.signature.height10 = input.height; n.signature.typeCount = input.parts.size();
        for (size_t group = 0; group < input.parts.size(); ++group) n.signature.typeOrder.push_back(group);
        for (const auto &p : n.state.output.placements) n.signature.placements.push_back({p.group, p.x, int32_t(p.y)});
        if(program.engineBudget.specialTechnology3c) {
            const double minimum=program.technologyMinimum40*1000.;
            n.terrain=ordinaryTerrain(input,n.state.output,applicability.mapInput.pitch230,parent,
                truncateInt32(minimum<0?minimum-.5:minimum+.5));
            const auto &terrain=*n.terrain;
            refreshStripQuantities(n.stripQuantities,terrain.heights,terrain.upper98-terrain.lower94,
                terrain.area80,applicability.mapInput.pitch230);
        } else {n.terrain.reset();n.stripQuantities={};}
        CorrectedLengthCache corrected;correctedLength(n,corrected);
    }
    MultiProgram multiSettings(const Input &active) {
        const auto context = ordinaryContext(active);
        MultiProgram settings;
        settings.contextBatch238 = context.batch238;
        settings.contextField218 = context.width218; settings.contextField220 = context.height220;
        settings.initialRandom = nesting().random;
        settings.dynamicQueue = true;
        if (const auto element = std::dynamic_pointer_cast<const SupervisorReference>(nesting().element14c0))
            if (!element->polygon.empty()) settings.referenceGeometry = element->polygon;
        settings.targetField14b8 = totalArea; settings.queueAggregate20 = nesting().quantity18;
        settings.maximumLengthX = nesting().bound14a8;
        return settings;
    }
    ReverseRunner createChild(const Input &active, int base, Random random) {
        if (const auto custom = program.additionalFactories.find(base); custom != program.additionalFactories.end())
            return custom->second(active);
        switch (base) {
        case 1: case 4: {
            OffsetFactoryInput settings;
            settings.count = base == 4 ? 1 : managers.at(activeId).descriptor.offsetTryCount;
            settings.dynamicEvaluation=nesting().trial13bc>0;
            auto plan = offsetProgram(active, settings, &random);
            return [plan](const Input &source, Budget &time, OrdinaryState &state) {
                return ordinaryOffsetContinue(source, plan, time, state);
            };
        }
        case 2: {
            const auto plan = columnProgram(active, {}, &random);
            return [plan](const Input &source, Budget &time, OrdinaryState &state) {
                return ordinaryColumnContinue(source, plan, time, state);
            };
        }
        case 3: {
            auto settings = multiSettings(active); settings.initialRandom = random;
            const auto plan = multiProgram(active, settings);
            return [plan](const Input &source, Budget &time, OrdinaryState &state) {
                return ordinaryBtContinue(source, plan, time, state);
            };
        }
        case 5: {
            ChrisProgram settings; settings.common = multiSettings(active); settings.common.initialRandom = random;
            settings.managerId = activeId;
            const auto plan = chrisProgram(active, settings);
            return [plan](const Input &source, Budget &time, OrdinaryState &state) {
                return ordinaryBtContinue(source, plan, time, state);
            };
        }
        case 6: {
            if(!program.problemEvaluation){trace.unsupportedManagers.insert(activeId);strategySupported=false;return {};}
            const auto plan=chris6Program(active,*program.problemEvaluation,{},&random);
            return [plan](const Input &source,Budget &time,OrdinaryState &state){
                return ordinaryChris6Continue(source,plan,time,state);
            };
        }
        case 7: {
            OffsetFactoryInput settings;settings.dynamicEvaluation=nesting().trial13bc>0;
            auto plan=escherProgram(active,parent.requestedSeconds,settings,&random);
            plan.trial13bc=nesting().trial13bc;
            return [this,plan](const Input &source,Budget &time,OrdinaryState &state){
                OrdinaryEscherTrace record;
                const auto count=ordinaryEscherContinue(source,plan,time,state,&record);
                trace.missingProviders.insert(record.missingProviders.begin(),record.missingProviders.end());
                return count;
            };
        }
        case 8:
            return [this](const Input &source, Budget &time, OrdinaryState &state) {
                if (!state.output.placements.empty()) {
                    trace.unsupportedManagers.insert(activeId); strategySupported = false; return 0;
                }
                Output result;
                RectangleProgram settings;
                settings.modules = managers.at(activeId).descriptor.reversed
                    ? ordinaryRectangleTilingModelsForInput(source, time, parent.requestedSeconds) : rectangleModels;
                //404d50 returns zero before solving when the already prepared
                //problem has no participating groups; it does not construct
                //primitive models from an unrelated orientation catalog.
                if(settings.modules.empty())return 0;
                if (!ordinaryRectangle(source, time, result, settings)) {
                    trace.unsupportedManagers.insert(activeId); strategySupported = false; return 0;
                }
                require(valid_layout(source, result), PN2S_INTERNAL, "Rectangle default candidate validity");
                if(!validPointsProximity(source,result,source.pointGroups)) {
                    trace.missingProviders.insert("rectangle.pointProximityPatternSearch");
                    return 0;
                }
                for (auto p : result.placements) {
                    p.exactX = p.exactY = p.solvedAngle = NAN;
                    ordinaryCommit(state, p);
                }
                // 405020 -> 3b9de0 -> 3f63a0: imported Rectangle results
                // acquire actual simultaneous-head ownership at this point.
                inferRectangleSisters(state);
                return int(result.placements.size());
            };
        case 10: {
            OffsetFactoryInput settings;settings.dynamicEvaluation=nesting().trial13bc>0;
            const auto plan=unplacedProgram(active,active.pointGroups,settings,&random);
            return [plan](const Input &source,Budget &time,OrdinaryState &state){
                return ordinaryUnplacedContinue(source,plan,time,state);
            };
        }
        case 9: {
            const auto plan=multiheadsProgram(active,program.multiheads,nesting().trial13bc,&random);
            return [plan](const Input &source,Budget &time,OrdinaryState &state){
                return ordinaryMultiheadsContinue(source,plan,time,state);
            };
        }
        default:
            trace.unsupportedManagers.insert(activeId);
            strategySupported = false;
            return {};
        }
    }
    class WarmAdapter final : public WarmStartHost {
        SupervisorHost &host;
        const SupervisorNesting &source;
        std::map<uintptr_t, std::shared_ptr<ProcessPlacement>> mapping;
    public:
        WarmAdapter(SupervisorHost &owner, const SupervisorNesting &from) : host(owner), source(from) {}
        WarmNesting &current() override { return host.nesting(); }
        bool isColumnNesting() override { return host.managers.at(host.activeId).descriptor.baseId == 2; }
        void resetCurrent() override { host.resetCurrent(); }
        void removeTrailingPlacements() override {
            ordinaryRestore(host.nesting().state, 0); host.sync(host.nesting(), host.activeId);
        }
        void notifyStartPredefined() override {}
        void resetSelectionQueue() override {}
        bool instanceAvailable(int32_t index) override {
            const auto &catalog = *host.nesting().state.catalog;
            return index >= 0 && size_t(index) < catalog.instances.size() && !catalog.instances[size_t(index)].placed;
        }
        bool acceptsPlacement(const WarmPlacement &p) override {
            auto trial = host.nesting().state.output;
            const auto found = source.instancePlacements.find(p.instance18);
            if (found == source.instancePlacements.end()) return false;
            trial.placements.push_back(found->second);
            return host.validCandidate(trial,false)&&validPointsProximity(host.input,trial,host.input.pointGroups);
        }
        WarmPlacementPtr cloneAndInsert(const WarmPlacement &p) override {
            ordinaryCommit(host.nesting().state, source.instancePlacements.at(p.instance18));
            const auto result = host.nesting().state.placementHost->nesting.placements.back();
            result->attributes68 = p.attributes68; result->marker28 = p.marker28;
            result->quantity220 = p.quantity220;
            mapping.emplace(p.instance40, result);
            host.sync(host.nesting(), host.activeId);
            return result;
        }
        double copyProcesses(const WarmPlacement &p) override {
            const PlacementCopyLookup lookup=[&](uintptr_t identity)
                -> std::optional<std::shared_ptr<ProcessPlacement>> {
                const auto found = mapping.find(identity);
                if (found == mapping.end()) return {};
                return found->second;
            };
            copyPlacementRelations(p,mapping.at(p.instance40),lookup);
            return copyPlacementProcesses(p,mapping.at(p.instance40),lookup);
        }
        void notifyEndPredefined() override {}
        void pruneAuxiliaryGroups() override {}
        double debugColorConfig750() override { return 0.; }
        void colorPlacements(const char *) override {}
    };
public:
    SupervisorHost(const Input &source, Budget &time, const SupervisorProgram &settings, SupervisorTrace &record)
        : input(source), parent(time), program(settings), trace(record),
          global(uint32_t(settings.maximumCandidates), [this](const auto &a, const auto &b) { return less(a, b); }) {
        require(ordinaryPreparedDomain(input), PN2S_UNSUPPORTED,
                "Recovered default supervisor requires finite grid-aligned single rings and zero spacing");
        require(program.maximumCandidates > 0 && program.factory.candidateLimit1c0 >= 1,
                PN2S_INVALID, "Default supervisor pool capacity");
        require(validPointsProximity(input,{},input.pointGroups),PN2S_INVALID,"Default supervisor point proximity input");
        if(program.predefined&&!program.predefined->records.empty())
            predefinedBaseline=ordinaryPreNest(input,*program.predefined,parent);
        for (size_t group = 0; group < input.parts.size(); ++group) {
            const auto &part = input.parts[group]; const auto box = bounds(part.polygon);
            const auto material = material_area(part);
            population += part.quantity; totalArea += double(part.quantity) * material;
            EligibilityPart value;
            value.identity = group + 1; value.required = part.quantity;
            value.minX = box.x0; value.minY = box.y0; value.maxX = box.x1; value.maxY = box.y1;
            value.area = value.value = material;
            eligibilityParts.push_back(value);
        }
        measures = ordinaryContext(input);
        applicability.population88 = population;
        applicability.input.parts = applicability.mapInput.parts = eligibilityParts;
        applicability.input.sheetHeight = input.height; applicability.input.rectangleCount998 = 10;
        applicability.input.heads48=program.multiheads.heads48;
        applicability.input.hasTiles28 = program.hasTiles;
        applicability.input.rectangleGroups = program.rectangleGroups;
        OrdinaryGeneralTilingTrace tilingTrace;
        rectangleModels = ordinaryRectangleTilingModelsForInput(input, parent, parent.requestedSeconds, &tilingTrace);
        trace.missingProviders.insert(tilingTrace.missingProviders.begin(),tilingTrace.missingProviders.end());
        if (applicability.input.rectangleGroups.empty()) {
            for (const auto &model : rectangleModels) {
                std::vector<size_t> members;
                for (const auto &member : model.members) members.push_back(member.group);
                if (!members.empty()) applicability.input.rectangleGroups.push_back(std::move(members));
            }
        }
        applicability.unplacedModules.resize(input.parts.size());
        if(!input.pointGroups.empty())applicability.unplacedModules.assign(input.pointGroups.size(),uint8_t(1));
        for (const auto &part : input.parts) {
            parent.check(true);
            std::vector<SdkOrientation> sdk;
            for (const auto &pose : part.poses) sdk.push_back({int32_t(pose.flip), pose.angle, pose.upper});
            const auto contours=ordinaryPreparedContours(part,{});
            const auto symmetry = shapeAxisSymmetry(contours,bounds(contours.front()));
            const auto cache = partOrientationCache(sdk, true, symmetry.horizontal, symmetry.vertical);
            trace.partOrientationCacheSizes.push_back(cache.size());
        }
        const int32_t orientations = orientationCachePopulation(trace.partOrientationCacheSizes);
        trace.orientationPopulation = orientations;
        applicability.mapInput.problemScale = program.problemScale < 0 ? orientations : program.problemScale;
        applicability.mapInput.typeLimit188 = 200;
        const std::array<PitchSheet, 1> sheets{{{input.width, 0, 0}}};
        applicability.mapInput.pitch230 = contextPitch(sheets, input.pitch, measures.width218, 50., false);
        applicability.mapInput.memoryLimitD0 = program.technologyMemoryLimitD0;
        const auto admit = [&](StrategyManager &manager) { return strategyManagerApplicable(manager, applicability, program.factory); };
        preset = initialFactoryPreset(program.initialPreset, program.reversePreset, 1, program.factory, admit);
        history.random = Random(0x373e); history.targetManager29d8 = 3;
        history.dynamic29dc = program.dynamicHistory;
        history.fixedElement = program.fixedReference;
        if (!history.fixedElement) {
            double area = 0.; int32_t count = 0;
            const auto parts = ordinaryValueParts(input, area, count);
            const auto mean = effectiveAverage(parts, area, {measures.width218, measures.height220});
            const auto description = fixedHistoryReference(mean, 0);
            auto reference = std::make_shared<SupervisorReference>();
            reference->polygon = description.polygon; reference->value10 = description.height;
            history.fixedElement = std::move(reference);
        }
        for (const auto &element : program.historyElements) history.elements.push_back(element);
        if (history.elements.empty()) {
            for (const auto &description : dynamicHistoryReferences(history.fixedElement->value10, input.height, 10, 0)) {
                auto reference = std::make_shared<SupervisorReference>();
                reference->polygon = description.polygon; reference->value10 = description.height;
                history.elements.push_back(std::move(reference));
            }
        }
        const auto count = history.elements.size();
        history.statistics = initialHistoryStatistics(count);
        currentNesting = std::make_shared<SupervisorNesting>();
    }
    double elapsed() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); }
    const TrialRecordPtr *best() const { return global.best(); }
    size_t poolSize() const { return global.size(); }
    int32_t configuredTrials() const { return schedule.trials; }
    void setSelectedPreset() {
        std::vector<StrategyResult> results;
        for (const auto &record : global.values()) results.push_back({recordNesting(record).metrics.metric, record->sourceId});
        const auto admit = [&](StrategyManager &manager) { return strategyManagerApplicable(manager, applicability, program.factory); };
        if (!results.empty()) preset = selectedFactoryPreset(results, int32_t(trace.trials), program.keptScale198,
            program.inverseKept190, program.fastPreset, 1, program.factory, admit);
        else preset = initialFactoryPreset(program.fastPreset, 0, 0, program.factory, admit);
        presetDirty = true;
    }
    void runStage(double untilElapsed, int phase, int32_t trialLimit, double poolGrowth) {
        const double seconds = std::min(untilElapsed - elapsed(), secondsLeft(parent));
        if (!(seconds > 0) || preset.managers.empty()) return;
        phaseBudget = std::make_unique<Budget>(seconds); phaseBudget->deadline = std::min(phaseBudget->deadline, parent.deadline);
        phaseSeconds = seconds; phaseStarted = std::chrono::steady_clock::now();
        phaseValue = phase;
        history.budget1b8 = seconds;
        expired = false; endedFast = false; entries.clear(); weights.clear();
        if (presetDirty) managers.clear();
        for (const auto &[id, entry] : preset.preset.entries) {
            auto value = entry;
            if (!presetDirty) value.budget = managers.at(id).descriptor.trials.budget58;
            entries.push_back(value);
        }
        addStrategyBudgets(entries, 1, seconds, preset.preset.totalWeight);
        for (const auto &entry : entries) {
            if (presetDirty) {
                Manager manager; manager.descriptor = preset.managers.at(entry.id);
                manager.pool = std::make_unique<CandidatePool<TrialRecordPtr>>(uint32_t(manager.descriptor.candidateLimit),
                    [this](const auto &a, const auto &b) { return less(a, b); });
                managers.emplace(entry.id, std::move(manager));
            }
            managers.at(entry.id).descriptor.trials.budget58 = entry.budget;
        }
        presetDirty = false;
        weights = strategyWeights(entries);
        schedule = {0, trialLimit, int32_t(global.limit()), poolGrowth};
        scheduleRandom = Random(uint32_t(truncateInt32(program.globalSeedOffset)));
        SupervisorTrace::Stage stage; stage.phase = phase; stage.seconds = seconds;
        for (const auto &entry : entries) stage.strategies.push_back(entry.id);
        stage.worker = runNesterWorker(*this);
        trace.stages.push_back(std::move(stage)); trace.fastEnded |= endedFast;
    }
    void postOptimize(double untilElapsed) {
        if (!program.usePostOptimization || !global.size()) return;
        const double seconds = std::min(untilElapsed - elapsed(), secondsLeft(parent));
        if (!(program.theoreticalSecond828 / 100. < seconds)) return;
        const auto snapshot = global.values();
        const auto &best = recordNesting(snapshot.front());
        SupervisorShakerInput mode;
        mode.width = input.width; mode.height = input.height;
        mode.placedArea = best.quantity18; mode.placedCount = best.placedCount13d8;
        mode.specialTechnology3c = program.engineBudget.specialTechnology3c;
        mode.restrictedC9 = program.engineBudget.restrictedC9;
        const int32_t operation = supervisorShakerOperation(mode);
        size_t cursor = 0;
        const auto began = std::chrono::steady_clock::now();
        for (;;) {
            const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count();
            const auto task = nextShakerTask(cursor, snapshot.size(), ticks, 0., seconds, seconds,
                [&] { return secondsLeft(parent) <= 0.; });
            if (!task.index) break;
            if (!(snapshot[*task.index]->timings[{operation, 0}] <= 0.)) continue;
            ++trace.postOptimizationTasks;
            ++trace.postOptimizationOperations[operation];
            const auto sourceOwner = snapshot[*task.index]->nesting;
            const auto &source = dynamic_cast<const SupervisorNesting &>(*sourceOwner);
            try {
                const auto taskStarted = std::chrono::steady_clock::now();
                Budget time(task.seconds); time.deadline = std::min(time.deadline, parent.deadline);
                bool accepted = false;
                const auto output = optimize(input, source.state.output, time, operation,
                    applicability.mapInput.pitch230, measures.height220, &accepted,
                    source.terrain?&*source.terrain:nullptr,&trace.postOptimizationGuides,&trace.postOptimizationPointRows,
                    program.predefined?&*program.predefined:nullptr,source.state.placementHost->nesting.placements,
                    &trace.postOptimizationHeadRows);
                auto n = std::make_shared<SupervisorNesting>();
                // Reconstruct catalog/lifecycle from accepted geometry, keeping
                // the source owner immutable for history and warm replay.
                phaseBudget = std::make_unique<Budget>(std::max(secondsLeft(parent), .001));
                initialize(*n);
                for (const auto &p : output.placements) ordinaryCommit(n->state, p);
                copyOutputRelations(source.state.placementHost->nesting.placements,n->state);
                static_cast<TrialNesting &>(*n) = static_cast<const TrialNesting &>(source);
                n->lineage14f8 = source.lineage14f8; n->fraction1510 = source.fraction1510;
                n->source1518 = source.source1518; n->sourceTrial151c = source.sourceTrial151c; n->depth1520 = source.depth1520;
                n->shake14e8 = accepted ? 1. : source.shake14e8;
                sync(*n, snapshot[*task.index]->sourceId);
                // 4444d0 updates the selected result in place. Its original
                // length (+10) remains the pre-Shaker baseline for training.
                auto record = snapshot[*task.index]; record->nesting = n;
                record->corrected = {true, n->metrics.correctedLength};
                recordTiming(*record, {operation, 0}, std::chrono::duration<double>(std::chrono::steady_clock::now() - taskStarted).count());
                if (n->maximumRight < source.maximumRight) ++trace.postOptimizationAccepted;
            } catch (const Failure &error) {
                if (error.code != PN2S_TIMEOUT) throw;
                break;
            }
        }
        // The original rebuilds the global tree after mutating result keys.
        global.rebuild();
    }
    void updateShakerGain() {
        if (program.engineBudget.rectangleModeC8 || program.engineBudget.specialTechnology3c ||
            program.engineBudget.restrictedC9) return;
        trace.shakerGainSamples.clear();
        for (const auto &record : global.values()) {
            const auto &n = recordNesting(record);
            ShakerGainInput value;
            value.target14b8 = totalArea; value.queue20 = value.placed10 = n.quantity18;
            value.originalLength10 = record->rawLength; value.maximumRight = n.maximumRight;
            trace.shakerGainSamples.push_back(measuredShakerGain(value));
        }
        trace.medianShakerGain = recovered::medianShakerGain(trace.shakerGainSamples);
        if (useExpectedShakerGain(trace.medianShakerGain)) expectedShakerGain = trace.medianShakerGain;
        trace.expectedShakerEvaluator = expectedShakerGain.has_value();
    }
    bool stopped() override { return expired || secondsLeft(parent) <= 0 || !phaseBudget || secondsLeft(*phaseBudget) <= 0; }
    bool fastEnded() override { return endedFast; }
    std::optional<int> nextManager() override {
        const auto index = nextStrategy(scheduleRandom, schedule, weights);
        if (!index) return {};
        if (uint32_t(schedule.poolLimit) > global.limit()) global.rebuild(uint32_t(schedule.poolLimit));
        return entries.at(*index).id;
    }
    void construct(int id) override {
        activeId = id; ++trace.trials; ++trace.managerAttempts[id];
        try {
            const auto result = runConstruction(*this);
            trace.localAccepted += result.localInserted; trace.globalAccepted += result.globalInserted;
            trace.bestUpdates += result.newBest;
        } catch (const Failure &error) {
            if (error.code != PN2S_TIMEOUT) throw;
            expired = true; trace.timedOut = true;
        }
    }
    double remaining() override { return phaseBudget ? secondsLeft(*phaseBudget) : 0.; }
    bool inspectFastEnd() override { return program.fastEndReferenceLength.has_value(); }
    std::optional<FastEndInput> bestCandidate() override {
        if (!global.best()) return {};
        const auto &n = recordNesting(*global.best());
        FastEndInput result; result.maximumRight = n.maximumRight;
        result.referenceLength = program.fastEndReferenceLength.value_or(0.);
        result.placedArea = result.queue20 = n.quantity18; result.target14b8 = totalArea;
        return result;
    }
    void markFastEnd() override { endedFast = true; }
    TrialManagerState &manager() override { return managers.at(activeId).descriptor.trials; }
    std::shared_ptr<TrialNesting> &current() override { return currentNesting; }
    void lockManager() override {}
    void unlockManager() override {}
    void lockSupervisor() override {}
    void unlockSupervisor() override {}
    int32_t managerId() override { return activeId; }
    double config4e0() override { return 0.; }
    void preprocess() override {}
    void finalizeGeometry() override { sync(nesting(), activeId); }
    int64_t rawLength(const TrialNesting &n) override { return dynamic_cast<const SupervisorNesting &>(n).maximumRight; }
    void evaluate(const TrialRecordPtr &record) override {
        auto &n = dynamic_cast<SupervisorNesting &>(*record->nesting);
        sync(n, record->sourceId);
        correctedLength(n,record->corrected);
    }
    CandidatePool<TrialRecordPtr>::Inserted notifyLocal(const TrialRecordPtr &record) override {
        if (!strategySupported||(program.predefined&&!validCandidate(recordNesting(record).state.output))||
            !validPointsProximity(input,recordNesting(record).state.output,input.pointGroups)) return {};
        return managers.at(activeId).pool->notify(record,
            [this](const auto &first, const auto &second) { return related(first, second); });
    }
    CandidatePool<TrialRecordPtr>::Inserted notifyGlobal(const TrialRecordPtr &record) override {
        return strategySupported&&(!program.predefined||validCandidate(recordNesting(record).state.output))&&
            validPointsProximity(input,recordNesting(record).state.output,input.pointGroups) ? global.notify(record,
            [this](const auto &first, const auto &second) { return related(first, second); }) : CandidatePool<TrialRecordPtr>::Inserted{};
    }
    void publishBest(const TrialRecordPtr &) override {}
    bool hasBestCallback() override { return false; }
    int64_t clockTicks() override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    double preciseSeconds() override { return constructionSeconds(clockTicks()); }
    void resetCurrent() override {
        auto &n = nesting();
        if (n.state.catalog && n.state.placementHost) ordinaryRestore(n.state, 0);
        else initialize(n);
        n.element14c0 = history.fixedElement;
        sync(n, activeId);
    }
    std::optional<Best> bestGoal() override {
        if (!global.best()) return {};
        const auto &n = recordNesting(*global.best()); return Best{n.metrics.metric, input.height, n.maximumRight};
    }
    void resetRandom(int32_t salt) override {
        auto &n = nesting(); resetTrialRandom(n.random, program.globalSeedOffset, uint32_t(salt));
        n.seed13b8 = uint32_t(salt);
    }
    int phase() override { return phaseValue; }
    void prepareWarmStart(double fraction, bool high) override {
        const auto values = global.values();
        const auto selected = selectWarmStart(values, fraction, high, nesting().seed13b8);
        if (!selected) return;
        selectedWarm = values.at(*selected); ++trace.warmSelections;
        const auto &selectedNesting=recordNesting(*selectedWarm);
        if(fraction<1.&&std::any_of(selectedNesting.placements.begin(),selectedNesting.placements.end(),[](const auto &p){return bool(p->group58);})){ 
            trace.missingProviders.insert("warm.multiheadsPartialGroupSelection");return;
        }
        WarmAdapter host(*this, recordNesting(*selectedWarm));
        trace.warmRetained += applyWarmStart(host, *selectedWarm, fraction, high).copied;
    }
    Worst worstCandidate() override {
        if (!global.worst()) return {true, 0, 0};
        const auto &n = recordNesting(*global.worst()); return {false, n.metrics.correctedLength, n.shake14e8};
    }
    double evaluatorFactor() override { return expectedShakerGain.value_or(0.); }
    void runPreNester() override {
        if(!program.predefined||program.predefined->records.empty())return;
        OrdinaryPredefinedTrace record;
        ordinaryApplyPredefined(input,*program.predefined,*phaseBudget,nesting().state,&record);
        ++trace.predefinedPhases;trace.predefinedCommitted+=record.committed.size();
        sync(nesting(),activeId);
    }
    void prepareModules() override {}
    bool useHistory() override { return program.useDynamicStructuringElements; }
    uint32_t prepareHistory() override {
        history.phase1c8 = phase();
        std::optional<int32_t> attempts;
        if (const auto found = managers.find(history.targetManager29d8); found != managers.end()) attempts = found->second.descriptor.trials.count60;
        const auto result = recovered::prepareHistory(history, nesting(),
            std::chrono::duration<double>(std::chrono::steady_clock::now() - phaseStarted).count(), attempts);
        if (result.choice) { ++trace.historyChoices; return uint32_t(result.choice->index); }
        return 0;
    }
    void createStrategy() override {
        auto &n = nesting(); const auto &descriptor = managers.at(activeId).descriptor;
        strategySupported = true;
        const auto base = descriptor.baseId;
        if ((base != 1 && base != 2 && base != 3 && base != 4 && base != 5 && base != 6 && base != 7 && base != 8 && base != 9 && base != 10 &&
             !program.additionalFactories.contains(base)) ||
            (base == 6 && !program.problemEvaluation && !program.additionalFactories.contains(base))) {
            trace.unsupportedManagers.insert(activeId); strategySupported = false; runner = {}; return;
        }
        Input active = input; active.randomSeed = uint32_t(truncateInt32(program.globalSeedOffset)) + n.seed13b8;
        active.lOffcut=program.engineBudget.specialTechnology3c;active.minimumLOffcutHeight=program.technologyMinimum40;
        if (descriptor.reversed) {
            auto random = n.random;
            resetTrialRandom(random, program.globalSeedOffset, n.seed13b8);
            ReverseRuntime metadata;
            metadata.configuredSeed = program.globalSeedOffset; metadata.salt = n.seed13b8;
            metadata.trial = n.trial13bc; metadata.bound = n.bound14a8; metadata.shake = n.shake14e8;
            metadata.lineage = n.lineage14f8; metadata.fraction = n.fraction1510;
            metadata.lineageFields = {uint32_t(n.source1518), uint32_t(n.sourceTrial151c), uint32_t(n.depth1520), 0, 0};
            runner = [this, random, base, metadata](const Input &source, Budget &time, OrdinaryState &state) {
                const auto count = state.output.placements.size();
                ordinaryReverse(source, [this, random, base](const Input &reversed) { return createChild(reversed, base, random); },
                                time, &state, nullptr, &metadata);
                return int(state.output.placements.size() - count);
            };
        } else runner = createChild(active, descriptor.baseId, n.random);
    }
    void attachStop() override {}
    void runStrategy() override {
        if (!runner) return;
        auto &n = nesting(); Input active = input;
        active.lOffcut=program.engineBudget.specialTechnology3c;active.minimumLOffcutHeight=program.technologyMinimum40;
        active.randomSeed = uint32_t(truncateInt32(program.globalSeedOffset)) + n.seed13b8;
        const auto before = n.state.output.placements.size();
        const auto noMapQueries=n.state.noMapCandidateQueries,noMapCommitted=n.state.noMapCommitted;
        runner(active, *phaseBudget, n.state);
        trace.noMapCandidateQueries+=n.state.noMapCandidateQueries-noMapQueries;
        trace.noMapCommitted+=n.state.noMapCommitted-noMapCommitted;
        trace.managerPlacements[activeId] += n.state.output.placements.size() - before;
        if (n.state.placementHost) {
            const auto &native = n.state.placementHost->nesting;
            n.random = native.random;
            if (managers.at(activeId).descriptor.reversed) {
                n.seed13b8 = native.seed13b8; n.trial13bc = native.trial13bc; n.bound14a8 = native.bound14a8;
                n.shake14e8 = native.shake14e8; n.lineage14f8 = native.lineage14f8;
                n.fraction1510 = native.fraction1510; n.source1518 = native.source1518;
                n.sourceTrial151c = native.sourceTrial151c; n.depth1520 = native.depth1520;
            }
        }
        sync(n, activeId);
    }
    void finalizeStrategy() override { runner = {}; }
    void completeHistory(uint32_t index) override {
        const auto &n = nesting(); HistoryCompletionInput record;
        record.target14b8 = totalArea; record.queue20 = record.placedArea = n.quantity18;
        record.maximumRight = n.maximumRight; record.managerId = activeId; record.index = int32_t(index);
        record.elapsed = elapsed();
        trace.historyUpdates += recovered::completeHistory(history, n, record).updated;
    }
    bool mayShake() override {
        if (!expectedShakerGain) return false;
        return shouldShake(global.values(), currentNesting, [&](const auto &owner) {
            auto record = std::make_shared<TrialRecord>(); record->nesting = owner; record->sourceId = activeId;
            evaluate(record); return record;
        }, [this](const auto &first, const auto &second) {
            const auto &a = recordNesting(first), &b = recordNesting(second);
            return expectedCandidateBetter(a.metrics, a.shake14e8, b.metrics, b.shake14e8, *expectedShakerGain);
        });
    }
    bool shake() override {
        if (!program.usePostOptimization) return false;
        auto &n = nesting(); bool accepted = false;
        const auto output = optimize(input, n.state.output, *phaseBudget, 1,
            applicability.mapInput.pitch230, measures.height220, &accepted,nullptr,nullptr,&trace.postOptimizationPointRows,
            program.predefined?&*program.predefined:nullptr,n.state.placementHost->nesting.placements,&trace.postOptimizationHeadRows);
        if (!accepted) return false;
        const auto sourceOwners=n.state.placementHost->nesting.placements;
        ordinaryRestore(n.state, 0); for (const auto &p : output.placements) ordinaryCommit(n.state, p);
        copyOutputRelations(sourceOwners,n.state);
        n.shake14e8 = 1.; sync(n, activeId); return true;
    }
    void logTrial(const TrialRecordPtr &record, double) override {trace.candidateCosts.push_back(costRecord(recordNesting(record)));}
    void releaseCurrentResources() override { runner = {}; selectedWarm.reset(); }
    std::shared_ptr<TrialNesting> acquireNesting() override { return std::make_shared<SupervisorNesting>(); }
    Output result() const { return global.best() ? recordNesting(*global.best()).state.output : Output{}; }
    std::optional<SupervisorTrace::CandidateCost> resultCost()const{
        if(!global.best())return {};return costRecord(recordNesting(*global.best()));
    }
    std::vector<std::vector<std::pair<size_t,size_t>>> resultSisters()const{
        std::vector<std::vector<std::pair<size_t,size_t>>> result;if(!global.best())return result;
        const auto &n=recordNesting(*global.best());multiheadsShakerConstraints(n.placements);
        for(const auto &p:n.placements)if(p->group58&&p->group58->members.front().lock()==p){
            result.emplace_back();for(const auto &slot:p->group58->members){const auto owner=std::static_pointer_cast<WarmPlacement>(slot.lock());
                const auto &placement=n.instancePlacements.at(owner->instance18);result.back().emplace_back(placement.group,placement.ordinal);}}
        return result;
    }
    bool resultValid(const Output &output)const{return validCandidate(output);}
};
}
Output ordinaryDefault(const Input &input, Budget &budget, SupervisorTrace *trace) {
    SupervisorProgram program;program.engineBudget.specialTechnology3c=input.lOffcut;
    program.technologyMinimum40=input.minimumLOffcutHeight;
    return ordinaryDefault(input,budget,program,trace);
}
Output ordinaryImproveLOffcut(const Input &input,const Output &source,Budget &budget) {
    if(source.placements.empty())return source;
    require(input.gap==0 && !requires_general(input),PN2S_UNSUPPORTED,"L-offcut improvement requires normalized ordinary polygons");
    const auto context=ordinaryContext(input);
    const std::array<PitchSheet,1> sheets{{{input.width,0,0}}};
    const int64_t pitch=contextPitch(sheets,input.pitch,context.width218,50.,false);
    const auto admitted=[&]{uint64_t vertices=0;for(const auto &placed:source.placements)vertices+=input.parts.at(placed.group).polygon.size();
        return multiLastPolishSeconds(secondsLeft(budget),int32_t(source.placements.size()),vertices).has_value();};
    if(!input.lOffcut)return admitted()?optimize(input,source,budget,2,pitch,context.height220):source;
    const double minimum=input.minimumLOffcutHeight*1000.;
    const int32_t quantized=truncateInt32(minimum<0?minimum-.5:minimum+.5);
    const auto terrain=ordinaryTerrain(input,source,pitch,budget,quantized);
    if(!admitted())return source;
    bool accepted=false;
    const auto candidate=optimize(input,source,budget,2,pitch,context.height220,&accepted,&terrain);
    if(!accepted)return source;
    const auto improved=ordinaryTerrain(input,candidate,pitch,budget,quantized);
    const auto value=[&](const ShakerTerrain &stairs,const Output &output){
        StripQuantityCache cache;refreshStripQuantities(cache,stairs.heights,stairs.upper98-stairs.lower94,stairs.area80,pitch);
        return candidateQuantity260(maximumRight(input,output),0,input.width,input.height,true,cache,[](auto &){});
    };
    return value(improved,candidate)>=value(terrain,source)?candidate:source;
}
double ordinaryLOffcutValue(const Input &input,const Output &source,Budget &budget) {
    const auto context=ordinaryContext(input);const std::array<PitchSheet,1> sheets{{{input.width,0,0}}};
    const int64_t pitch=contextPitch(sheets,input.pitch,context.width218,50.,false);
    const double minimum=input.minimumLOffcutHeight*1000.;
    const auto terrain=ordinaryTerrain(input,source,pitch,budget,truncateInt32(minimum<0?minimum-.5:minimum+.5));
    StripQuantityCache cache;refreshStripQuantities(cache,terrain.heights,terrain.upper98-terrain.lower94,terrain.area80,pitch);
    return candidateQuantity260(maximumRight(input,source),0,input.width,input.height,true,cache,[](auto &){});
}
Output ordinaryDefault(const Input &input, Budget &budget, const SupervisorProgram &program, SupervisorTrace *trace) {
    if (input.parts.empty()) return {};
    budget.check(true);
    SupervisorTrace local; auto &record = trace ? *trace : local;
    const double remaining = secondsLeft(budget);
    auto timing = program.engineBudget; timing.remaining = remaining;
    auto phases = recovered::singleEngineBudgets(timing);
    if (std::isfinite(program.strategyFraction)) {
        require(program.strategyFraction >= 0. && program.strategyFraction <= 1., PN2S_INVALID, "Default strategy budget fraction");
        const double construction = remaining * program.strategyFraction;
        phases.selectionA0 = program.phaseFractions[0] * construction;
        phases.refinementA8 = program.phaseFractions[1] * construction;
        phases.continuationB0 = program.phaseFractions[2] * construction;
    }
    const double first = phases.selectionA0, second = phases.refinementA8, third = phases.continuationB0;
    SupervisorHost host(input, budget, program, record);
    host.runStage(first, std::max(program.constructionPhases[0], 0), program.selectionTrialLimit, program.selectionPoolGrowth);
    const auto selectionTrials = host.configuredTrials();
    const int automaticPhase = recovered::multiheadsAutomaticConstructionPhase(
        program.engineBudget.rectangleModeC8,selectionTrials,program.multiheads.heads48);
    host.setSelectedPreset();
    host.runStage(second + first, program.constructionPhases[1] < 0 ? automaticPhase : program.constructionPhases[1],
                  program.optimizationTrialLimit, program.optimizationPoolGrowth);
    host.postOptimize(remaining);
    host.updateShakerGain();
    host.runStage((second + first) + third, program.constructionPhases[2] < 0 ? automaticPhase : program.constructionPhases[2],
                  program.optimizationTrialLimit, program.optimizationPoolGrowth);
    host.postOptimize(remaining);
    record.candidateCount = host.poolSize();
    if (!record.candidateCount) {
        if (record.timedOut || secondsLeft(budget) <= 0.)
            throw Failure(PN2S_TIMEOUT, "Default supervisor exhausted its budget before publishing a completed trial");
        if (!record.unsupportedManagers.empty())
            throw Failure(PN2S_UNSUPPORTED, "No concrete default strategy geometry provider accepted this input");
    }
    auto result = host.result();
    record.sisterGroups=host.resultSisters();
    record.bestCost=host.resultCost();
    require(host.resultValid(result), PN2S_INTERNAL, "Default supervisor candidate material validity");
    require(validPointsProximity(input,result,input.pointGroups),PN2S_INTERNAL,"Default supervisor point proximity validity");
    return result;
}
}
