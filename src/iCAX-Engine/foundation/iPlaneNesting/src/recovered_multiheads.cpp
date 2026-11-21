#include "recovered_multiheads.hpp"
namespace study::recovered {
MultiheadsParameters multiheadsParameters(const MultiheadsHardware &hardware){
    MultiheadsParameters result;result.heads=hardware.heads48;result.minimumGap=hardware.minimumGap58;
    if(hardware.maximumGap60>0)result.maximumGap=hardware.maximumGap60;
    if(hardware.fixedDivisions50>1)result.fixedDivisions=hardware.fixedDivisions50;
    return result;
}
bool multiheadsPreNestCompatible(const MultiheadsPreNest &pre,double config90){
    const auto mode=truncateInt32(config90);
    if(mode<=0||(pre.flag2b0&&pre.flag2b8&&pre.flag2c0))
        return mode<=1&&(pre.modelCount198_8!=0||pre.tailCount198_38!=0);
    return true;
}
bool multiheadsDivisionApplicable(const MultiheadsSpecialInput &input){
    return !input.hasPlacements&&!input.hasPreNestPlacements&&input.priorityCount108<2&&!input.specialMaterial394170;
}
std::vector<MultiheadsChild> multiheadsChildren(const MultiheadsHardware &hardware,
    const MultiheadsSpecialInput &input,int32_t trial,double selection,double reporting){
    std::vector<MultiheadsChild> result;const auto parameters=multiheadsParameters(hardware);
    const bool instrumented=truncateInt32(reporting)==1;
    if(truncateInt32(selection)!=1&&multiheadsDivisionApplicable(input)){
        MultiheadsChild child;child.kind=MultiheadsChildKind::Division;child.parameters=parameters;
        child.instrumented=instrumented;child.randomizeDivisions=trial>5;result.push_back(child);
    }
    if(!result.empty()&&truncateInt32(selection)==0)return result;
    MultiheadsChild child;child.parameters=parameters;child.instrumented=instrumented;
    const double variation=std::pow(double(trial),.3);
    child.clusterTrials=std::max(1,truncateInt32(variation));child.clusterVariation=variation/10.;
    child.randomCluster=trial>2;child.halfClusters=parameters.fixedDivisions==0&&trial>5;
    result.push_back(child);return result;
}
int32_t multiheadsDivisionCount(const MultiheadsDivisionInput &input,Random &random,bool randomize){
    const auto &parameters=input.parameters;
    if(parameters.fixedDivisions>0)return parameters.fixedDivisions;
    const auto selected=randomize&&parameters.heads>2?random.integer(2,parameters.heads):parameters.heads;
    const double height=(input.sheetHeight-input.bottom)-input.top;
    int32_t maximum=parameters.minimumGap==0?INT32_MAX:truncateInt32(height/parameters.minimumGap);
    const auto minimum=truncateInt32(height/parameters.maximumGap);
    double smallest=std::numeric_limits<double>::max();
    for(const auto value:input.partHeights)if(!(smallest<=value))smallest=value;
    maximum=std::min(maximum,truncateInt32((input.maximumPartGap+height)/(input.maximumPartGap+smallest)));
    int32_t result;
    if(maximum<minimum)result=1;
    else result=maximum<selected?maximum:minimum>selected?minimum:selected;
    result=std::min(parameters.heads,result);int32_t available=0;
    for(const auto value:input.availableQuantities)available=std::max(available,value);
    if(result>1&&available>0&&available<result)result=available;
    return result;
}
MultiheadsDivisionPlan multiheadsDivisionPlan(int32_t divisions,double height,double gap,const MultiheadsParameters &parameters){
    MultiheadsDivisionPlan result;result.divisions=divisions;if(divisions<2)return result;
    const double low=parameters.minimumGap-gap,middle=((height-.001)-gap*double(divisions-1))/double(divisions),high=parameters.maximumGap-gap;
    // Native cmovbe order: the maximum of low/middle when middle<=high,
    // otherwise high. Do not normalize invalid or unordered limits here.
    const double selected=middle<=high?(low<=middle?middle:low):high;
    result.laneHeight=selected;result.repeatedLaneHeight=selected;result.repetitionPitch=selected+gap;
    result.repetitions=truncateInt32(((height+.000001)+gap)/(result.repetitionPitch*double(divisions)));
    if(result.repetitions==0){result.laneHeight=height-(parameters.minimumGap+gap)*double(divisions-1);result.repetitions=1;if(result.laneHeight<=0)return result;}
    result.usable=true;return result;
}
int32_t multiheadsDivisionQuantity(int32_t quantity,const MultiheadsDivisionPlan &plan,bool fixed){
    if(quantity<2)return quantity;
    const auto product=int32_t(uint32_t(plan.divisions)*uint32_t(plan.repetitions));
    require(product!=0,PN2S_INVALID,"Multiheads division repetition product is zero");
    auto selected=quantity/product;const auto threshold=fixed?plan.divisions:2;
    if(quantity%product>=threshold)++selected;
    return quantity-selected;
}
int32_t multiheadsDivisionMethod(int32_t forced,std::span<const MultiheadsMethodGroup> groups,double height,bool rectangles,Random &random){
    if(forced!=0)return forced;
    std::array<int32_t,3> methods{1,0,0};int32_t count=1;
    for(const auto &group:groups)if(group.count<=group.available&&group.maximumY-group.minimumY<=height){methods[count++]=2;break;}
    if(rectangles)methods[count++]=3;
    return methods[size_t(random.integer(0,count-1))];
}
bool multiheadsClusterLess(const MultiheadsClusterKey &first,const MultiheadsClusterKey &second){
    return first.heads<second.heads||(first.heads==second.heads&&first.pitch<second.pitch);
}
std::vector<MultiheadsClusterKey> multiheadsClusterKeys(int32_t trials,bool halves,const MultiheadsClusterChooser &choose){
    std::vector<MultiheadsClusterKey> result;std::vector<uintptr_t> work;
    int32_t remaining=int32_t(uint32_t(trials)*3);
    const auto insert=[&](MultiheadsClusterKey key){auto at=std::lower_bound(result.begin(),result.end(),key,multiheadsClusterLess);
        if(at==result.end()||multiheadsClusterLess(key,*at))result.insert(at,key);};
    while(int32_t(uint32_t(result.size()))<trials){if(remaining<0)break;--remaining;const auto key=choose(work);
        if(key.heads==0)break;if(key.heads==1)continue;insert(key);
        if(halves&&key.heads>3)insert({key.heads/2,key.pitch});
    }
    return result;
}
int32_t runMultiheadsDivision(const MultiheadsChild &child,int32_t divisions,double gap,MultiheadsDivisionHost &host){
    host.assertEmpty(!host.hasPlacements());if(divisions<2)return 0;
    const bool irregular=host.irregularSheet();if(irregular)host.useBoundsSheet();
    const auto plan=multiheadsDivisionPlan(divisions,host.usableHeight(),gap,child.parameters);
    if(!plan.usable)return 0;
    const bool fixed=child.parameters.fixedDivisions>1;
    for(size_t n=0;n<host.groupCount();++n){const auto quantity=host.quantity(n);
        const auto replacement=multiheadsDivisionQuantity(quantity,plan,fixed);
        if(quantity>=2&&!host.setQuantity(n,replacement))host.setQuantity(n,quantity);
        else if(quantity<2)host.setQuantity(n,quantity);
    }
    host.useLane(host.usableWidth(),plan.laneHeight);const auto method=host.chooseMethod();host.runChild(method);host.postChild();
    host.restoreQuantities();host.restoreSheet();host.repeat(plan);
    if(!fixed)host.runChild(irregular?1:method);
    return int32_t(uint32_t(host.placementCount()));
}
void multiheadsReplicate(const MultiheadsDivisionPlan &plan,double originX,double originY,bool fixed,MultiheadsReplicationHost &host){
    const auto sources=host.placements();const auto dx=rounded(originX*1e6);
    const auto dy=truncateInt32(originY*1e3+(originY<0?-.5:.5));
    for(const auto source:sources){auto position=host.position(source);position.x=int64_t(uint64_t(position.x)+uint64_t(dx));
        position.y=int32_t(uint32_t(position.y)+uint32_t(dy));host.move(source,position);}
    std::map<std::tuple<int32_t,int32_t,size_t>,size_t> copies;
    for(const auto source:sources){auto group=host.createHeadGroup(source);host.attachHeadGroup(source,group);
        std::vector<size_t> pending{source};bool failed=false;
        for(int32_t batch=0;batch<plan.repetitions;++batch){
            for(int32_t head=0;head<plan.divisions;++head){
                if(batch==0&&head==0)continue;
                const auto index=int32_t(uint32_t(plan.divisions)*uint32_t(batch)+uint32_t(head));
                const double translated=double(index)*plan.repetitionPitch*1e3;
                const auto offset=truncateInt32(translated+(translated<0?-.5:.5));
                auto position=host.position(source);position.y=int32_t(uint32_t(position.y)+uint32_t(offset));
                const auto copy=host.cloneAvailable(source,position);
                if(!copy){if(head==1&&!pending.empty())for(const auto item:pending)host.attachHeadGroup(item,{});
                    if(fixed)for(auto at=pending.rbegin();at!=pending.rend();++at)if(!host.remove(*at))break;
                    failed=true;break;}
                copies[{batch,head,source}]=*copy;host.copyRelationships(source,*copy,batch,head,copies);host.commit(*copy);
                if(head==0)group=host.createHeadGroup(*copy);else host.appendHeadGroup(group,*copy);
                pending.push_back(*copy);host.attachHeadGroup(*copy,group);
            }
            pending.clear();if(failed)break;
        }
    }
}
} // namespace study::recovered
