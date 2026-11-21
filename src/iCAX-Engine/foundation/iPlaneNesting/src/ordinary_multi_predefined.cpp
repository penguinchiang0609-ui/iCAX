#include "ordinary_multi_predefined.hpp"
#include <numeric>
#include <set>

namespace study {
OrdinaryMultiPredefinedBoard::OrdinaryMultiPredefinedBoard(OrdinaryMultiPredefinedSheet value)
    :source(std::move(value)),lifecycle(nesting,std::span<const size_t>{}) {
    lifecycle.validateOnCommit=false;nesting.box08=source.width;nesting.box10=source.height;
    nesting.sheet70=source.predefined.sheetOrigin.x;nesting.sheet78=source.predefined.sheetOrigin.y;
}
namespace {
class SharedInstanceScope {
    OrdinaryMultiPredefinedScene &scene;
    recovered::PlacementLifecycle &lifecycle;
public:
    SharedInstanceScope(OrdinaryMultiPredefinedScene &s,recovered::PlacementLifecycle &l):scene(s),lifecycle(l){
        std::swap(scene.placementCatalog,lifecycle.catalog);std::swap(scene.instances,lifecycle.instances);
    }
    ~SharedInstanceScope(){std::swap(scene.placementCatalog,lifecycle.catalog);std::swap(scene.instances,lifecycle.instances);}
};
class MultiMandatoryHost final:public recovered::PredefinedNesterHost {
    OrdinaryMultiPredefinedScene &scene;
    OrdinaryMultiPredefinedBoard &board;
    Budget &budget;
    OrdinaryPredefinedTrace &trace;
    std::map<uintptr_t,recovered::PredefinedConstraintRecord> constraints;
    std::pair<size_t,size_t> key(uintptr_t instance)const{
        const auto &record=scene.catalog->instances.at(instance);return {record.group,record.ordinal};
    }
public:
    MultiMandatoryHost(OrdinaryMultiPredefinedScene &s,OrdinaryMultiPredefinedBoard &b,Budget &time,
                       OrdinaryPredefinedTrace &record):scene(s),board(b),budget(time),trace(record){
        for(const auto &entry:board.source.predefined.records){const auto instance=scene.instanceBySdk.at(entry.sdk.partIdentity);
            constraints.emplace(instance,recovered::predefinedConstraints(entry.sdk,board.source.predefined.sheetOrigin,
                                                                           board.source.predefined.moveDefault));}
    }
    std::vector<uintptr_t> instances(uintptr_t group)override{
        budget.check();const auto record=scene.catalog->groups.at(group).at(0);const auto &values=scene.catalog->records[record].instances;
        return {values.begin(),values.end()};
    }
    int32_t difficulty(uintptr_t instance)override{
        ++trace.difficultyQueries;if(!constraints.contains(instance))return 0;
        recovered::PredefinedDifficulty value;value.cached={100,100,100};const auto &program=board.source.predefined;
        return recovered::predefinedDifficulty(value,{program.xWeight778,program.yWeight780,2.},false);
    }
    bool placed(uintptr_t instance)override{
        const bool result=scene.catalog->instances.at(instance).placed;if(result)++trace.unavailable;return result;
    }
    std::vector<recovered::PredefinedVariant> variants(uintptr_t instance)override{
        ++trace.variantQueries;std::vector<recovered::PredefinedVariant> result;const auto found=constraints.find(instance);if(found==constraints.end())return result;
        const auto [group,ordinal]=key(instance);const auto &part=scene.input.parts[group];
        const std::array<recovered::PlacementOrientation,1> required{{found->second.orientation}};
        for(size_t pose=0;pose<part.poses.size();++pose){const auto &p=part.poses[pose];const auto actual=recovered::quantizeSdkOrientation({int32_t(p.flip),p.angle,p.angle},false);
            if(recovered::mandatoryOrientationContains(actual,required,false,false,true))result.push_back({pose,scene.modules[group][pose].leftE0});}
        return result;
    }
    std::optional<int64_t> solveX(uintptr_t instance,const recovered::PredefinedVariant &,bool,bool)override{
        ++trace.xQueries;return constraints.at(instance).x;
    }
    std::optional<int32_t> solveY(uintptr_t instance,const recovered::PredefinedVariant &,bool,bool)override{
        ++trace.yQueries;return constraints.at(instance).y;
    }
    void commit(uintptr_t instance,const recovered::PredefinedVariant &variant,int64_t x,int32_t y,bool unaligned)override{
        budget.check();const auto [group,ordinal]=key(instance);
        auto placement=std::make_shared<recovered::WarmPlacement>(scene.modules.at(group).at(variant.identity));
        placement->instance18=int32_t(instance);placement->instance40=instance;placement->x48=x;placement->y50=y;placement->rounded2c=unaligned;
        {SharedInstanceScope shared(scene,board.lifecycle);require(board.lifecycle.commit(placement),PN2S_INTERNAL,"Global manual placement instance is already owned");}
        scene.catalog->setPlaced(instance,true);const Placement output{group,ordinal,variant.identity,x,y};board.output.placements.push_back(output);trace.committed.push_back(output);
    }
};
void validateInput(const Input &input,std::span<const OrdinaryMultiPredefinedSheet> sheets){
    require(input.gap==0&&!requires_general(input)&&input.pointGroups.empty(),PN2S_UNSUPPORTED,"Multi predefined requires finite grid-aligned single rings without spacing or point groups");
    size_t total=0;for(const auto &part:input.parts){require(part.quantity>=0&&!part.poses.empty(),PN2S_INVALID,"Invalid global multi predefined part quantity/poses");total+=size_t(part.quantity);}
    require(total<=10000&&sheets.size()<=10000,PN2S_RESOURCE_LIMIT,"Multi predefined input size");
    std::set<uintptr_t> sdk;std::set<std::pair<size_t,size_t>> concrete;
    for(const auto &sheet:sheets){require(sheet.sheetIdentity!=0&&std::isfinite(sheet.width)&&std::isfinite(sheet.height)&&sheet.width>0&&sheet.height>0,PN2S_INVALID,"Invalid manual multi sheet");
        require(std::isfinite(sheet.predefined.sheetOrigin.x)&&std::isfinite(sheet.predefined.sheetOrigin.y),PN2S_INVALID,"Invalid manual sheet origin");
        for(const auto &record:sheet.predefined.records){require(record.sdk.partIdentity!=0&&record.group<input.parts.size()&&record.ordinal<size_t(input.parts[record.group].quantity),PN2S_INVALID,"Invalid global concrete instance");
            require(sdk.insert(record.sdk.partIdentity).second&&concrete.emplace(record.group,record.ordinal).second,PN2S_UNSUPPORTED,"Repeated global predefined concrete instance is unsupported");
            require(recovered::predefinedOrientationValid(record.sdk.orientation),PN2S_INVALID,"Invalid multi predefined orientation");
            require(std::isfinite(record.sdk.position.x)&&std::isfinite(record.sdk.position.y)&&std::abs(record.sdk.position.x)<=1e6&&std::abs(record.sdk.position.y)<=1e6,PN2S_UNSUPPORTED,"Multi predefined position exceeds the finite native domain");
        }
    }
    require(concrete.size()==total,PN2S_INVALID,"Global multi predefined quantities must match the exact concrete demand");
}
}
std::unique_ptr<OrdinaryMultiPredefinedScene> ordinaryMultiPreNest(const Input &input,
    std::span<const OrdinaryMultiPredefinedSheet> sheets,Budget &budget,OrdinaryMultiPredefinedTrace *record){
    validateInput(input,sheets);budget.check(true);auto scene=std::make_unique<OrdinaryMultiPredefinedScene>();scene->input=input;
    OrdinaryMultiPredefinedTrace local;auto &trace=record?*record:local;trace={};trace.sheets.resize(sheets.size());
    if(sheets.empty())return scene;
    for(const auto &sheet:sheets){scene->input.width=std::max(scene->input.width,sheet.width);scene->input.height=std::max(scene->input.height,sheet.height);}
    recovered::MultiPredefinedControllerInput controller;for(const auto &sheet:sheets)controller.sheets.push_back(sheet.sheetIdentity);
    controller.groups.emplace_back();for(size_t n=0;n<sheets.size();++n){controller.groups[0].push_back(n+1);for(const auto &entry:sheets[n].predefined.records)controller.parts.push_back(entry.sdk.partIdentity);}
    recovered::MultiPredefinedControllerHost host;
    host.buildGlobal=[&](auto,auto,auto uniqueParts,auto owners){
        require(owners.size()==sheets.size(),PN2S_INTERNAL,"Manual multi global owner count");
        if(uniqueParts.empty()){for(const auto &sheet:sheets)scene->boards.push_back(std::make_unique<OrdinaryMultiPredefinedBoard>(sheet));return;}
        OrdinaryState prepared;OrdinaryControl control;control.placementLimit=0;for(const auto &part:scene->input.parts)control.values.push_back(area(part.polygon));
        ordinaryRun(scene->input,budget,prepared,&control);++trace.globalCatalogBuilds;++trace.globalGeometryBuilds;
        scene->catalog=std::move(prepared.catalog);scene->modules=std::move(prepared.placementHost->modules);scene->placementCatalog=std::move(prepared.placementHost->lifecycle.catalog);scene->instances=std::move(prepared.placementHost->lifecycle.instances);scene->pitchX=prepared.output.pitchX;
        scene->sdkIdentities.resize(scene->catalog->instances.size());for(const auto &sheet:sheets)for(const auto &entry:sheet.predefined.records){const auto directory=scene->catalog->groups.at(entry.group).at(0);const auto instance=scene->catalog->records[directory].instances.at(entry.ordinal);scene->sdkIdentities[instance]=entry.sdk.partIdentity;scene->instanceBySdk.emplace(entry.sdk.partIdentity,instance);}
        for(const auto &group:scene->modules)for(const auto &module:group)trace.geometryOwners+=bool(module.geometryOwner);
        for(const auto &sheet:sheets){auto board=std::make_unique<OrdinaryMultiPredefinedBoard>(sheet);board->output=prepared.output;board->output.placements.clear();scene->boards.push_back(std::move(board));}
    };
    host.begin=[](double seconds,int32_t preset,bool flag){require(seconds==1.&&preset==1&&!flag,PN2S_INTERNAL,"Original manual multi technology policy");};
    host.submit=[&](auto owners,size_t){++trace.submittedGroups;for(const auto owner:owners){budget.check();const size_t n=owner-1;auto &board=*scene->boards.at(n);++trace.submittedSheets;
        if(!scene->catalog)continue;MultiMandatoryHost mandatory(*scene,board,budget,trace.sheets[n]);std::vector<uintptr_t> parts(scene->input.parts.size());std::iota(parts.begin(),parts.end(),uintptr_t(0));
        recovered::PredefinedNesterOptions options;options.minimumDifficulty=recovered::predefinedMinimumDifficulty(board.source.predefined.xWeight778,board.source.predefined.yWeight780);options.checkConstraints4e0=board.source.predefined.checkConstraints4e0;options.pitch17f0=scene->pitchX;
        trace.committed+=recovered::runPredefinedNester(mandatory,parts,options);
    }};
    host.finish=[&]{if(scene->catalog)for(size_t n=0;n<scene->sdkIdentities.size();++n)if(!scene->catalog->instances[n].placed)scene->unplaced.push_back(scene->sdkIdentities[n]);};
    recovered::runMultiPredefinedController(controller,host);return scene;
}
}
