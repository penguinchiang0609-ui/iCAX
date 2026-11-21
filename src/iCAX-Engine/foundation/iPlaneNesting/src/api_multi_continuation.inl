// Included by api.cpp after its TU-private Session/Multi/publication helpers.
// Keep native C++ declarations outside a surrounding public extern "C" block.
extern "C++" {
#include "ordinary_multi_continuation_mapped.hpp"
#include "recovered_multi_continuation.hpp"
}

extern "C" PN2S_API PN2_MultiResult PN2S_CALL PN2_MultiNestMore(
    PN2_Session h,PN2_MultiResult old,const PN2_Sheet *sheets,int ns,
    const int *quantities,PN2_Part *parts,int n,double seconds){
    return invoke<PN2_MultiResult>([&]{
        auto &s=session(h);const auto &source=lookup(s.multis,id(old));
        require(!source.materialInfo && !s.materialGap,PN2S_UNSUPPORTED,
                "Legacy MultiNestMore is not connected to material-layout continuation");
        require(ns>=0&&ns<=64&&(ns==0||(sheets&&quantities)),PN2S_INVALID,
                "Continuation needs 0..64 new stock types and their quantities");
        require(n>=0&&n<=10000&&(n==0||parts),PN2S_INVALID,
                "Continuation needs 0..10000 explicitly added SDK instances");
        require(std::isfinite(seconds)&&seconds>0&&seconds<=3600,PN2S_INVALID,
                "Multi continuation needs a time budget in (0,3600]");
        require(s.strategy==PN2S_OFFSET_RECOVERED,PN2S_UNSUPPORTED,
                "MultiNestMore currently requires the explicitly selected recovered Offset strategy; default global continuation is not substituted");
        require(bool(source.predefinedScene),PN2S_UNSUPPORTED,
                "MultiNestMore requires a live global MultiPreNest or mapped continuation scene");
        require(!s.lOffcut,PN2S_UNSUPPORTED,
                "Mapped multi continuation does not implement the L-offcut objective");
        const auto previous=source.predefinedScene;
        require(source.results.size()==previous->boards.size(),PN2S_INTERNAL,
                "Published multi boards differ from the retained global native scene");
        Budget budget(seconds);budget.check(true);

        Input input;input.pitch=s.pitch;input.gap=s.gap;input.randomSeed=s.randomSeed;
        input.recoveredOffset=true;
        std::vector<OrdinaryMultiPredefinedSheet> stock;
        std::vector<recovered::PitchSheet> pitchSheets;
        pitchSheets.reserve(size_t(ns)+previous->boards.size());
        size_t physicalCount=previous->boards.size();
        for(int index=0;index<ns;++index){
            const auto key=id(sheets[index]);const auto &sheet=lookup(s.sheets,key);
            require(!sheet.materialRoute,PN2S_UNSUPPORTED,
                    "Legacy MultiNestMore cannot consume polygonal sheets or sheet defects");
            require(quantities[index]>=0&&quantities[index]<=10000,PN2S_INVALID,
                    "New stock quantity must be in [0,10000]");
            physicalCount+=size_t(quantities[index]);
            require(physicalCount<=10000,PN2S_RESOURCE_LIMIT,
                    "Multi continuation supports at most 10000 old and available new physical boards");
            pitchSheets.push_back({sheet.width,0,0});
            input.width=std::max(input.width,sheet.width);input.height=std::max(input.height,sheet.height);
            for(int copy=0;copy<quantities[index];++copy)stock.push_back({key,sheet.width,sheet.height,{}});
        }

        // 674f40 copies this call's additions, then appends only actual previous
        // board-result records. The previous full demand/unplaced list is not
        // a source; callers may explicitly add one of those missing IDs again.
        std::vector<std::vector<uintptr_t>> previousPlaced;
        std::set<Id> retainedIds;size_t retainedCount=0;
        previousPlaced.reserve(source.results.size());
        for(size_t index=0;index<source.results.size();++index){
            budget.check();const auto &published=lookup(s.results,source.results[index]);
            const auto &board=*previous->boards[index];
            require(published.sheet==board.source.sheetIdentity&&
                    published.placed.size()==board.nesting.placements.size(),PN2S_INTERNAL,
                    "Published previous result no longer matches its actual native owner");
            const auto &sheet=lookup(s.sheets,published.sheet);
            pitchSheets.push_back({sheet.width,0,0});
            input.width=std::max(input.width,sheet.width);input.height=std::max(input.height,sheet.height);
            previousPlaced.emplace_back();previousPlaced.back().reserve(published.placed.size());
            for(size_t position=0;position<published.placed.size();++position){
                const auto key=published.placed[position].part;
                const auto nativeIndex=size_t(board.nesting.placements[position]->instance18);
                require(nativeIndex<previous->sdkIdentities.size()&&previous->sdkIdentities[nativeIndex]==key,
                        PN2S_INTERNAL,"Previous SDK record differs from its stable native concrete identity");
                require(retainedIds.insert(key).second,PN2S_UNSUPPORTED,
                        "Repeated previous placed SDK identity is unsupported");
                const auto &part=lookup(s.parts,key);
                require(!recovered::predefinedMovable(part.glueMode,false),PN2S_UNSUPPORTED,
                        "Multi continuation of current glue mode 1 needs the original moving-predefined phase");
                previousPlaced.back().push_back(key);++retainedCount;
            }
        }
        require(retainedCount+size_t(n)<=10000,PN2S_RESOURCE_LIMIT,
                "Multi continuation concrete-instance limit");
        std::vector<uintptr_t> additions;additions.reserve(size_t(n));
        for(int index=0;index<n;++index){const auto key=id(parts[index]);
            require(!lookup(s.parts,key).materialRoute,PN2S_UNSUPPORTED,
                    "Legacy MultiNestMore is not connected to material orientation catalogs");
            additions.push_back(key);}
        const auto demand=recovered::multiContinuationDemand(additions,previousPlaced);
        require(demand.repeated==0,PN2S_UNSUPPORTED,
                "Repeated retained or explicitly added SDK identity is unsupported");

        std::vector<Part> types;
        std::vector<std::vector<Id>> instances;
        std::map<Id,std::pair<size_t,size_t>> concrete;
        for(const auto key:demand.identities){
            budget.check();const auto &part=lookup(s.parts,key);const auto &shape=lookup(s.shapes,part.shape);
            size_t group=0;for(;group<types.size()&&!same(types[group],part);++group){}
            if(group==types.size()){
                require(types.size()<128,PN2S_RESOURCE_LIMIT,"Maximum 128 joined multi-continuation groups");
                types.push_back(part);instances.emplace_back();
                input.parts.push_back({shape.polygon,shape.pivot,part.poses,0,shape.holes,shape.protection});
            }
            const auto ordinal=instances[group].size();instances[group].push_back(key);
            ++input.parts[group].quantity;concrete.emplace(key,std::pair{group,ordinal});
        }
        std::vector<OrdinaryMultiAddedInstance> retainedMapping,addedMapping;
        retainedMapping.reserve(retainedCount);addedMapping.reserve(additions.size());
        for(const auto &board:previousPlaced)for(const auto key:board){const auto [group,ordinal]=concrete.at(key);retainedMapping.push_back({group,ordinal,key});}
        for(const auto key:additions){const auto [group,ordinal]=concrete.at(key);addedMapping.push_back({group,ordinal,key});}
        if(!input.parts.empty()){
            const auto context=ordinaryContext(input);
            input.pitch=double(recovered::contextPitch(pitchSheets,s.pitch,context.width218,50.,false))/1e6;
        }
        const ReverseRunnerFactory runner=[](const Input &material)->ReverseRunner{
            const auto program=offsetProgram(material);
            return [program](const Input &source,Budget &time,OrdinaryState &state){
                return ordinaryOffsetContinue(source,program,time,state);
            };
        };
        auto scene=ordinaryMultiNestMoreMapped(*previous,input,retainedMapping,addedMapping,stock,runner,budget);
        std::vector<Result> boards;boards.reserve(scene->boards.size());
        for(const auto &board:scene->boards){
            Result result;result.sheet=board->source.sheetIdentity;
            for(size_t index=0;index<board->output.placements.size();++index){
                const auto &placement=board->output.placements[index];
                const auto nativeIndex=size_t(board->nesting.placements.at(index)->instance18);
                const auto key=scene->sdkIdentities.at(nativeIndex);
                require(key==instances.at(placement.group).at(placement.ordinal),PN2S_INTERNAL,
                        "Continued output differs from its stable global SDK mapping");
                const auto &part=input.parts.at(placement.group);auto pose=part.poses.at(placement.poseIndex);
                if(std::isfinite(placement.solvedAngle))pose.angle=pose.upper=placement.solvedAngle;
                const Point point{(std::isfinite(placement.exactX)?placement.exactX:double(placement.x)/1e6)+board->source.predefined.sheetOrigin.x,
                                  (std::isfinite(placement.exactY)?placement.exactY:double(placement.y)/1000.)+board->source.predefined.sheetOrigin.y};
                result.placed.push_back({key,point,{int(pose.flip),pose.angle,pose.angle}});
                for(const auto vertex:transform(part.polygon,part.pivot,pose))result.length=std::max(result.length,point.x+vertex.x);
            }
            boards.push_back(std::move(result));
        }
        Multi result;result.unplaced.assign(scene->unplaced.begin(),scene->unplaced.end());
        result.predefinedScene=std::move(scene);
        return handle<PN2_MultiResult>(publishMulti(s,std::move(boards),std::move(result)));
    });
}
