#include "core.hpp"
#include "ordinary_runtime.hpp"
#include "ordinary_supervisor.hpp"
#include "ordinary_pricing.hpp"
#include "ordinary_multi_predefined.hpp"
#include "ordinary_pair_map.hpp"
#include "recovered_scheduler.hpp"
#include "recovered_rich_conversion.hpp"
#include "recovered_convolution_floating.hpp"
#include "shaker_runtime.hpp"
#include "recovered_shaker_mobility.hpp"
#include "recovered_multi.hpp"
#include "recovered_multi_modify.hpp"
#include "recovered_sdk_contour.hpp"
#include "recovered_sdk_sampling.hpp"
#include "recovered_sdk_writeback.hpp"
#include "recovered_multiheads_sdk.hpp"
#include "api_material_types.hpp"
#include <mutex>
#include <type_traits>
#include <unordered_map>
namespace {
using namespace study;
using Id = uintptr_t;
struct Contour {
    std::shared_ptr<recovered::RichGraph> graph;
    const recovered::RichNode *curve;
};
struct Shape {
    Polygon polygon;
    Point pivot;
    double protection = 0;
    std::vector<Polygon> holes;
};
struct Part {
    Id shape;
    std::vector<Pose> poses;
    uint32_t glueMode=2;
    bool materialRoute=false;
};
struct Sheet {
    double width, height;
    Polygon outer;
    std::vector<Polygon> defects;
    std::vector<double> defectOffsets;
    double uniformBorderGap=0;
    bool materialRoute=false;
};
struct Located {
    Id part;
    Point position;
    PN2_Orientation orientation;
};
struct Result {
    Id sheet;
    std::vector<Located> placed;
    double length = 0;
    std::vector<std::vector<Id>> multiheadsSisters;
    std::shared_ptr<const MaterialResultMetadata> materialInfo;
};
struct PredefinedRequest {std::vector<Located> records;bool manual=false;};
struct Multi {
    std::vector<Id> results;
    std::vector<Id> unplaced;
    std::shared_ptr<OrdinaryMultiPredefinedScene> predefinedScene;
    std::shared_ptr<const MaterialResultMetadata> materialInfo;
};
struct Schedule {
    std::vector<recovered::StrategyEntry> entries;
    std::vector<double> weights;
    recovered::ScheduleState state;
    recovered::Random rng;
    explicit Schedule(uint32_t seed):rng(seed){}
};
struct Session {
    std::map<Id, Contour> contours;
    std::map<Id, Shape> shapes;
    std::map<Id, Part> parts;
    std::map<Id, Sheet> sheets;
    std::map<Id, Result> results;
    std::map<Id, Multi> multis;
    std::map<Id, Schedule> schedules;
    int strategy = PN2S_DEFAULT_RECOVERED;
    double pitch = 0, gap = 0;
    bool fastConstruction = false;
    bool materialGap = false;
    bool lOffcut = false, suppressLOffcut = false;
    double minimumLOffcutHeight = 0;
    uint32_t randomSeed = 1;
    recovered::MultiheadsSdkState multiheads;
};
void apiMultiheadsConfigureSupervisor(const Session &,SupervisorProgram &);
void apiMultiheadsStoreTrace(Result &,const SupervisorTrace &,const std::vector<std::vector<Id>> &);
void apiMultiheadsRebindSisters(const Result &,Result &,const std::map<Id,Id> &);
std::mutex mutex;
std::map<Id, Session> sessions;
Id nextId = 0x10000;
thread_local std::string detail;
Id token() {
    require(nextId < std::numeric_limits<Id>::max() / 2 - 16, PN2S_RESOURCE_LIMIT, "Handle limit");
    nextId += 16;
    return nextId;
}
template <class T> Id id(T p) {
    return reinterpret_cast<Id>(p);
}
template <class T> T handle(Id n) {
    return reinterpret_cast<T>(n);
}
template <class T> T error(int code) {
    if constexpr (std::is_pointer_v<T>)
        return reinterpret_cast<T>(static_cast<intptr_t>(code));
    else
        return static_cast<T>(code);
}
template <class T, class F> T invoke(F f) noexcept {
    try {
        std::lock_guard lock(mutex);
        detail.clear();
        return f();
    } catch (const Failure &e) {
        try {
            detail = e.what();
        } catch (...) {
        }
        return error<T>(e.code);
    } catch (const std::bad_alloc &) {
        return error<T>(PN2S_RESOURCE_LIMIT);
    } catch(const std::exception &e) {
        try {detail=e.what();}catch(...){}
        return error<T>(PN2S_INTERNAL);
    } catch (...) {
        return error<T>(PN2S_INTERNAL);
    }
}
Session &session(PN2_Session s) {
    auto it = sessions.find(id(s));
    require(it != sessions.end(), PN2S_INVALID, "Invalid or expired session");
    return it->second;
}
template <class T> T &lookup(std::map<Id, T> &objects, Id i) {
    auto it = objects.find(i);
    require(it != objects.end(), PN2S_INVALID, "Invalid, expired, wrong-type or cross-session handle");
    return it->second;
}
void validateContourPoint(PN2_Point point) {
    require(std::isfinite(point.x) && std::isfinite(point.y) &&
            std::abs(point.x) <= 1e6 && std::abs(point.y) <= 1e6,
            PN2S_INVALID, "Invalid contour coordinate");
}
PN2_Contour createContour(Session &s, const PN2_Point *points, int count,
    recovered::SdkContourKind kind, const double *values = nullptr,
    const PN2_Point *centers = nullptr, const PN2_Bool *directions = nullptr) {
    require(points && count >= 2 && count <= 8192, PN2S_INVALID, "Need 2..8192 contour points");
    require(s.contours.size() < 4096, PN2S_RESOURCE_LIMIT, "Contour limit");
    std::vector<recovered::ShakerPoint> vertices, arcCenters;
    vertices.reserve(size_t(count));
    if (centers) arcCenters.reserve(size_t(count));
    for (int index = 0; index < count; ++index) {
        validateContourPoint(points[index]);
        vertices.push_back({points[index].x, points[index].y});
        if (values) require(std::isfinite(values[index]) && std::abs(values[index]) <= 1e6,
                            PN2S_INVALID, "Invalid contour arc value");
        if (centers) {
            validateContourPoint(centers[index]);
            arcCenters.push_back({centers[index].x, centers[index].y});
        }
        if (directions) require(directions[index] == 0 || directions[index] == 1,
                                PN2S_INVALID, "Invalid contour arc direction");
    }
    recovered::SdkContourInput input;
    input.points = vertices;
    input.kind = kind;
    if (values) input.values = {values, size_t(count)};
    input.centers = arcCenters;
    if (directions) input.directions = {directions, size_t(count)};
    auto graph = std::make_shared<recovered::RichGraph>();
    const recovered::RichNode *curve;
    try { curve = &recovered::sdkBuildContour(*graph, input); }
    catch (const std::invalid_argument &e) { throw Failure(PN2S_INVALID, e.what()); }
    require(curve->first && curve->last && curve->last->next, PN2S_INVALID, "Contour is not closed");
    const auto key = token();
    s.contours.emplace(key, Contour{std::move(graph), curve});
    return handle<PN2_Contour>(key);
}
Polygon contourPolygon(const Contour &contour, bool exterior) {
    std::vector<recovered::ShakerPoint> points;
    try { points = recovered::sdkSampleContour(*contour.curve, exterior); }
    catch (const std::invalid_argument &e) { throw Failure(PN2S_INVALID, e.what()); }
    Polygon polygon;
    polygon.reserve(points.size());
    for (const auto point : points) polygon.push_back({point[0], point[1]});
    if (area(polygon) < 0) std::reverse(polygon.begin(), polygon.end());
    validate_polygon(polygon);
    return polygon;
}
std::vector<Pose> readPoses(const PN2_Orientation *poses, int count, int maximum = 32) {
    require(poses && count > 0 && count <= maximum, PN2S_INVALID, "Supplied pose count exceeds the selected API limit");
    std::vector<Pose> out;
    for (int i = 0; i < count; i++) {
        auto p = poses[i];
        require(std::isfinite(p.min_angle) && std::isfinite(p.max_angle) &&
                    std::abs(p.min_angle) <= 1e6 && std::abs(p.max_angle) <= 1e6,
                PN2S_INVALID, "Invalid angle");
        require(p.max_angle >= p.min_angle && p.max_angle - p.min_angle <= 360, PN2S_INVALID,
                "Angle interval must be ordered and at most 360 degrees");
        require(p.horizontal_flip == 0 || p.horizontal_flip == 1, PN2S_INVALID, "Mirror must be 0 or 1");
        out.push_back({p.min_angle, p.horizontal_flip != 0, p.max_angle});
    }
    return out;
}
bool same(const Part &a, const Part &b) {
    if (a.shape != b.shape || a.poses.size() != b.poses.size())
        return false;
    for (size_t i = 0; i < a.poses.size(); i++)
        if (a.poses[i].angle != b.poses[i].angle || a.poses[i].flip != b.poses[i].flip ||
            a.poses[i].upper != b.poses[i].upper)
            return false;
    return true;
}
Id publishMulti(Session &s,std::vector<Result> boards,Multi multi){
    std::vector<Id> added;added.reserve(boards.size());multi.results.reserve(boards.size());
    const auto key=token();
    try{for(auto &board:boards){const auto result=token();s.results.emplace(result,std::move(board));
            added.push_back(result);multi.results.push_back(result);}
        s.multis.emplace(key,std::move(multi));
    }catch(...){for(const auto result:added)s.results.erase(result);throw;}
    return key;
}
#include "api_material.inl"

Result solve(Session &s, Id sheetId, const PN2_Part *parts, int count, Budget &budget,
             const PN2S_Direction14Options *direction=nullptr,const double *values=nullptr,std::optional<int> limit18=std::nullopt,
             const BtProgram::Direction10 *direction10=nullptr,const BtProgram *combined=nullptr,const MultiProgram *multi=nullptr,
             const int *prices=nullptr,const PredefinedRequest *predefined=nullptr) {
    auto sheet = lookup(s.sheets, sheetId);
    require(count >= 0 && count <= 10000 && (count == 0 || parts), PN2S_INVALID,
            "Need 0..10000 part instances");
    budget.check(true);
    std::vector<Id> materialInstances;
    materialInstances.reserve(size_t(count));
    for (int i=0; i<count; ++i) materialInstances.push_back(id(parts[i]));
    const std::array<Id,1> materialStocks{sheetId};
    if (apiNeedsMaterial(s, materialStocks, materialInstances)) {
        require(!direction && !values && !limit18 && !direction10 && !combined && !multi &&
                    !prices && !predefined,
                PN2S_UNSUPPORTED, "Material-sheet route does not support predefined, priced or dedicated recovered strategy requests");
        return apiMaterialSolve(s, sheetId, parts, count, budget);
    }
    Input input;
    input.width = sheet.width;
    input.height = sheet.height;
    input.pitch = s.pitch;
    input.gap = s.gap;
    input.fastConstruction = s.fastConstruction;
    input.recoveredOffset = !prices && s.strategy == PN2S_OFFSET_RECOVERED;
    input.randomSeed = s.randomSeed;
    input.lOffcut=s.lOffcut && !s.suppressLOffcut;
    input.minimumLOffcutHeight=s.minimumLOffcutHeight;
    std::vector<Part> types;
    std::vector<double> groupValues;
    std::vector<recovered::PricedSdkPart> pricedRecords;
    std::vector<Id> pricedShapes;
    std::vector<std::vector<Id>> instances;
    std::set<Id> seen;
    for (int i = 0; i < count; i++) {
        if(direction)require(std::isfinite(values[i]) && values[i]>0,PN2S_INVALID,"Direction generators require positive finite values");
        if(prices)require(prices[i]>0,PN2S_INVALID,"PricedNest prices must be strictly positive integers");
        Id key = id(parts[i]);
        require(seen.insert(key).second, PN2S_INVALID, "A part instance occurs twice in the order");
        const auto &p = lookup(s.parts, key);
        const auto &shape = lookup(s.shapes, p.shape);
        require(shape.protection == 0 || !(direction || multi || input.recoveredOffset || prices), PN2S_UNSUPPORTED,
                "Recovered finite-grid strategies require zero protection offset");
        size_t group = 0;
        for (; group < types.size() && (!same(types[group], p) || (direction && groupValues[group]!=values[i])); group++)
            ;
        if (group == types.size()) {
            require(types.size() < 128, PN2S_RESOURCE_LIMIT, "Maximum 128 distinct part groups");
            types.push_back(p);
            instances.emplace_back();
            input.parts.push_back({shape.polygon, shape.pivot, p.poses, 0, shape.holes, shape.protection});
            if(direction)groupValues.push_back(values[i]);
        }
        instances[group].push_back(key);
        input.parts[group].quantity++;
        if(prices)pricedShapes.push_back(p.shape);
    }
    //5cc270 merges source records by SDK shape ID before5cf580 writes profit.
    //The finite host retains pose subgroups to enforce their instance catalogs;
    //broadcast every ordered source write to those subgroups of the same shape.
    if(prices)for(size_t record=0;record<pricedShapes.size();++record)
        for(size_t group=0;group<types.size();++group)if(types[group].shape==pricedShapes[record])
            pricedRecords.push_back({group,prices[record]});
    OrdinaryPredefinedProgram predefinedProgram;
    if(predefined){
        require(input.gap==0&&!requires_general(input)&&!input.lOffcut,PN2S_UNSUPPORTED,
                "Predefined entry requires finite grid poses, single rings, zero gap/protection and the ordinary objective");
        std::map<Id,std::pair<size_t,size_t>> identities;
        for(size_t group=0;group<instances.size();++group)for(size_t ordinal=0;ordinal<instances[group].size();++ordinal)
            identities.emplace(instances[group][ordinal],std::pair{group,ordinal});
        for(const auto &record:predefined->records){
            const auto found=identities.find(record.part);
            require(found!=identities.end(),PN2S_INVALID,"Predefined part is absent from the concrete job");
            const auto &part=lookup(s.parts,record.part);
            predefinedProgram.records.push_back({found->second.first,found->second.second,
                {record.part,record.position,{record.orientation.horizontal_flip,record.orientation.min_angle,record.orientation.max_angle},part.glueMode}});
        }
    }
    Output output;
    if(count && s.lOffcut)require(s.strategy==PN2S_DEFAULT_RECOVERED && input.gap==0 && !requires_general(input),
        PN2S_UNSUPPORTED,"L-offcut requires the default supervisor, finite grid poses, single rings, zero gap and zero protection");
    if(count && s.lOffcut)require(s.multiheads.hardware.heads48<2,PN2S_UNSUPPORTED,
        "Combined L-offcut and multiple cutting heads require retaining native sister owners through final board polish");
    bool retainedSupervisorCandidate = false;
    SupervisorTrace supervisorTrace;
    if (count) {
        if(predefined){
            if(predefined->manual)output=ordinaryPreNest(input,predefinedProgram,budget);
            else {
                SupervisorProgram program;program.predefined=predefinedProgram;
                apiMultiheadsConfigureSupervisor(s,program);
                output=ordinaryDefault(input,budget,program,&supervisorTrace);
                require(supervisorTrace.candidateCount>0,PN2S_TIMEOUT,"Continuation finished without an evaluated candidate");
                retainedSupervisorCandidate=true;
            }
        }
        else if(prices){
            require(input.gap==0&&!requires_general(input)&&!s.lOffcut,PN2S_UNSUPPORTED,
                    "PricedNest currently requires finite grid poses, single rings, zero gap/protection and the ordinary objective");
            SupervisorProgram program;apiMultiheadsConfigureSupervisor(s,program);
            output=ordinaryPricedNest(input,budget,pricedRecords,program,&supervisorTrace);
            require(supervisorTrace.candidateCount>0,PN2S_TIMEOUT,"Priced supervisor finished without an evaluated candidate");
            retainedSupervisorCandidate=true;
        }
        else if(multi)output=ordinaryMulti(input,*multi,budget);
        else if(direction) {
            BtProgram program;
            if(combined)program=*combined;
            else if(direction10)program.direction10=*direction10;
            else if(limit18)program.direction18=BtProgram::Direction18{direction->direction_scale_218,direction->direction_scale_220,direction->batch_size,*limit18};
            else program.direction14=BtProgram::Direction14{direction->direction_scale_218,direction->direction_scale_220,direction->batch_size};
            program.evaluationValues=groupValues;
            program.reference=static_cast<recovered::ReferenceKind>(direction->reference_kind);
            program.lengthMode=direction->length_mode!=0;
            program.targetField14b8=direction->target_14b8;program.queueAggregate20=direction->queue_20;
            program.maximumLengthX=rounded(direction->maximum_length*1e6);
            output=ordinaryBt(input,program,budget);
        } else if (s.strategy == PN2S_DEFAULT_RECOVERED && ordinaryPreparedDomain(input)) {
            SupervisorProgram program;
            program.engineBudget.specialTechnology3c=input.lOffcut;
            program.technologyMinimum40=input.minimumLOffcutHeight;
            apiMultiheadsConfigureSupervisor(s,program);
            output = ordinaryDefault(input, budget, program, &supervisorTrace);
            require(supervisorTrace.candidateCount > 0, PN2S_TIMEOUT, "Default supervisor finished without an evaluated candidate");
            retainedSupervisorCandidate = true;
        } else if ((requires_general(input)&&(s.strategy==PN2S_MAP_GREEDY||s.strategy==PN2S_RECTANGLE_RECURSIVE||!ordinaryPreparedDomain(input))) ||
                   (input.gap > 0 && s.strategy != PN2S_RECTANGLE_RECURSIVE)) {
            require(!input.recoveredOffset, PN2S_UNSUPPORTED,
                    "Recovered Offset route requires prepared finite-grid rings and zero gap/protection");
            if (!rectangle_range_nest(input, budget, output)) output = general_nest(input, budget);
        } else output = s.strategy == PN2S_RECTANGLE_RECURSIVE ? rectangles(input, budget) : ordinary(input, budget);
    }
    Result result;
    result.sheet = sheetId;
    std::set<Id> placed;
    for (auto p : output.placements) {
        require(p.group < input.parts.size() && p.ordinal < instances[p.group].size() &&
                    p.poseIndex < input.parts[p.group].poses.size(),
                PN2S_INTERNAL, "Kernel output index invalid");
        Id key = instances[p.group][p.ordinal];
        require(placed.insert(key).second, PN2S_INTERNAL, "Kernel repeated an instance");
        auto &type = input.parts[p.group];
        auto pose = type.poses[p.poseIndex];
        if (std::isfinite(p.solvedAngle))
            pose.angle = p.solvedAngle;
        require(pose.angle >= type.poses[p.poseIndex].angle - 1e-8 &&
                    pose.angle <= type.poses[p.poseIndex].upper + 1e-8,
                PN2S_INTERNAL, "Kernel exceeded permitted angle interval");
        Point position{std::isfinite(p.exactX) ? p.exactX : static_cast<double>(p.x) / 1e6,
                       std::isfinite(p.exactY) ? p.exactY : static_cast<double>(p.y) / 1000};
        auto polygon = transform(type.polygon, type.pivot, pose);
        for (auto q : polygon) {
            q.x += position.x;
            q.y += position.y;
            require(predefined || (q.x >= -1e-6 && q.y >= -1e-6 && q.x <= sheet.width + 1e-6 &&
                        q.y <= sheet.height + 1e-6),
                    PN2S_INTERNAL, "Kernel produced a placement outside the sheet");
            result.length = std::max(result.length, q.x);
        }
        result.placed.push_back({key, position, {pose.flip ? 1 : 0, pose.angle, pose.angle}});
    }
    apiMultiheadsStoreTrace(result,supervisorTrace,instances);
    if (!retainedSupervisorCandidate) budget.check(true);
    return result;
}
std::array<double,2> currentAngleBounds(const Part &part, const Located &located) {
    std::vector<std::array<double,2>> intervals;
    for (const auto &pose : part.poses)
        if (pose.flip == bool(located.orientation.horizontal_flip))
            intervals.push_back({pose.angle,pose.upper});
    std::sort(intervals.begin(), intervals.end());
    std::vector<std::array<double,2>> merged;
    for (const auto interval : intervals) {
        if (merged.empty() || interval[0] > merged.back()[1])
            merged.push_back(interval);
        else
            merged.back()[1] = std::max(merged.back()[1], interval[1]);
    }
    for (const auto interval : merged)
        if (located.orientation.min_angle >= interval[0]-1e-8 &&
            located.orientation.min_angle <= interval[1]+1e-8)
            return interval;
    throw Failure(PN2S_INVALID, "Result orientation is outside the part's allowed intervals");
}
Result optimizeResult(Session &s, const Result &source, std::optional<int> compact, double seconds) {
    require(!source.materialInfo, PN2S_UNSUPPORTED,
            "Material layouts already use the shared cross-board optimizer; legacy Shake/Compact technology is not connected to this route");
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 3600, PN2S_INVALID,
            "Time budget must be in (0,3600] seconds");
    Budget budget(seconds);
    budget.check(true);
    const auto sheet = lookup(s.sheets, source.sheet);
    if (source.placed.empty())
        return source;
    recovered::ShakerInputScene scene;
    recovered::ConvolutionCurves curves;
    recovered::shakerConfigureOrdinaryRelations(scene.relations);
    struct PreparedShape {
        size_t geometry;
        bool rotationLocked;
    };
    std::map<Id,PreparedShape> geometries;
    std::vector<std::array<double,2>> intervals;
    Input material;
    material.width = sheet.width;
    material.height = sheet.height;
    material.gap = s.gap;
    for (const auto &located : source.placed) {
        budget.check(true);
        const auto &part = lookup(s.parts, located.part);
        const auto &shape = lookup(s.shapes, part.shape);
        material.parts.push_back({shape.polygon, shape.pivot,
            {{located.orientation.min_angle, bool(located.orientation.horizontal_flip)}},
            1, shape.holes, shape.protection});
        auto geometry = geometries.find(part.shape);
        if (geometry == geometries.end()) {
            recovered::ShakerPolygonInput polygon;
            for (const auto point : shape.polygon)
                polygon.outer.push_back({point.x,point.y});
            for (const auto &hole : shape.holes) {
                polygon.holes.emplace_back();
                for (const auto point : hole)
                    polygon.holes.back().push_back({point.x,point.y});
            }
            const auto index = recovered::shakerCreateGeometry(scene, {shape.pivot.x,shape.pivot.y});
            recovered::shakerAppendPolygonComponents(scene, index, polygon, curves);
            require(!scene.shapes[index].components.empty(), PN2S_INVALID,
                    "Shape collapses at the recovered geometry tolerance");
            const double protection = std::max(s.gap, shape.protection);
            if (protection > 0) {
                auto holes = polygon.holes;
                for (auto &hole : holes)
                    std::reverse(hole.begin(), hole.end());
                // 6722e0 offsets a single exterior and positive hole contours
                // through 7f1bb0. Default table fc22f0[52] supplies resolution 24.
                auto expanded = recovered::convolutionFloatingOffsetPair({polygon.outer}, holes, protection, 24);
                require(expanded[0].size() == 1, PN2S_INTERNAL,
                        "Positive protection offset did not preserve the connected exterior");
                recovered::ShakerPolygonInput zone;
                zone.type = -3;
                zone.offset = protection;
                zone.outer = std::move(expanded[0].front());
                zone.holes = std::move(expanded[1]);
                for (auto &hole : zone.holes)
                    std::reverse(hole.begin(), hole.end());
                recovered::shakerAppendPolygonComponents(scene, index, zone, curves);
            }
            recovered::ShakerRotationMetrics rotationMetrics;
            const std::array<recovered::ShakerSdkPolygon, 1> rotationPolygons{{{std::move(polygon.outer), 1.}}};
            const bool rotationLocked = recovered::shakerGeometryLocksRotation(rotationMetrics, rotationPolygons);
            geometry = geometries.emplace(part.shape, PreparedShape{index, rotationLocked}).first;
        }
        const auto index = recovered::shakerCreatePart(scene, geometry->second.geometry,
            {located.position.x,located.position.y}, located.orientation.min_angle*pi/180,
            {uint8_t(located.orientation.horizontal_flip),0});
        const auto interval = currentAngleBounds(part, located);
        intervals.push_back(interval);
        const bool fixed = compact.has_value() || interval[0] == interval[1] || geometry->second.rotationLocked;
        const int32_t glued=!recovered::predefinedMovable(part.glueMode,true);
        recovered::shakerSetMobility(scene.parts[index], {glued,glued,glued?1:int32_t(fixed)},
            scene.parts[index].bounds, {interval[0]*pi/180,interval[1]*pi/180});
    }
    // Boundary slabs cover the whole permitted sheet and the translated part
    // extent. Their dimensions are host allocation parameters in caller units.
    const double span = std::max(sheet.width, sheet.height);
    recovered::shakerCreateBoundaryGeometry(scene, span*4, span*4, span);
    // 650150 uses the occupied right extent for ordinary Shake/Compact modes.
    // Both upper bounds include half of the SDK's 1e-6 coordinate quantum.
    const auto sdkBounds=recovered::shakerSdkBounds(0,0,0,sheet.width,sheet.height,rounded(source.length*1e6));
    require(recovered::shakerSetBounds(scene,sdkBounds[0],sdkBounds[1],sdkBounds[2],sdkBounds[3]) == 0,
            PN2S_INVALID, "Post-optimization sheet bounds are invalid");
    budget.check(true);
    const double remaining = std::chrono::duration<double>(budget.deadline-
        std::chrono::steady_clock::now()).count();
    std::map<Id,size_t> placedIndexes;
    for(size_t index=0;index<source.placed.size();++index)
        require(placedIndexes.emplace(source.placed[index].part,index).second,PN2S_INTERNAL,"Sister optimization requires unique SDK instances");
    std::vector<recovered::ShakerPairConstraint> headConstraints;
    std::set<Id> sisterMembers;
    for(const auto &group:source.multiheadsSisters){
        require(group.size()>1,PN2S_INTERNAL,"Stored cutting-head group has no sisters");
        for(const auto member:group)require(placedIndexes.contains(member)&&sisterMembers.insert(member).second,
            PN2S_INTERNAL,"Stored cutting-head group lost its unique placed SDK member");
        for(size_t index=1;index<group.size();++index){
            headConstraints.push_back({placedIndexes.at(group.front()),placedIndexes.at(group[index]),0,0,{}});
            headConstraints.push_back({placedIndexes.at(group.front()),placedIndexes.at(group[index]),4,0,{}});
        }
    }
    const auto run = compact
        ? recovered::compactShakerFixedScene(scene, *compact == 1 ? 0 : 1, 2, remaining,0,headConstraints)
        : recovered::shakeShakerScene(scene, 2, true, remaining,0,headConstraints);
    budget.check(true);
    // 650150 only enters the SDK pose-writeback branch after successful scene
    // acceptance. A rejected optimization retains the existing layout.
    if (run.status != 0 || !run.iteration.success)
        return source;
    Result result = source;
    result.length = 0;
    Output candidate;
    for (size_t n = 0; n < result.placed.size(); ++n) {
        auto &located = result.placed[n];
        const auto &solved = scene.parts[n];
        located.position = recovered::shakerSdkFloatingPosition(
            recovered::shakerSdkUnchangedPosition({solved.position[0],solved.position[1]}));
        double angle = located.orientation.min_angle;
        if (solved.angle != scene.inputParts[n].initialAngle)
            angle = solved.angle*180/pi;
        require(angle >= intervals[n][0]-1e-8 && angle <= intervals[n][1]+1e-8,
                PN2S_INTERNAL, "Post-optimization exceeded the permitted angle interval");
        located.orientation.min_angle = located.orientation.max_angle = angle;
        const auto &part = lookup(s.parts, located.part);
        const auto &shape = lookup(s.shapes, part.shape);
        const auto polygon = transform(shape.polygon, shape.pivot,
                                       {angle,bool(located.orientation.horizontal_flip)});
        for (const auto point : polygon) {
            const double x = point.x+located.position.x;
            result.length = std::max(result.length, x);
        }
        Placement placement;
        placement.group = n;
        placement.exactX = located.position.x;
        placement.exactY = located.position.y;
        placement.solvedAngle = angle;
        candidate.placements.push_back(placement);
    }
    if (!valid_layout(material, candidate))
        return source;
    return result;
}
Id nestDirection(Session &s,Id sheetId,const PN2_Part *parts,int count,const double *values,
                 const PN2S_Direction14Options &options,double seconds,std::optional<int> limit18=std::nullopt,
                 const BtProgram::Direction10 *direction10=nullptr,const BtProgram *combined=nullptr) {
    require(count==0 || values,PN2S_INVALID,"Direction values are required");
    require(options.batch_size>0 && options.batch_size<=10000 && options.reference_kind>=0 && options.reference_kind<=2 &&
            (options.length_mode==0 || options.length_mode==1),PN2S_INVALID,"Invalid direction mode or batch");
    require(std::isfinite(options.direction_scale_218) && std::isfinite(options.direction_scale_220) &&
            std::isfinite(options.target_14b8) && std::isfinite(options.queue_20) &&
            std::isfinite(options.maximum_length) && options.maximum_length>=0 && options.maximum_length<=1e6,
            PN2S_INVALID,"Invalid direction upstream fields");
    require(std::isfinite(seconds) && seconds>0 && seconds<=3600,PN2S_INVALID,"Time budget must be in (0,3600] seconds");
    Budget budget(seconds);
    auto result=solve(s,sheetId,parts,count,budget,&options,values,limit18,direction10,combined);
    const Id resultId=token();s.results.emplace(resultId,std::move(result));return resultId;
}
Id nest(Session &s, Id sheetId, const PN2_Part *parts, int count, double seconds) {
    require(std::isfinite(seconds) && seconds > 0 && seconds <= 3600, PN2S_INVALID,
            "Time budget must be in (0,3600] seconds");
    Budget budget(seconds);
    auto result = solve(s, sheetId, parts, count, budget);
    Id key = token();
    s.results.emplace(key, std::move(result));
    return key;
}
Id nestPriced(Session &s,Id sheetId,const PN2_Part *parts,const int *prices,int count,double seconds){
    require(count>0&&count<=10000&&parts&&prices,PN2S_INVALID,"PricedNest needs 1..10000 parts and integer prices");
    require(std::isfinite(seconds)&&seconds>0&&seconds<=3600,PN2S_INVALID,"Time budget must be in (0,3600] seconds");
    const std::vector<int> copiedPrices(prices,prices+count);
    for(const auto price:copiedPrices)require(price>0,PN2S_INVALID,"PricedNest prices must be strictly positive integers");
    Budget budget(seconds);
    auto result=solve(s,sheetId,parts,count,budget,nullptr,nullptr,std::nullopt,nullptr,nullptr,nullptr,copiedPrices.data());
    const auto key=token();s.results.emplace(key,std::move(result));return key;
}
struct ResultGeometry {Input input;Output output;};
ResultGeometry resultGeometry(Session &s,const Result &source,bool offcut) {
    const auto &sheet=lookup(s.sheets,source.sheet);
    Input input;input.width=sheet.width;input.height=sheet.height;input.pitch=s.pitch;input.gap=s.gap;
    input.lOffcut=offcut;input.minimumLOffcutHeight=s.minimumLOffcutHeight;
    Output original;
    for(const auto &located:source.placed) {
        const auto &part=lookup(s.parts,located.part);const auto &shape=lookup(s.shapes,part.shape);
        input.parts.push_back({shape.polygon,shape.pivot,{{located.orientation.min_angle,bool(located.orientation.horizontal_flip)}},1,shape.holes,shape.protection});
        original.placements.push_back({input.parts.size()-1,0,0,rounded(located.position.x*1e6),rounded(located.position.y*1000.)});
    }
    return {std::move(input),std::move(original)};
}
Result polishResult(Session &s,const Result &source,Budget &budget,bool offcut) {
    const auto normalized=resultGeometry(s,source,offcut);
    const auto &input=normalized.input;
    const auto output=ordinaryImproveLOffcut(input,normalized.output,budget);
    Result result=source;result.length=0;
    require(output.placements.size()==result.placed.size(),PN2S_INTERNAL,"L-offcut changed the placed-instance order");
    for(size_t index=0;index<result.placed.size();++index) {
        auto &located=result.placed[index];const auto &placed=output.placements[index];
        require(placed.group==index,PN2S_INTERNAL,"L-offcut changed an instance identity");
        located.position={double(placed.x)/1e6,double(placed.y)/1000.};
        const auto &part=input.parts[index];
        for(const auto point:transform(part.polygon,part.pivot,part.poses[0]))result.length=std::max(result.length,point.x+located.position.x);
    }
    return result;
}
bool copyEquivalentResult(Session &s,const Result &source,Result &target) {
    if(source.sheet!=target.sheet || source.placed.size()!=target.placed.size())return false;
    auto copy=source;std::vector<uint8_t> used(target.placed.size(),0);std::map<Id,Id> rebound;
    for(auto &located:copy.placed){
        const auto &part=lookup(s.parts,located.part);size_t match=0;
        for(;match<target.placed.size();++match)if(!used[match] && same(part,lookup(s.parts,target.placed[match].part)))break;
        if(match==target.placed.size())return false;
        rebound.emplace(located.part,target.placed[match].part);
        located.part=target.placed[match].part;used[match]=1;
    }
    copy.multiheadsSisters.clear();apiMultiheadsRebindSisters(source,copy,rebound);
    target=std::move(copy);return true;
}
bool multiPolishGate(Session &s,const Result &result) {
    const auto &sheet=lookup(s.sheets,result.sheet);
    recovered::SupervisorShakerInput input;input.width=sheet.width;input.height=sheet.height;
    input.specialTechnology3c=s.lOffcut;input.placedCount=int32_t(result.placed.size());
    for(const auto &placed:result.placed)input.placedArea+=area(lookup(s.shapes,lookup(s.parts,placed.part).shape).polygon);
    return recovered::smallNestingShakeGate(input);
}
void polishLOffcutPlan(Session &s,std::vector<Result> &boards,const std::vector<Id> &stocks,
    const std::vector<int> &inventory,double &stockArea,Budget &budget) {
    if(boards.empty())return;
    budget.check(true);
    const double remaining=std::chrono::duration<double>(budget.deadline-std::chrono::steady_clock::now()).count();
    const auto times=recovered::multiPolishBudget(remaining,boards.size(),multiPolishGate(s,boards.back()));
    size_t selected=boards.size()-1;const auto value=[&](const Result &board){auto shape=resultGeometry(s,board,true);return ordinaryLOffcutValue(shape.input,shape.output,budget);};
    double best=value(boards[selected]);
    for(size_t index=boards.size()-1;index-- >0;){const double current=value(boards[index]);if(best<current){best=current;selected=index;}}
    std::rotate(boards.begin()+ptrdiff_t(selected),boards.begin()+ptrdiff_t(selected+1),boards.end());
    auto availableStocks=inventory;
    for(const auto &board:boards){const auto stock=std::find(stocks.begin(),stocks.end(),board.sheet);require(stock!=stocks.end(),PN2S_INTERNAL,"Polish stock identity");--availableStocks[size_t(stock-stocks.begin())];}
    const auto replaceStock=[&](size_t index){
        auto &board=boards[index];const auto current=lookup(s.sheets,board.sheet);
        const recovered::MultiPolishStock original{board.sheet,current.width,current.height};
        const auto query=recovered::multiPolishStockQuery(original,rounded(board.length*1e6),false);
        std::vector<recovered::MultiPolishStock> candidates;
        for(size_t stock=0;stock<stocks.size();++stock){
            if(availableStocks[stock]<=0)continue;const auto &candidate=lookup(s.sheets,stocks[stock]);
            candidates.push_back({stocks[stock],candidate.width,candidate.height});
        }
        std::sort(candidates.begin(),candidates.end(),[](const auto &a,const auto &b){return a.identity<b.identity;});
        const auto choice=query?recovered::multiPolishLookupStock(candidates,*query):std::nullopt;
        if(!choice || !recovered::multiPolishAcceptStock(original,candidates[*choice]))return;
        const auto replacement=candidates[*choice];
        const auto chosen=size_t(std::find(stocks.begin(),stocks.end(),replacement.identity)-stocks.begin());
        const auto old=size_t(std::find(stocks.begin(),stocks.end(),board.sheet)-stocks.begin());
        ++availableStocks[old];--availableStocks[chosen];board.sheet=stocks[chosen];
        stockArea+=replacement.width*replacement.height-current.width*current.height;
    };
    const auto apply=[&](size_t index,double seconds,bool direct){
        size_t vertices=0;for(const auto &placed:boards[index].placed)vertices+=lookup(s.shapes,lookup(s.parts,placed.part).shape).polygon.size();
        const auto time=direct?std::optional<double>(seconds):recovered::multiLastPolishSeconds(seconds,int32_t(boards[index].placed.size()),vertices);
        if(!time)return;
        budget.check(true);const double available=std::chrono::duration<double>(budget.deadline-std::chrono::steady_clock::now()).count();
        Budget slice(*time==0?available:*time);slice.deadline=std::min(slice.deadline,budget.deadline);
        boards[index]=polishResult(s,boards[index],slice,index+1==boards.size());
        if(!direct)replaceStock(index);
    };
    apply(boards.size()-1,times.perLayout,false);
    if(multiPolishGate(s,boards.back()))apply(boards.size()-1,times.reservedLast,true);
    size_t prefix=boards.size();
    if(boards.size()>1){
        prefix=boards.size()-1;size_t index=0;
        while(index<prefix){if(copyEquivalentResult(s,boards.back(),boards[index]))std::swap(boards[index],boards[--prefix]);else ++index;}
    }
    std::vector<Part> types;
    for(const auto &[identity,part]:s.parts)if(std::none_of(types.begin(),types.end(),[&](const auto &type){return same(type,part);}))types.push_back(part);
    const auto order=[&](const Result &board){
        recovered::MultiPolishOrder key;key.sheetUserId=int64_t(board.sheet);
        const auto &sheet=lookup(s.sheets,board.sheet);key.width=sheet.width;key.height=sheet.height;
        for(const auto &placed:board.placed){const auto &part=lookup(s.parts,placed.part);const auto type=std::find_if(types.begin(),types.end(),[&](const auto &candidate){return same(part,candidate);});
            ++key.typeQuantities[int32_t(type-types.begin())];key.quantity18+=area(lookup(s.shapes,part.shape).polygon);key.sortedPartUserIds.push_back(int64_t(part.shape));}
        std::sort(key.sortedPartUserIds.begin(),key.sortedPartUserIds.end());return key;
    };
    std::stable_sort(boards.begin(),boards.begin()+ptrdiff_t(prefix),[&](const auto &first,const auto &second){return recovered::multiPolishLess(order(first),order(second));});
    if(prefix){
        apply(0,times.perLayout,false);
        for(size_t index=1;index<prefix;++index)if(!copyEquivalentResult(s,boards[index-1],boards[index]))apply(index,times.perLayout,false);
    }
}
struct SuppressLOffcut {
    Session &owner;bool saved;
    explicit SuppressLOffcut(Session &s):owner(s),saved(s.suppressLOffcut){owner.suppressLOffcut=true;}
    void restore(){owner.suppressLOffcut=saved;}
    ~SuppressLOffcut(){restore();}
};
double partArea(Session &s, Id part) {
    const auto &sh = lookup(s.shapes, lookup(s.parts, part).shape);
    double a = area(sh.polygon);
    for (const auto &h : sh.holes)
        a -= std::abs(area(h));
    return a;
}
struct Plan {
    std::vector<Result> boards;
    std::vector<Id> left;
    double stockArea = 0;
};
Plan plan(Session &s, const std::vector<Id> &stocks, std::vector<int> available,
          const std::vector<Id> &parts, int policy, Budget &budget) {
    Plan output;
    output.left = parts;
    while (!output.left.empty()) {
        budget.check(true);
        std::vector<PN2_Part> remaining;
        for (Id p : output.left)
            remaining.push_back(handle<PN2_Part>(p));
        bool found = false;
        Result best;
        size_t chosen = 0;
        double score = -1;
        for (size_t stock = 0; stock < stocks.size(); stock++) {
            if (available[stock] == 0)
                continue;
            auto candidate =
                solve(s, stocks[stock], remaining.data(), static_cast<int>(remaining.size()), budget);
            if (candidate.placed.empty())
                continue;
            auto sheet = lookup(s.sheets, stocks[stock]);
            double material = 0;
            for (auto p : candidate.placed)
                material += partArea(s, p.part);
            double value = policy == 0   ? material / (sheet.width * sheet.height)
                           : policy == 1 ? material
                                         : static_cast<double>(candidate.placed.size()) /
                                               (sheet.width * sheet.height);
            if (!found || value > score + 1e-12) {
                found = true;
                score = value;
                chosen = stock;
                best = std::move(candidate);
            }
        }
        if (!found)
            break;
        std::set<Id> consumed;
        for (auto p : best.placed)
            consumed.insert(p.part);
        output.left.erase(std::remove_if(output.left.begin(), output.left.end(),
                                         [&](Id p) { return consumed.contains(p); }),
                          output.left.end());
        available[chosen]--;
        auto sheet = lookup(s.sheets, stocks[chosen]);
        output.stockArea += sheet.width * sheet.height;
        output.boards.push_back(std::move(best));
    }
    // Cross-board repair: remove a pair of patterns and try to merge their actual
    // instance queues onto either existing stock. Quantities are conserved.
    try {
        for (size_t i = output.boards.size(); i-- > 0;) {
            if (i >= output.boards.size())
                continue;
            for (size_t j = 0; j < i; j++) {
                std::vector<PN2_Part> combined;
                for (auto p : output.boards[j].placed)
                    combined.push_back(handle<PN2_Part>(p.part));
                for (auto p : output.boards[i].placed)
                    combined.push_back(handle<PN2_Part>(p.part));
                auto merged = solve(s, output.boards[j].sheet, combined.data(),
                                    static_cast<int>(combined.size()), budget);
                if (merged.placed.size() != combined.size())
                    continue;
                auto removed = lookup(s.sheets, output.boards[i].sheet);
                output.stockArea -= removed.width * removed.height;
                output.boards[j] = std::move(merged);
                output.boards.erase(output.boards.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
        }
    } catch (const Failure &failure) {
        // Construction has already evaluated every required instance. A
        // speculative merge may exhaust the remaining time or resources,
        // while its source boards remain a complete, unmodified plan.
        if (!output.left.empty() ||
            (failure.code != PN2S_TIMEOUT && failure.code != PN2S_RESOURCE_LIMIT)) throw;
    } catch (const std::bad_alloc &) {
        if (!output.left.empty()) throw;
    }
    return output;
}
Plan masterPlan(Session &s, const std::vector<Id> &stocks, const std::vector<int> &inventory,
                const std::vector<Id> &instances, const std::vector<Result> &layouts, Budget &budget) {
    budget.check(true);
    std::vector<Id> types;
    std::map<Id, size_t> groupOf;
    std::vector<std::vector<Id>> queues;
    for (const auto instance : instances) {
        const auto &part = lookup(s.parts, instance);
        size_t group = 0;
        for (; group < types.size() && !same(lookup(s.parts, types[group]), part); ++group) {}
        if (group == types.size()) {
            types.push_back(instance);
            queues.emplace_back();
        }
        groupOf.emplace(instance, group);
        queues[group].push_back(instance);
    }
    std::vector<recovered::MultiStock> stock;
    for (size_t index = 0; index < stocks.size(); ++index) {
        const auto &sheet = lookup(s.sheets, stocks[index]);
        // The public polygon interface uses one unit of material cost per area.
        stock.push_back({inventory[index], sheet.width * sheet.height});
    }
    std::vector<recovered::MultiDemand> demand;
    for (size_t group = 0; group < types.size(); ++group)
        demand.push_back({int32_t(queues[group].size()), partArea(s, types[group])});
    std::vector<recovered::MultiPattern> patterns;
    std::vector<Result> candidateLayouts;
    const auto addColumn = [&](Result layout) {
        if (layout.placed.empty()) return false;
        const auto found = std::find(stocks.begin(), stocks.end(), layout.sheet);
        require(found != stocks.end(), PN2S_INTERNAL, "Pattern refers to unavailable sheet type");
        const auto sheet = size_t(found - stocks.begin());
        recovered::MultiPattern pattern{patterns.size(), sheet, stock[sheet].cost};
        std::map<size_t, int32_t> counts;
        for (const auto &placement : layout.placed) ++counts[groupOf.at(placement.part)];
        for (const auto [group, count] : counts) pattern.parts.push_back({group, count});
        for (size_t index = 0; index < patterns.size(); ++index) {
            const auto &existing = patterns[index];
            if (existing.sheet != pattern.sheet || existing.parts.size() != pattern.parts.size()) continue;
            bool equal = true;
            for (size_t part = 0; part < pattern.parts.size(); ++part)
                if (existing.parts[part].part != pattern.parts[part].part ||
                    existing.parts[part].quantity != pattern.parts[part].quantity) { equal = false; break; }
            if (equal) {
                if (layout.length < candidateLayouts[index].length) candidateLayouts[index] = std::move(layout);
                return false;
            }
        }
        patterns.push_back(std::move(pattern));
        candidateLayouts.push_back(std::move(layout));
        return true;
    };
    for (const auto &layout : layouts) addColumn(layout);
    const auto solveMaster = [&] {
        budget.check(true);
        const auto model = recovered::multiMasterModel(stock, demand, patterns);
        const auto remaining = std::chrono::duration<double>(budget.deadline - std::chrono::steady_clock::now()).count();
        require(remaining>0.,PN2S_TIMEOUT,"Multi-sheet master LP deadline elapsed while building its model");
        return recovered::multiSolveMaster(model, stock, patterns, remaining);
    };
    auto solution = solveMaster();
    recovered::Random random(s.randomSeed);
    if (solution.status == 0) {
        std::vector<recovered::MultiPricingStats> algorithms(stocks.size());
        std::vector<uint8_t> masked(types.size());
        recovered::MultiPricingSelectionHost selection;
        selection.unit = [&] { return random.unit(); };
        selection.integer = [&](int32_t first, int32_t last) { return recovered::multiRandomIndex(random, first, last); };
        selection.weighted = [&](std::span<const double> weights) {
            // A pricing pool can have no accepted columns yet. The original
            // zero-sum alias table has unit thresholds, giving a uniform draw.
            if (std::all_of(weights.begin(), weights.end(), [](double value) { return value == 0; })) {
                const std::vector<double> uniform(weights.size(), 1.);
                return int32_t(recovered::aliasDraw(random, uniform));
            }
            return int32_t(recovered::aliasDraw(random, weights));
        };
        recovered::MultiPricingHost pricing;
        pricing.patternStorageEnabled = true;
        pricing.existingLayout = !candidateLayouts.empty();
        pricing.snapshotPrices = [&] { return solution.partPrices; };
        pricing.availableQuantity = [&](size_t group) { return int32_t(queues[group].size()); };
        pricing.mask = [&](size_t group, int32_t) { masked[group] = 1; };
        pricing.restore = [&] { std::fill(masked.begin(), masked.end(), uint8_t(0)); };
        pricing.sessionStopped = [&] { budget.check(true); return false; };
        pricing.generate = [&](std::span<const double> prices, uint32_t maximumTrials) {
            const auto chosen = recovered::multiSelectPricing(algorithms, selection);
            if (!chosen || inventory[*chosen] == 0) return;
            const auto started = std::chrono::steady_clock::now();
            const auto secondsLeft = std::chrono::duration<double>(budget.deadline - started).count();
            // 4bbe60 forwards both the trial limit and limit * .0002 seconds
            // to its geometric provider. This adapter uses the same time cap.
            Budget pricingBudget(std::min(secondsLeft, double(maximumTrials) * .0002));
            Input input;
            const auto &sheet = lookup(s.sheets, stocks[*chosen]);
            input.width = sheet.width; input.height = sheet.height;
            input.pitch = s.pitch; input.gap = s.gap;
            input.fastConstruction = s.fastConstruction; input.randomSeed = random.next();
            std::vector<size_t> activeGroups;
            std::vector<PN2_Part> activeInstances;
            OrdinaryControl control;
            for (size_t group = 0; group < types.size(); ++group) {
                if (masked[group]) continue;
                const auto &part = lookup(s.parts, types[group]);
                const auto &shape = lookup(s.shapes, part.shape);
                input.parts.push_back({shape.polygon, shape.pivot, part.poses, int(queues[group].size()), shape.holes, shape.protection});
                activeGroups.push_back(group); control.values.push_back(prices[group]);
                for (const auto instance : queues[group]) activeInstances.push_back(handle<PN2_Part>(instance));
            }
            recovered::MultiPricingTrial trial;
            try {
                Result column;
                column.sheet = stocks[*chosen];
                if (!input.parts.empty() && input.gap == 0 && !requires_general(input)) {
                    OrdinaryState state;
                    ordinaryRun(input, pricingBudget, state, &control);
                    for (const auto &placement : state.output.placements) {
                        const auto group = activeGroups.at(placement.group);
                        const auto &part = input.parts.at(placement.group);
                        const auto pose = part.poses.at(placement.poseIndex);
                        const Point position{double(placement.x) / 1e6, double(placement.y) / 1000};
                        column.placed.push_back({queues[group].at(placement.ordinal), position,
                            {pose.flip ? 1 : 0, pose.angle, pose.angle}});
                        for (const auto point : transform(part.polygon, part.pivot, pose))
                            column.length = std::max(column.length, point.x + position.x);
                    }
                } else if (!activeInstances.empty()) {
                    // The engineering material/continuous-angle producer is
                    // retained for these inputs; its factory is not claimed
                    // to reproduce the supplier's pricing technology.
                    std::stable_sort(activeInstances.begin(), activeInstances.end(), [&](auto first, auto second) {
                        return prices[groupOf.at(id(first))] > prices[groupOf.at(id(second))];
                    });
                    column = solve(s, stocks[*chosen], activeInstances.data(), int(activeInstances.size()), pricingBudget);
                }
                trial.layout = true;
                trial.nonempty = !column.placed.empty();
                trial.accepted = addColumn(std::move(column));
            } catch (const Failure &failure) {
                if (failure.code != PN2S_TIMEOUT) throw;
                budget.check(true);
            }
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            recovered::multiRecordPricing(algorithms[*chosen], trial, elapsed);
        };
        const auto generated = recovered::multiGenerateColumns(pricing);
        if (generated.attempts) solution = solveMaster();
    }
    budget.check(true);
    Plan output;
    std::vector<int> available = inventory;
    std::vector<size_t> used(types.size());
    recovered::MultiRoundingHost rounding;
    rounding.stopped = [&] { budget.check(true); return false; };
    const auto applyPattern = [&](size_t index) -> std::optional<double> {
        budget.check(true);
        const auto &pattern = patterns.at(index);
        if (available[pattern.sheet] == 0) return {};
        const auto &prototype = candidateLayouts.at(index);
        Result board;
        board.sheet = prototype.sheet;
        auto reboundUsed=used;
        std::map<Id,Id> reboundIdentities;
        double requiredArea = 0, reboundArea = 0;
        for (const auto &placement : prototype.placed) {
            const auto group = groupOf.at(placement.part);
            const auto value = partArea(s, placement.part);
            requiredArea += value;
            if (reboundUsed[group] == queues[group].size()) continue;
            auto rebound = placement;
            rebound.part = queues[group][reboundUsed[group]++];
            reboundIdentities.emplace(placement.part,rebound.part);
            board.placed.push_back(rebound);
            reboundArea += value;
            const auto &part = lookup(s.parts, rebound.part);
            const auto &shape = lookup(s.shapes, part.shape);
            const Pose pose{rebound.orientation.min_angle, bool(rebound.orientation.horizontal_flip)};
            for (const auto point : transform(shape.polygon, shape.pivot, pose))
                board.length = std::max(board.length, point.x + rebound.position.x);
        }
        // A template may exhaust a part queue while retaining another sister.
        // Reject that truncated pattern before consuming inventory or demand.
        for(const auto &sisters:prototype.multiheadsSisters){
            size_t retained=0;for(const auto member:sisters)retained+=reboundIdentities.contains(member);
            if(retained&&retained!=sisters.size())return {};
        }
        apiMultiheadsRebindSisters(prototype,board,reboundIdentities);
        used=std::move(reboundUsed);
        --available[pattern.sheet];
        output.stockArea += stock[pattern.sheet].cost;
        output.boards.push_back(std::move(board));
        // 4f43b0 divides actual required area by template required area.
        // The public part interface has no optional-part designation.
        return reboundArea / requiredArea;
    };
    rounding.addPattern = [&](size_t index) { return applyPattern(index).has_value(); };
    if (solution.status == 0) {
        recovered::multiRoundMasterUses(solution.usage, rounding);
        const auto perturbed = recovered::multiPerturbMasterUses(solution.usage, [&] { return random.unit(); });
        const auto candidates = recovered::multiReusePatternCandidates(perturbed);
        recovered::MultiPatternReuseHost reuse;
        reuse.unit = [&] { return random.unit(); };
        reuse.apply = applyPattern;
        reuse.removeLast = [&] {
            const auto &board = output.boards.back();
            const auto sheet = size_t(std::find(stocks.begin(), stocks.end(), board.sheet) - stocks.begin());
            ++available[sheet];
            for (const auto &placement : board.placed) --used[groupOf.at(placement.part)];
            output.stockArea -= stock[sheet].cost;
            output.boards.pop_back();
        };
        recovered::multiApplyReuseCandidates(candidates, reuse);
    }
    std::vector<recovered::MultiLnsSheet> modificationSheets;
    recovered::MultiLnsHost modification;
    modification.sheets = [&]() -> std::span<const recovered::MultiLnsSheet> { return modificationSheets; };
    modification.randomIndex = [&](size_t count) { return size_t(recovered::multiRandomIndex(random, 0, int32_t(count) - 1)); };
    modification.remove = [&](size_t index) {
        budget.check(true);
        const auto &board = output.boards.at(index);
        const auto sheet = size_t(std::find(stocks.begin(), stocks.end(), board.sheet) - stocks.begin());
        ++available[sheet];
        output.stockArea -= stock[sheet].cost;
        output.boards.erase(output.boards.begin() + ptrdiff_t(index));
        modificationSheets.erase(modificationSheets.begin() + ptrdiff_t(index));
    };
    recovered::MultiColumnCompletionHost completion;
    completion.stopped = [&] { budget.check(true); return false; };
    completion.unit = [&] { return random.unit(); };
    completion.integer = [&](int32_t first, int32_t last) { return recovered::multiRandomIndex(random, first, last); };
    completion.modify = [&](bool includeLast, int32_t lowest, int32_t randomCount) {
        modificationSheets.clear();
        for (const auto &board : output.boards) {
            recovered::MultiLnsSheet entry;
            const auto &sheet = lookup(s.sheets, board.sheet);
            entry.costComponents[0] = sheet.width * sheet.height; entry.costWeights[0] = 1;
            for (const auto &placement : board.placed) {
                const auto value = partArea(s, placement.part);
                entry.requiredAreas.push_back(value); entry.informerRequiredArea += value;
            }
            modificationSheets.push_back(std::move(entry));
        }
        recovered::multiModifyLayout(modification, includeLast, lowest, randomCount);
    };
    completion.complete = [&] {
        // Destruction can remove an earlier board. Remaining instances are
        // therefore identified from the actual boards, not queue prefixes.
        std::set<Id> consumed;
        for (const auto &board : output.boards)
            for (const auto &placement : board.placed) consumed.insert(placement.part);
        output.left.clear();
        for (const auto instance : instances)
            if (!consumed.contains(instance)) output.left.push_back(instance);
        if (output.left.empty()) return int32_t(0);
        auto remainder = plan(s, stocks, available, output.left, 0, budget);
        const auto added = int32_t(remainder.boards.size());
        output.left = std::move(remainder.left);
        output.stockArea += remainder.stockArea;
        for (auto &board : remainder.boards) {
            const auto sheet = size_t(std::find(stocks.begin(), stocks.end(), board.sheet) - stocks.begin());
            --available[sheet];
            addColumn(board);
            output.boards.push_back(std::move(board));
        }
        return added;
    };
    recovered::multiCompleteColumns(completion);
    (void)solveMaster();
    return output;
}
Plan lnsPlan(Session &s, const std::vector<Id> &stocks, std::vector<int> available,
             const std::vector<Id> &instances, const Plan &source, recovered::Random &random, Budget &budget) {
    Plan output = source;
    std::vector<recovered::MultiLnsSheet> sheets;
    for (size_t index = 0; index < output.boards.size(); ++index) {
        const auto &board = output.boards[index];
        const auto found = std::find(stocks.begin(), stocks.end(), board.sheet);
        require(found != stocks.end(), PN2S_INTERNAL, "LNS refers to unavailable sheet type");
        --available[size_t(found - stocks.begin())];
        const auto &sheet = lookup(s.sheets, board.sheet);
        recovered::MultiLnsSheet entry;
        entry.identity = index;
        entry.costComponents[0] = sheet.width * sheet.height;
        entry.costWeights[0] = 1;
        for (const auto &placement : board.placed) {
            const auto value = partArea(s, placement.part);
            entry.requiredAreas.push_back(value);
            entry.informerRequiredArea += value;
        }
        sheets.push_back(std::move(entry));
    }
    recovered::MultiLnsHost host;
    host.sheets = [&]() -> std::span<const recovered::MultiLnsSheet> { return sheets; };
    host.unit = [&] { return random.unit(); };
    host.randomIndex = [&](size_t count) { return size_t(recovered::multiRandomIndex(random, 0, int32_t(count) - 1)); };
    host.remove = [&](size_t index) {
        budget.check(true);
        const auto &board = output.boards.at(index);
        const auto stock = size_t(std::find(stocks.begin(), stocks.end(), board.sheet) - stocks.begin());
        ++available[stock];
        output.stockArea -= sheets[index].costComponents[0];
        output.boards.erase(output.boards.begin() + index);
        sheets.erase(sheets.begin() + index);
    };
    recovered::MultiLnsRunHost run;
    run.destroy = host;
    // 54d37b initializes the ordinary problem's +1950 preservation flag to zero.
    run.preserveInitialLastSheet = false;
    run.hasExistingLayout = [&] { return !source.boards.empty(); };
    run.copyExistingLayout = [] {}; // source boards and metadata were copied above.
    run.partial = [&](double randomWeight, double ratioWeight) {
        recovered::MultiLnsPartialHost partial{host};
        // The polygon ABI supplies no cutting technology. 4baf70 returns at
        // this gate, while its enclosing 4ba8c0 still consumes the .5 draw.
        partial.hasTechnology = false;
        recovered::multiLnsPartial(partial, randomWeight, ratioWeight);
    };
    run.oneByOne = [&] {
        std::set<Id> retained;
        for (const auto &board : output.boards)
            for (const auto &placement : board.placed) retained.insert(placement.part);
        output.left.clear();
        for (const auto instance : instances)
            if (!retained.contains(instance)) output.left.push_back(instance);
        auto remainder = plan(s, stocks, available, output.left, 0, budget);
        output.left = std::move(remainder.left);
        output.stockArea += remainder.stockArea;
        for (auto &board : remainder.boards) output.boards.push_back(std::move(board));
    };
    recovered::multiLnsRun(run);
    return output;
}
int objectError(void *p) {
    auto v = reinterpret_cast<intptr_t>(p);
    if (v == 0)
        return PN2S_INVALID;
    if (v < 0 && v >= -65535)
        return static_cast<int>(v);
    return 0;
}
} // namespace
extern "C" {
const char *PN2S_CALL PN2S_Version() {
    return "iPlaneNesting 0.4.0; recovered circular geometry, SDK contours, Offset/Oreol factory, "
           "multi-sheet master LP, LNS, pricing and pattern reuse; incomplete vendor engine equivalence";
}
const char *PN2S_CALL PN2S_LastErrorDetail() {
    return detail.c_str();
}
PN2_Session PN2S_CALL PN2_CreateSession() {
    return invoke<PN2_Session>([] {
        Id t = token();
        sessions.emplace(t, Session{});
        return handle<PN2_Session>(t);
    });
}
PN2_Bool PN2S_CALL PN2_DeleteSession(PN2_Session s) {
    return invoke<PN2_Bool>([&] {
        session(s);
        sessions.erase(id(s));
        return 1;
    });
}
PN2_Orientation PN2S_CALL PN2_CreateOrientation(double a) {
    return {0, a, a};
}
PN2_Orientation PN2S_CALL PN2_CreateXFlippedOrientation(double a) {
    return {1, a, a};
}
PN2_Orientation PN2S_CALL PN2_CreateRangeOrientation(double a, double t) {
    return {0, a - t, a + t};
}
PN2_Orientation PN2S_CALL PN2_CreateFreeOrientation(PN2_Bool flip) {
    return {flip, 0, 360};
}
PN2_Contour PN2S_CALL PN2_CreateContourFromPoints(PN2_Session h, const PN2_Point *points, int count) {
    return invoke<PN2_Contour>([&] { return createContour(session(h), points, count, recovered::SdkContourKind::Points); });
}
PN2_Contour PN2S_CALL PN2_CreateContourFromArcsBulges(PN2_Session h, const PN2_Point *points,
    const double *values, int count) {
    return invoke<PN2_Contour>([&] { return createContour(session(h), points, count, recovered::SdkContourKind::Bulges, values); });
}
PN2_Contour PN2S_CALL PN2_CreateContourFromArcsSagittas(PN2_Session h, const PN2_Point *points,
    const double *values, int count) {
    return invoke<PN2_Contour>([&] { return createContour(session(h), points, count, recovered::SdkContourKind::Sagittas, values); });
}
PN2_Contour PN2S_CALL PN2_CreateContourFromArcsCenters(PN2_Session h, const PN2_Point *points,
    const PN2_Point *centers, const PN2_Bool *directions, int count) {
    return invoke<PN2_Contour>([&] {
        require((centers && directions) || (!centers && !directions), PN2S_INVALID,
                "Center and direction arrays must be supplied together");
        return createContour(session(h), points, count, recovered::SdkContourKind::Centers,
                              nullptr, centers, directions);
    });
}
PN2_Shape PN2S_CALL PN2_AddShapeFromContour(PN2_Session h, PN2_Contour contour, PN2_Point pivot) {
    return invoke<PN2_Shape>([&] {
        auto &s = session(h);
        validateContourPoint(pivot);
        auto polygon = contourPolygon(lookup(s.contours, id(contour)), true);
        require(s.shapes.size() < 4096, PN2S_RESOURCE_LIMIT, "Shape limit");
        const auto key = token();
        s.shapes.emplace(key, Shape{std::move(polygon), pivot});
        return handle<PN2_Shape>(key);
    });
}
PN2_ErrorCode PN2S_CALL PN2_ShapeAddHoleFromContour(PN2_Session h, PN2_Shape sh, PN2_Contour contour) {
    return invoke<int>([&] {
        auto &s = session(h);
        auto &shape = lookup(s.shapes, id(sh));
        auto polygon = contourPolygon(lookup(s.contours, id(contour)), false);
        require(shape.holes.size() < 512, PN2S_RESOURCE_LIMIT, "Maximum 512 holes per shape");
        validate_hole(shape.polygon, shape.holes, polygon);
        shape.holes.push_back(std::move(polygon));
        return PN2S_OK;
    });
}
PN2_Shape PN2S_CALL PN2_AddShape(PN2_Session h, const PN2_Point *points, int n, PN2_Point pivot) {
    return invoke<PN2_Shape>([&] {
        auto &s = session(h);
        require(points && n >= 3 && n <= 8192, PN2S_INVALID, "Need 3..8192 points");
        require(std::isfinite(pivot.x) && std::isfinite(pivot.y) && std::abs(pivot.x) <= 1e6 &&
                    std::abs(pivot.y) <= 1e6,
                PN2S_INVALID, "Invalid pivot");
        Polygon p(points, points + n);
        validate_polygon(p);
        require(s.shapes.size() < 4096, PN2S_RESOURCE_LIMIT, "Shape limit");
        Id t = token();
        s.shapes.emplace(t, Shape{std::move(p), pivot});
        return handle<PN2_Shape>(t);
    });
}
PN2_Part PN2S_CALL PN2_AddPart(PN2_Session h, PN2_Shape sh, const PN2_Orientation *poses, int n) {
    return invoke<PN2_Part>([&] {
        auto &s = session(h);
        lookup(s.shapes, id(sh));
        auto p = readPoses(poses, n);
        require(s.parts.size() < 10000, PN2S_RESOURCE_LIMIT, "Part limit");
        Id t = token();
        s.parts.emplace(t, Part{id(sh), std::move(p)});
        return handle<PN2_Part>(t);
    });
}
PN2_ErrorCode PN2S_CALL PN2_AddSeveralParts(PN2_Session h, PN2_Shape sh, const PN2_Orientation *poses,
                                            int n, int qty, PN2_Part *output) {
    return invoke<int>([&] {
        auto &s = session(h);
        lookup(s.shapes, id(sh));
        auto p = readPoses(poses, n);
        require(output && qty > 0 && qty <= 10000 && s.parts.size() + static_cast<size_t>(qty) <= 10000,
                PN2S_INVALID, "Invalid quantity or output array");
        std::vector<Id> added;
        added.reserve(static_cast<size_t>(qty));
        try {
            for (int i = 0; i < qty; i++) {
                Id t = token();
                s.parts.emplace(t, Part{id(sh), p});
                added.push_back(t);
            }
        } catch (...) {
            for (Id t : added)
                s.parts.erase(t);
            throw;
        }
        for (int i = 0; i < qty; i++)
            output[i] = handle<PN2_Part>(added[i]);
        return 0;
    });
}
PN2_Sheet PN2S_CALL PN2_AddSheet(PN2_Session h, double height, double length) {
    return invoke<PN2_Sheet>([&] {
        auto &s = session(h);
        require(std::isfinite(height) && std::isfinite(length) && height > 0 && length > 0, PN2S_INVALID,
                "Positive finite sheet dimensions required");
        require(length <= 1e6 && height <= 1e6, PN2S_UNSUPPORTED,
                "Sheet dimensions exceed the finite host coordinate domain");
        Id t = token();
        s.sheets.emplace(t, Sheet{length, height});
        return handle<PN2_Sheet>(t);
    });
}
PN2_ErrorCode PN2S_CALL PN2S_SetStrategy(PN2_Session h, int strategy) {
    return invoke<int>([&] {
        auto &s = session(h);
        require(strategy >= 0 && strategy <= PN2S_DEFAULT_RECOVERED, PN2S_UNSUPPORTED, "Unknown strategy");
        s.strategy = strategy;
        return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2S_SetRandomSeed(PN2_Session h, unsigned int seed) {
    return invoke<int>([&] { session(h).randomSeed = seed; return 0; });
}
PN2_ErrorCode PN2S_CALL PN2S_SetPitch(PN2_Session h, double pitch) {
    return invoke<int>([&] {
        auto &s = session(h);
        require(std::isfinite(pitch) && pitch >= 0 && pitch <= 1e6 &&
                    (pitch == 0 || rounded(pitch * 1e6) > 0),
                PN2S_INVALID, "Invalid strip pitch");
        s.pitch = pitch;
        return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2S_SetRectangleGap(PN2_Session h, double gap) {
    return invoke<int>([&] {
        auto &s = session(h);
        require(std::isfinite(gap) && gap >= 0 && gap <= 1e6, PN2S_INVALID, "Invalid gap");
        require(grid_exact(gap, 1e6) && grid_exact(gap, 1000), PN2S_UNSUPPORTED,
                "Gap does not fit host grid");
        s.gap = gap;
        s.materialGap = false;
        return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2_ActivateLOffcut(PN2_Session h,PN2_Bool activate) {
    return invoke<int>([&]{session(h).lOffcut=activate!=0;return 0;});
}
PN2_ErrorCode PN2S_CALL PN2_SetLOffcutMinimumUsableHeight(PN2_Session h,double height) {
    return invoke<int>([&]{session(h).minimumLOffcutHeight=height;return 0;});
}
PN2_Result PN2S_CALL PN2_Nest(PN2_Session h, PN2_Sheet sh, const PN2_Part *parts, int n,
                              double seconds) {
    return invoke<PN2_Result>(
        [&] { return handle<PN2_Result>(nest(session(h), id(sh), parts, n, seconds)); });
}
PN2_Result PN2S_CALL PN2_PreNest(PN2_Session h,PN2_Sheet sh,const PN2_Part *parts,
    const PN2_Orientation *poses,const PN2_Point *positions,int n){
    return invoke<PN2_Result>([&]{
        auto &s=session(h);lookup(s.sheets,id(sh));
        require(recovered::preNestCountValid(n)&&n<=10000&&parts&&poses&&positions,PN2S_INVALID,
                "PreNest needs 1..10000 parts, orientations and positions");
        PredefinedRequest request;request.manual=true;
        std::vector<PN2_Part> concrete;std::set<Id> seen;
        for(int index=0;index<n;++index){
            const auto part=id(parts[index]);lookup(s.parts,part);
            require(std::isfinite(poses[index].min_angle)&&std::isfinite(poses[index].max_angle)&&
                    recovered::predefinedOrientationValid({poses[index].horizontal_flip,poses[index].min_angle,poses[index].max_angle}),
                    PN2S_INVALID,"Invalid predefined orientation");
            validateContourPoint(positions[index]);
            request.records.push_back({part,positions[index],poses[index]});
            require(seen.insert(part).second,PN2S_UNSUPPORTED,
                    "Repeated PreNest SDK identities need the original occurrence-to-instance mapping");
            concrete.push_back(parts[index]);
        }
        Budget budget(3600.);
        auto result=solve(s,id(sh),concrete.data(),int(concrete.size()),budget,nullptr,nullptr,std::nullopt,
                          nullptr,nullptr,nullptr,nullptr,&request);
        const auto key=token();s.results.emplace(key,std::move(result));return handle<PN2_Result>(key);
    });
}
PN2_Result PN2S_CALL PN2_NestMore(PN2_Session h,PN2_Result old,const PN2_Part *parts,int n,double seconds){
    return invoke<PN2_Result>([&]{
        auto &s=session(h);const auto source=lookup(s.results,id(old));
        const auto action=recovered::nestMoreCountAction(n);
        require(action!=recovered::PredefinedCountAction::Invalid&&n<=10000,PN2S_INVALID,"Invalid NestMore part count");
        if(action==recovered::PredefinedCountAction::ReturnExisting)return old;
        require(source.multiheadsSisters.empty(),PN2S_UNSUPPORTED,
                "NestMore of cutting-head sisters requires retaining the original predefined process owners");
        require(parts&&std::isfinite(seconds)&&seconds>0&&seconds<=3600,PN2S_INVALID,"NestMore needs parts and a time budget in (0,3600]");
        require(source.placed.size()+size_t(n)<=10000,PN2S_RESOURCE_LIMIT,"NestMore concrete instance limit");
        PredefinedRequest request;request.records=source.placed;
        std::vector<PN2_Part> concrete;std::set<Id> seen;
        for(auto &record:request.records){
            concrete.push_back(handle<PN2_Part>(record.part));seen.insert(record.part);
            double angle=std::fmod(record.orientation.min_angle,360.);if(angle<0)angle+=360.;
            record.orientation.min_angle=record.orientation.max_angle=angle;
        }
        for(int index=0;index<n;++index){lookup(s.parts,id(parts[index]));
            require(seen.insert(id(parts[index])).second,PN2S_UNSUPPORTED,"Repeated old/new NestMore identities need the original duplicate-instance mapping");
            concrete.push_back(parts[index]);}
        Budget budget(seconds);
        auto result=solve(s,source.sheet,concrete.data(),int(concrete.size()),budget,nullptr,nullptr,std::nullopt,
                          nullptr,nullptr,nullptr,nullptr,&request);
        const auto key=token();s.results.emplace(key,std::move(result));return handle<PN2_Result>(key);
    });
}
PN2_ErrorCode PN2S_CALL PN2_SetPreNestedGlueMode(PN2_Session h,PN2_Part part,PN2_PRE_NESTED_GLUE_MODE mode){
    return invoke<int>([&]{lookup(session(h).parts,id(part)).glueMode=uint32_t(mode);return 0;});
}
PN2_Result PN2S_CALL PN2_PricedNest(PN2_Session h,PN2_Sheet sh,const PN2_Part *parts,int *prices,int n,double seconds){
    return invoke<PN2_Result>([&]{return handle<PN2_Result>(nestPriced(session(h),id(sh),parts,prices,n,seconds));});
}
PN2_Result PN2S_CALL PN2S_NestDirection10(PN2_Session h,PN2_Sheet sh,const PN2_Part *parts,int n,
    const double *values,const PN2S_Direction10Options *options,double seconds) {
    return invoke<PN2_Result>([&] {
        auto &s=session(h);
        require(options && options->struct_size==sizeof(PN2S_Direction10Options),PN2S_INVALID,"Direction10 options size mismatch");
        require(options->candidate_count_10<=10000 && std::isfinite(options->alpha_50) && std::isfinite(options->gamma_58) &&
            std::isfinite(options->perimeter_probability_1d8) && options->perimeter_probability_1d8>=0 && options->perimeter_probability_1d8<=1 &&
            std::isfinite(options->perimeter_scale_1e0) && options->perimeter_scale_1e0>=0,PN2S_INVALID,"Invalid Direction10 weight fields");
        const PN2S_Direction14Options common{sizeof(PN2S_Direction14Options),options->batch_size,options->reference_kind,options->length_mode,
            options->direction_scale_218,options->direction_scale_220,options->target_14b8,options->queue_20,options->maximum_length};
        const BtProgram::Direction10 config{options->direction_scale_218,options->direction_scale_220,options->batch_size,options->candidate_count_10,
            options->alpha_50,options->gamma_58,options->perimeter_probability_1d8,options->perimeter_scale_1e0};
        return handle<PN2_Result>(nestDirection(s,id(sh),parts,n,values,common,seconds,std::nullopt,&config));
    });
}
PN2_Result PN2S_CALL PN2S_NestDirection14(PN2_Session h,PN2_Sheet sh,const PN2_Part *parts,int n,
    const double *values,const PN2S_Direction14Options *options,double seconds) {
    return invoke<PN2_Result>([&] {
        auto &s=session(h);
        require(options && options->struct_size==sizeof(PN2S_Direction14Options),PN2S_INVALID,"Direction14 options size mismatch");
        return handle<PN2_Result>(nestDirection(s,id(sh),parts,n,values,*options,seconds));
    });
}
PN2_Result PN2S_CALL PN2S_NestDirection18(PN2_Session h,PN2_Sheet sh,const PN2_Part *parts,int n,
    const double *values,const PN2S_Direction18Options *options,double seconds) {
    return invoke<PN2_Result>([&] {
        auto &s=session(h);
        require(options && options->struct_size==sizeof(PN2S_Direction18Options),PN2S_INVALID,"Direction18 options size mismatch");
        const PN2S_Direction14Options common{sizeof(PN2S_Direction14Options),options->batch_size,options->reference_kind,options->length_mode,
            options->direction_scale_218,options->direction_scale_220,options->target_14b8,options->queue_20,options->maximum_length};
        return handle<PN2_Result>(nestDirection(s,id(sh),parts,n,values,common,seconds,options->candidate_limit_18));
    });
}
PN2_Result PN2S_CALL PN2S_NestBt(PN2_Session h,PN2_Sheet sh,const PN2_Part *parts,int n,
    const double *values,const PN2S_BtOptions *o,double seconds) {
    return invoke<PN2_Result>([&] {
        auto &s=session(h);
        require(o && o->struct_size==sizeof(PN2S_BtOptions),PN2S_INVALID,"Bt options size mismatch");
        require(o->count_10<=10000 && o->count_1c<=10000 && o->limit_18<=10000 &&
            std::isfinite(o->alpha_50) && std::isfinite(o->gamma_58) &&
            std::isfinite(o->perimeter_probability_1d8) && o->perimeter_probability_1d8>=0 && o->perimeter_probability_1d8<=1 &&
            std::isfinite(o->perimeter_scale_1e0) && o->perimeter_scale_1e0>=0 &&
            std::isfinite(o->fixed_front) && o->fixed_front>=0 && o->fixed_front<=1e6,PN2S_INVALID,"Invalid Bt fields");
        for(int flag:{o->force_process_inspection,o->replay_previous,o->filter_replay_front})
            require(flag==0 || flag==1,PN2S_INVALID,"Bt switches must be zero or one");
        const PN2S_Direction14Options common{sizeof(PN2S_Direction14Options),o->batch_size,o->reference_kind,o->length_mode,
            o->direction_scale_218,o->direction_scale_220,o->target_14b8,o->queue_20,o->maximum_length};
        BtProgram p;p.combineGenerators=true;p.replayPrediction=o->replay_previous!=0;p.filterReplayAtFront=o->filter_replay_front!=0;
        if(o->flag_14>0)p.direction14=BtProgram::Direction14{o->direction_scale_218,o->direction_scale_220,o->batch_size};
        if(o->limit_18>0)p.direction18=BtProgram::Direction18{o->direction_scale_218,o->direction_scale_220,o->batch_size,o->limit_18};
        p.extremal1c=BtProgram::Extremal1c{o->batch_size,o->count_1c,rounded(o->fixed_front*1e6),o->force_process_inspection!=0};
        if(o->count_10>0)p.direction10=BtProgram::Direction10{o->direction_scale_218,o->direction_scale_220,o->batch_size,o->count_10,
            o->alpha_50,o->gamma_58,o->perimeter_probability_1d8,o->perimeter_scale_1e0};
        return handle<PN2_Result>(nestDirection(s,id(sh),parts,n,values,common,seconds,std::nullopt,nullptr,&p));
    });
}
PN2_Result PN2S_CALL PN2S_NestMultiStrategy(PN2_Session h,PN2_Sheet sh,const PN2_Part *parts,int n,
    const PN2S_MultiStrategyOptions *o,double seconds) {
    return invoke<PN2_Result>([&] {
        auto &s=session(h);
        require(o && o->struct_size==sizeof(PN2S_MultiStrategyOptions),PN2S_INVALID,"MultiStrategy options size mismatch");
        require(o->context_batch_238>0 && o->context_batch_238<=3333 && o->current_reference_kind>=0 && o->current_reference_kind<=2,PN2S_INVALID,"Invalid MultiStrategy batch or reference");
        for(int flag:{o->length_mode,o->force_process_inspection,o->replay_previous,o->filter_replay_front})
            require(flag==0 || flag==1,PN2S_INVALID,"MultiStrategy switches must be zero or one");
        require(std::isfinite(o->direction_scale_218) && std::isfinite(o->direction_scale_220) &&
            std::isfinite(o->target_14b8) && std::isfinite(o->queue_20) &&
            std::isfinite(o->maximum_length) && o->maximum_length>=0 && o->maximum_length<=1e6 &&
            std::isfinite(o->fixed_front) && o->fixed_front>=0 && o->fixed_front<=1e6,PN2S_INVALID,"Invalid MultiStrategy upstream fields");
        require(std::isfinite(o->perimeter_probability_1d8) && o->perimeter_probability_1d8>=0 && o->perimeter_probability_1d8<=1 &&
            std::isfinite(o->perimeter_scale_1e0) && o->perimeter_scale_1e0>=0 &&
            std::isfinite(o->alpha_limit_260) && std::isfinite(o->gamma_limit_268),PN2S_INVALID,"Invalid MultiStrategy complexity configuration");
        require(std::isfinite(seconds) && seconds>0 && seconds<=3600,PN2S_INVALID,"Time budget must be in (0,3600] seconds");
        MultiProgram p;p.contextBatch238=o->context_batch_238;p.contextField218=o->direction_scale_218;p.contextField220=o->direction_scale_220;
        p.currentReference=static_cast<recovered::ReferenceKind>(o->current_reference_kind);p.lengthMode=o->length_mode!=0;
        p.targetField14b8=o->target_14b8;p.queueAggregate20=o->queue_20;p.maximumLengthX=rounded(o->maximum_length*1e6);
        p.config={o->perimeter_probability_1d8,o->perimeter_scale_1e0,o->alpha_limit_260,o->gamma_limit_268};
        p.fixedFront=rounded(o->fixed_front*1e6);p.forceProcessInspection=o->force_process_inspection!=0;
        p.replayPrediction=o->replay_previous!=0;p.filterReplayAtFront=o->filter_replay_front!=0;
        Budget budget(seconds);auto result=solve(s,id(sh),parts,n,budget,nullptr,nullptr,std::nullopt,nullptr,nullptr,&p);
        const Id resultId=token();s.results.emplace(resultId,std::move(result));return handle<PN2_Result>(resultId);
    });
}
PN2S_StrategySchedule PN2S_CALL PN2S_CreateStrategySchedule(PN2_Session h,const PN2S_StrategyEntry *entries,int count,const PN2S_ScheduleOptions *o) {
    return invoke<PN2S_StrategySchedule>([&] {
        auto &s=session(h);
        require(o && o->struct_size==sizeof(PN2S_ScheduleOptions) && entries && count>0 && count<=128,PN2S_INVALID,"Invalid schedule options or entries");
        require(o->worker_count>0 && o->worker_count<=256 && o->initial_trials>=0 && o->max_trials>=-1 && o->initial_pool_limit>=0,
            PN2S_INVALID,"Invalid schedule counters");
        require(std::isfinite(o->time_1c0) && o->time_1c0>=0 && std::isfinite(o->total_weight) && o->total_weight>0 &&
            std::isfinite(o->pool_growth_1568) && o->pool_growth_1568>=0,PN2S_INVALID,"Invalid schedule budget/growth fields");
        Schedule schedule(o->seed);
        for(int k=0;k<count;++k) {
            const auto &e=entries[k];
            require(e.strategy_id>0 && e.calibration>0 && std::isfinite(e.weight) && e.weight>=0 &&
                std::isfinite(e.accumulated_budget) && e.accumulated_budget>=0,PN2S_INVALID,"Invalid strategy entry");
            schedule.entries.push_back({e.strategy_id,e.calibration,e.weight,e.accumulated_budget});
        }
        std::sort(schedule.entries.begin(),schedule.entries.end(),[](const auto &a,const auto &b){return a.id<b.id;});
        for(size_t k=1;k<schedule.entries.size();++k)require(schedule.entries[k-1].id!=schedule.entries[k].id,PN2S_INVALID,"Duplicate strategy ID");
        schedule.weights=recovered::strategyWeights(schedule.entries);
        recovered::aliasTable(schedule.weights); // Validate before publishing the handle or consuming RNG.
        recovered::addStrategyBudgets(schedule.entries,o->worker_count,o->time_1c0,o->total_weight);
        for(const auto &e:schedule.entries)require(std::isfinite(e.budget),PN2S_INVALID,"Strategy budget overflow");
        schedule.state={o->initial_trials,o->max_trials,o->initial_pool_limit,o->pool_growth_1568};
        const Id result=token();s.schedules.emplace(result,std::move(schedule));return handle<PN2S_StrategySchedule>(result);
    });
}
int PN2S_CALL PN2S_NextScheduledStrategy(PN2_Session h,PN2S_StrategySchedule schedule,int *strategy,int *trials,int *poolLimit) {
    return invoke<int>([&] {
        auto &s=lookup(session(h).schedules,id(schedule));
        require(strategy && trials && poolLimit,PN2S_INVALID,"Null schedule output");
        auto selected=recovered::nextStrategy(s.rng,s.state,s.weights);
        *strategy=selected?s.entries[*selected].id:0;*trials=s.state.trials;*poolLimit=s.state.poolLimit;return selected?1:0;
    });
}
PN2_ErrorCode PN2S_CALL PN2S_GetScheduleEntry(PN2_Session h,PN2S_StrategySchedule schedule,int index,PN2S_StrategyEntry *out,double *weight) {
    return invoke<int>([&] {
        const auto &s=lookup(session(h).schedules,id(schedule));
        require(out && weight && index>=0 && static_cast<size_t>(index)<s.entries.size(),PN2S_INVALID,"Invalid schedule entry output");
        const auto &e=s.entries[index];*out={e.id,e.calibration,e.weight,e.budget};*weight=s.weights[index];return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2S_DeleteStrategySchedule(PN2_Session h,PN2S_StrategySchedule schedule) {
    return invoke<int>([&]{auto &s=session(h);require(s.schedules.erase(id(schedule))==1,PN2S_INVALID,"Invalid schedule handle");return 0;});
}
PN2_ErrorCode PN2S_CALL PN2S_SelectStrategyWeights(const PN2S_StrategyResult *pool,int count,int trials,double a,double b,
    PN2S_StrategyWeight *output,int capacity,int *weightCount,int *prefix,int *selected) {
    return invoke<int>([&] {
        require(pool && count>0 && count<=10000 && trials>=0 && weightCount && prefix && selected && capacity>=0 &&
            (output || capacity==0),PN2S_INVALID,"Invalid result-weight arguments");
        require(std::isfinite(a) && std::isfinite(b),PN2S_INVALID,"Nonfinite result-weight configuration");
        std::vector<recovered::StrategyResult> input;input.reserve(count);
        for(int k=0;k<count;++k){require(std::isfinite(pool[k].metric),PN2S_INVALID,"Nonfinite candidate metric");input.push_back({pool[k].metric,pool[k].source_id});}
        const auto result=recovered::selectResultWeights(input,trials,a,b);
        require(!output || static_cast<size_t>(capacity)>=result.weights.size(),PN2S_RESOURCE_LIMIT,"Result-weight output buffer too small");
        if(output){int k=0;for(auto [id,value]:result.weights)output[k++]={id,value};}
        *weightCount=static_cast<int>(result.weights.size());*prefix=result.equalPrefix;*selected=result.selectedCount;return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2_GetResultInfos(PN2_Session h, PN2_Result r, double *length, int *count) {
    return invoke<int>([&] {
        auto &result = lookup(session(h).results, id(r));
        require(length && count, PN2S_INVALID, "Null result output");
        *length = result.length;
        *count = static_cast<int>(result.placed.size());
        return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2_GetKthNestedPart(PN2_Session h, PN2_Result r, int index, PN2_Part *part,
                                             PN2_Point *pos, PN2_Orientation *pose) {
    return invoke<int>([&] {
        auto &result = lookup(session(h).results, id(r));
        require(index >= 0 && static_cast<size_t>(index) < result.placed.size() && part && pos && pose,
                PN2S_INVALID, "Invalid index or output pointer");
        auto p = result.placed[static_cast<size_t>(index)];
        *part = handle<PN2_Part>(p.part);
        *pos = p.position;
        *pose = p.orientation;
        return 0;
    });
}
PN2_Bool PN2S_CALL PN2_GetNestedPart(PN2_Session h, PN2_Result r, PN2_Part part, PN2_Point *pos,
                                     PN2_Orientation *pose) {
    return invoke<int>([&] {
        auto &s = session(h);
        lookup(s.parts, id(part));
        auto &result = lookup(s.results, id(r));
        require(pos && pose, PN2S_INVALID, "Null placement output");
        for (auto p : result.placed)
            if (p.part == id(part)) {
                *pos = p.position;
                *pose = p.orientation;
                return 1;
            }
        return 0;
    });
}
PN2_MultiResult PN2S_CALL PN2_MultiPreNest(PN2_Session h,int ns,const PN2_Sheet *sheets,
    const int *counts,const PN2_Part *parts,const PN2_Orientation *poses,const PN2_Point *positions){
    return invoke<PN2_MultiResult>([&]{
        auto &s=session(h);
        require(ns>0&&ns<=10000&&sheets&&counts,PN2S_INVALID,"Manual multi input needs 1..10000 physical boards");
        std::vector<OrdinaryMultiPredefinedSheet> stocks;stocks.reserve(size_t(ns));
        std::vector<int> copiedCounts;copiedCounts.reserve(size_t(ns));size_t total=0;
        Input input;input.pitch=s.pitch;input.gap=s.gap;input.randomSeed=s.randomSeed;
        std::vector<recovered::PitchSheet> pitchSheets;
        for(int board=0;board<ns;++board){
            const auto sheet=id(sheets[board]);const auto &stock=lookup(s.sheets,sheet);
            require(!stock.materialRoute && !s.materialGap,PN2S_UNSUPPORTED,
                    "Manual MultiPreNest is not connected to the material-sheet route");
            require(counts[board]>=0&&counts[board]<=10000,PN2S_INVALID,"Invalid manual board part count");
            total+=size_t(counts[board]);require(total<=10000,PN2S_RESOURCE_LIMIT,"Manual multi concrete-instance limit");
            copiedCounts.push_back(counts[board]);stocks.push_back({sheet,stock.width,stock.height,{}});
            input.width=std::max(input.width,stock.width);input.height=std::max(input.height,stock.height);
            pitchSheets.push_back({stock.width,0,0});
        }
        require(total==0||(parts&&poses&&positions),PN2S_INVALID,"Missing flat manual part arrays");
        std::vector<Located> records;records.reserve(total);std::set<Id> unique;
        for(size_t index=0;index<total;++index){const auto part=id(parts[index]);
            require(!lookup(s.parts,part).materialRoute,PN2S_UNSUPPORTED,
                    "Manual MultiPreNest is not connected to material orientation catalogs");
            require(unique.insert(part).second,PN2S_UNSUPPORTED,"Repeated SDK identity in global manual multi input");
            require(std::isfinite(poses[index].min_angle)&&std::isfinite(poses[index].max_angle)&&
                recovered::predefinedOrientationValid({poses[index].horizontal_flip,poses[index].min_angle,poses[index].max_angle}),
                PN2S_INVALID,"Invalid manual multi orientation");
            validateContourPoint(positions[index]);records.push_back({part,positions[index],poses[index]});
        }
        std::vector<Part> types;std::vector<std::vector<Id>> instances;
        std::map<Id,std::pair<size_t,size_t>> concrete;
        // The original global producer builds demand from its ordered SDK-ID set.
        for(const auto key:unique){const auto &part=lookup(s.parts,key);const auto &shape=lookup(s.shapes,part.shape);
            size_t group=0;for(;group<types.size()&&!same(types[group],part);++group){}
            if(group==types.size()){
                require(types.size()<128,PN2S_RESOURCE_LIMIT,"Maximum 128 distinct manual part groups");
                types.push_back(part);instances.emplace_back();
                input.parts.push_back({shape.polygon,shape.pivot,part.poses,0,shape.holes,shape.protection});
            }
            const auto ordinal=instances[group].size();instances[group].push_back(key);++input.parts[group].quantity;
            concrete.emplace(key,std::pair{group,ordinal});
        }
        size_t offset=0;
        for(size_t board=0;board<stocks.size();++board)for(int count=0;count<copiedCounts[board];++count,++offset){
            const auto &record=records[offset];const auto [group,ordinal]=concrete.at(record.part);
            stocks[board].predefined.records.push_back({group,ordinal,{record.part,record.position,
                {record.orientation.horizontal_flip,record.orientation.min_angle,record.orientation.max_angle},lookup(s.parts,record.part).glueMode}});
        }
        if(!input.parts.empty()){
            const auto context=ordinaryContext(input);
            input.pitch=double(recovered::contextPitch(pitchSheets,s.pitch,context.width218,50.,false))/1e6;
        }
        Budget budget(1.);auto scene=ordinaryMultiPreNest(input,stocks,budget);
        std::vector<Result> boards;boards.reserve(scene->boards.size());
        for(const auto &source:scene->boards){Result result;result.sheet=source->source.sheetIdentity;
            for(const auto &placement:source->output.placements){const auto key=instances.at(placement.group).at(placement.ordinal);
                const auto &part=input.parts.at(placement.group);const auto pose=part.poses.at(placement.poseIndex);
                const Point point{double(placement.x)/1e6+source->source.predefined.sheetOrigin.x,
                                  double(placement.y)/1000.+source->source.predefined.sheetOrigin.y};
                result.placed.push_back({key,point,{int(pose.flip),pose.angle,pose.angle}});
                for(const auto vertex:transform(part.polygon,part.pivot,pose))result.length=std::max(result.length,point.x+vertex.x);
            }
            boards.push_back(std::move(result));
        }
        Multi result;result.unplaced.assign(scene->unplaced.begin(),scene->unplaced.end());result.predefinedScene=std::move(scene);
        return handle<PN2_MultiResult>(publishMulti(s,std::move(boards),std::move(result)));
    });
}
PN2_MultiResult PN2S_CALL PN2_MultiNest(PN2_Session h, const PN2_Sheet *sheets, int ns,
                                        const int *quantities, const PN2_Part *parts, int n,
                                        double seconds) {
    return invoke<PN2_MultiResult>([&] {
        auto &s = session(h);
        require(sheets && quantities && ns > 0, PN2S_INVALID, "Missing sheet input");
        require(ns <= (s.materialGap ? 100 : 64) && n >= 0 && n <= 10000 && (n == 0 || parts), PN2S_INVALID,
                "Invalid multi-sheet input size");
        require(std::isfinite(seconds) && seconds > 0 && seconds <= 3600, PN2S_INVALID,
                "Invalid time budget");
        Budget budget(seconds);
        budget.check(true);
        std::vector<Id> stocks, instances;
        std::vector<int> inventory;
        std::set<Id> unique;
        for (int i = 0; i < ns; i++) {
            lookup(s.sheets, id(sheets[i]));
            require(unique.insert(id(sheets[i])).second, PN2S_INVALID, "Repeated sheet type handle");
            require(quantities[i] >= 0 && quantities[i] <= 10000, PN2S_INVALID,
                    "Stock quantity must be in [0,10000]");
            stocks.push_back(id(sheets[i]));
            inventory.push_back(quantities[i]);
        }
        unique.clear();
        for (int i = 0; i < n; i++) {
            lookup(s.parts, id(parts[i]));
            require(unique.insert(id(parts[i])).second, PN2S_INVALID, "Repeated part instance");
            instances.push_back(id(parts[i]));
        }
        if (apiNeedsMaterial(s, stocks, instances))
            return handle<PN2_MultiResult>(apiMaterialMulti(s, stocks, inventory, instances, budget));
        SuppressLOffcut ordinaryObjectives(s);
        // First obtain an actual complete layout with material-checked contact
        // construction. An expensive refinement must not erase a complete plan.
        Plan best;
        s.fastConstruction = true;
        try {
            best = plan(s, stocks, inventory, instances, 0, budget);
        } catch (...) {
            s.fastConstruction = false;
            throw;
        }
        s.fastConstruction = false;
        std::vector<Result> patternLayouts = best.boards;
        if (!best.left.empty() || best.boards.size() > 1) {
            try {
                auto candidate = plan(s, stocks, inventory, instances, 0, budget);
                patternLayouts.insert(patternLayouts.end(), candidate.boards.begin(), candidate.boards.end());
                if (candidate.left.size() < best.left.size() ||
                    (candidate.left.size() == best.left.size() && candidate.stockArea <= best.stockArea))
                    best = std::move(candidate);
            } catch (const Failure &e) {
                if (!best.left.empty() || (e.code != PN2S_TIMEOUT && e.code != PN2S_RESOURCE_LIMIT)) throw;
            }
        }
        for (int policy = 1; policy < 3 && ns > 1; policy++) {
            Plan candidate;
            try {
                candidate = plan(s, stocks, inventory, instances, policy, budget);
            } catch (const Failure &e) {
                if (best.left.empty() && (e.code == PN2S_TIMEOUT || e.code == PN2S_RESOURCE_LIMIT)) break;
                throw;
            }
            patternLayouts.insert(patternLayouts.end(), candidate.boards.begin(), candidate.boards.end());
            if (candidate.left.size() < best.left.size() ||
                (candidate.left.size() == best.left.size() &&
                 (candidate.stockArea < best.stockArea - 1e-8 ||
                  (std::abs(candidate.stockArea - best.stockArea) < 1e-8 &&
                   candidate.boards.size() < best.boards.size()))))
                best = std::move(candidate);
        }
        const auto accept = [&](Plan candidate) {
            if (candidate.left.size() < best.left.size() ||
                (candidate.left.size() == best.left.size() &&
                 (candidate.stockArea < best.stockArea - 1e-8 ||
                  (std::abs(candidate.stockArea - best.stockArea) < 1e-8 && candidate.boards.size() < best.boards.size()))))
                best = std::move(candidate);
        };
        const auto completeDeadline=budget.deadline;
        if(s.lOffcut){
            const double remaining=std::chrono::duration<double>(completeDeadline-std::chrono::steady_clock::now()).count();
            if(remaining>0.){
                // 5bc79f..5bc807. The public stock model has no predefined
                // sheets; P_POLISH_GLUE_MORE_COMMON_CUT defaults to 2.
                const auto phases=recovered::multiPostBudget(remaining,2.,false);
                budget.deadline-=std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(phases.polishReserve));
            }
        }
        if (best.boards.size() > 1) {
            try {
                accept(masterPlan(s, stocks, inventory, instances, patternLayouts, budget));
                recovered::Random random(s.randomSeed);
                accept(lnsPlan(s, stocks, inventory, instances, best, random, budget));
            } catch (const Failure &e) {
                if (e.code != PN2S_TIMEOUT && e.code != PN2S_RESOURCE_LIMIT) throw;
            } catch (const std::bad_alloc &) {
                // Optional optimization retains the last accepted full plan.
            }
        }
        budget.deadline=completeDeadline;
        ordinaryObjectives.restore();
        if(s.lOffcut && !best.boards.empty()) {
            try {polishLOffcutPlan(s,best.boards,stocks,inventory,best.stockArea,budget);}
            catch(const Failure &failure){if(failure.code!=PN2S_TIMEOUT && failure.code!=PN2S_RESOURCE_LIMIT)throw;}
        }
        if (!best.left.empty()) budget.check(true);
        Multi multi;
        multi.unplaced = best.left;
        return handle<PN2_MultiResult>(publishMulti(s,std::move(best.boards),std::move(multi)));
    });
}
#include "api_multi_continuation.inl"
#include "api_multiheads.inl"

PN2_ErrorCode PN2S_CALL PN2_GetMultiResultInfos(PN2_Session h, PN2_MultiResult m, int *n) {
    return invoke<int>([&] {
        auto &result = lookup(session(h).multis, id(m));
        require(n, PN2S_INVALID, "Null count output");
        *n = static_cast<int>(result.results.size());
        return 0;
    });
}
PN2_Result PN2S_CALL PN2_GetResult(PN2_Session h, PN2_MultiResult m, int index, PN2_Sheet *sh) {
    return invoke<PN2_Result>([&] {
        auto &s = session(h);
        auto &multi = lookup(s.multis, id(m));
        require(index >= 0 && static_cast<size_t>(index) < multi.results.size() && sh, PN2S_INVALID,
                "Invalid result index");
        Id r = multi.results[static_cast<size_t>(index)];
        *sh = handle<PN2_Sheet>(lookup(s.results, r).sheet);
        return handle<PN2_Result>(r);
    });
}
PN2_ErrorCode PN2S_CALL PN2_ShapeAddHole(PN2_Session h, PN2_Shape sh, const PN2_Point *points, int n) {
    return invoke<int>([&]() -> int {
        auto &shape = lookup(session(h).shapes, id(sh));
        require(points && n >= 3 && n <= 8192, PN2S_INVALID, "Need 3..8192 hole vertices");
        require(shape.holes.size() < 512, PN2S_RESOURCE_LIMIT, "Maximum 512 holes per shape");
        Polygon hole(points, points + n);
        if (hole.size() > 3 && hole.front().x == hole.back().x && hole.front().y == hole.back().y)
            hole.pop_back();
        if (area(hole) < 0)
            std::reverse(hole.begin(), hole.end());
        validate_polygon(hole);
        validate_hole(shape.polygon, shape.holes, hole);
        shape.holes.push_back(std::move(hole));
        return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2S_GetMultiUnplacedCount(PN2_Session h, PN2_MultiResult m, int *n) {
    return invoke<int>([&] {
        auto &multi = lookup(session(h).multis, id(m));
        require(n, PN2S_INVALID, "Null output pointer");
        *n = static_cast<int>(multi.unplaced.size());
        return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2S_GetMultiUnplacedPart(PN2_Session h, PN2_MultiResult m, int index,
                                                  PN2_Part *p) {
    return invoke<int>([&] {
        auto &multi = lookup(session(h).multis, id(m));
        require(p && index >= 0 && static_cast<size_t>(index) < multi.unplaced.size(), PN2S_INVALID,
                "Invalid unplaced index");
        *p = handle<PN2_Part>(multi.unplaced[static_cast<size_t>(index)]);
        return 0;
    });
}
PN2_ErrorCode PN2S_CALL PN2_ShapeSetProtectionOffset(PN2_Session h, PN2_Shape sh, double x) {
    return invoke<int>([&] {
        auto &shape = lookup(session(h).shapes, id(sh));
        require(std::isfinite(x) && x >= 0 && x <= 1e6, PN2S_INVALID, "Invalid protection offset");
        shape.protection = x;
        return 0;
    });
}
PN2_Result PN2S_CALL PN2_Shake(PN2_Session h, PN2_Result r, double seconds) {
    return invoke<PN2_Result>([&]() -> PN2_Result {
        auto &s = session(h);
        auto result = optimizeResult(s, lookup(s.results, id(r)), std::nullopt, seconds);
        const auto key = token();
        s.results.emplace(key, std::move(result));
        return handle<PN2_Result>(key);
    });
}
PN2_Result PN2S_CALL PN2_Compact(PN2_Session h, PN2_Result r, int direction, double seconds) {
    return invoke<PN2_Result>([&]() -> PN2_Result {
        auto &s = session(h);
        auto result = optimizeResult(s, lookup(s.results, id(r)), direction, seconds);
        const auto key = token();
        s.results.emplace(key, std::move(result));
        return handle<PN2_Result>(key);
    });
}
PN2_Bool PN2S_CALL PN2_IsObjectError(void *p) {
    return objectError(p) != 0;
}
PN2_ErrorCode PN2S_CALL PN2_ErrorCodeFromObject(void *p) {
    return objectError(p);
}
const char *PN2S_CALL PN2_ErrorMessage(int c) {
    switch (c) {
    case 0:
        return "OK";
    case -973:
        return "Invalid cutting-head argument or linked-part index";
    case PN2S_INVALID:
        return "Invalid argument or handle";
    case PN2S_UNSUPPORTED:
        return "Unsupported feature or input in this research subset";
    case PN2S_RESOURCE_LIMIT:
        return "Research resource limit exceeded";
    case PN2S_TIMEOUT:
        return "Time budget expired; no partial result published";
    case PN2S_INTERNAL:
        return "Internal analysis implementation error";
    default:
        return "Unknown error code";
    }
}
const char *PN2S_CALL PN2_ErrorMessageFromObject(void *p) {
    return PN2_ErrorMessage(objectError(p));
}
#include "api_material_exports.inl"
}
