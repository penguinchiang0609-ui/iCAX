// Source-skin interval evidence. Included in the Work's private namespace.
// Chain positions below retain the existing SourceUse index/native parameter;
// they never interpolate geometry or a tool axis, or round tiny native gaps
// into one normalized floating-point coordinate.
struct SkinEndpointRef final
{
    std::uint64_t EdgeId=0;
    double Parameter=0.;
    TopoDS_Vertex Vertex;
};

SkinEndpointRef SkinEndpoint(const SMachiningPathSourceUse& use,bool start,
    const O::SOpenCascadeBRepBuildResult& built)
{
    SkinEndpointRef result;result.EdgeId=use.EdgeId;
    result.Parameter=start?(use.bReversed?use.LastParameter:use.FirstParameter):
        (use.bReversed?use.FirstParameter:use.LastParameter);
    const auto found=built.Edges.find(use.EdgeId);
    if(found==built.Edges.end())return result;
    BRepAdaptor_Curve curve(found->second);TopoDS_Vertex first,last;
    TopExp::Vertices(found->second,first,last,false);
    if(result.Parameter==curve.FirstParameter())result.Vertex=first;
    else if(result.Parameter==curve.LastParameter())result.Vertex=last;
    return result;
}

const char* SharedSkinEndpointBasis(const SkinEndpointRef& first,const SkinEndpointRef& second)
{
    if(!first.Vertex.IsNull()&&!second.Vertex.IsNull()&&first.Vertex.IsSame(second.Vertex))
        return "shared-topological-vertex";
    // A stock-skin transition may lie inside an unchanged Topo edge. Its two
    // logical spans share one exact native event, even without a TopoVertex.
    if(first.EdgeId==second.EdgeId&&first.Parameter==second.Parameter)
        return "shared-native-edge-parameter-event";
    return nullptr;
}

const char* SideTouchesSkinEnd(const SMachiningPathEdgeAnalysis& source,
    const SMachiningPathEdgeAnalysis& side,bool start,const O::SOpenCascadeBRepBuildResult& built)
{
    if(!source.bSelectedPath||source.bClosed||!side.bSide||side.FaceId!=source.FaceId||side.WireId!=source.WireId
        ||source.SourceUses.empty()||side.SourceUses.empty())return nullptr;
    const auto endpoint=SkinEndpoint(start?source.SourceUses.front():source.SourceUses.back(),start,built);
    if(const auto* basis=SharedSkinEndpointBasis(endpoint,SkinEndpoint(side.SourceUses.front(),true,built)))return basis;
    return SharedSkinEndpointBasis(endpoint,SkinEndpoint(side.SourceUses.back(),false,built));
}

std::string WitnessLogicKey(const SMachiningFaceCutArrival& arrival,
    const SMachiningPathAnalysisResult& paths)
{
    for(const auto& path:paths.Edges)if(path.FaceId==arrival.FaceId)
        for(const auto& use:path.SourceUses)
            if(use.EdgeId==arrival.TargetEdgeId&&use.FirstParameter<=arrival.TargetFirstParameter
                &&use.LastParameter>=arrival.TargetLastParameter)return path.Key;
    return {};
}

// Only the connected first-hit component at a real, incident trace endpoint
// can complete a side. A central concave floor is not a side completion just
// because its own endpoints happen to touch stock skin.
struct EndSideCompletion final {std::string SidePathKey,IncidenceBasis;};

std::map<std::size_t,EndSideCompletion> EndSideCompletions(
    const O::SOpenCascadeBRepBuildResult& built,const SMachiningPathAnalysisResult& paths,
    const SMachiningFaceCutCoverageResult& result)
{
    struct Position final {
        std::size_t Index=0;
        double Parameter=0.;
        bool bBoundary=true;
        bool bReversed=false;
    };
    const auto less=[](const Position& a,const Position& b){
        if(a.Index!=b.Index)return a.Index<b.Index;
        if(a.bBoundary!=b.bBoundary)return a.bBoundary;
        if(a.bBoundary)return false;
        return a.bReversed?a.Parameter>b.Parameter:a.Parameter<b.Parameter;
    };
    struct Range final {Position First,Last;std::size_t ArrivalIndex=0;};
    using Group=std::pair<std::string,std::string>;
    std::map<std::string,const SMachiningPathEdgeAnalysis*> byKey;
    for(const auto& path:paths.Edges)byKey.emplace(path.Key,&path);
    std::map<Group,std::vector<Range>> groups,targetGroups;
    for(const auto& reference:result.SideCorrespondences) {
        if(reference.ArrivalIndex>=result.Arrivals.size())continue;
        const auto& arrival=result.Arrivals[reference.ArrivalIndex];
        const auto source=byKey.find(arrival.SourcePathKey),side=byKey.find(reference.SidePathKey);
        if(source==byKey.end()||side==byKey.end()||!source->second->bSelectedPath
            ||!side->second->bSide||source->second->bClosed
            ||source->second->FaceId!=side->second->FaceId||source->second->WireId!=side->second->WireId)continue;
        const auto& uses=source->second->SourceUses;
        for(std::size_t i=0;i<uses.size();++i) {
            const auto& use=uses[i];if(use.EdgeId!=arrival.SourceEdgeId)continue;
            const double first=std::max(use.FirstParameter,std::min(arrival.SourceFirstParameter,arrival.SourceLastParameter));
            const double last=std::min(use.LastParameter,std::max(arrival.SourceFirstParameter,arrival.SourceLastParameter));
            if(!(last>first))continue;
            const auto position=[&](double parameter){
                if(parameter==(use.bReversed?use.LastParameter:use.FirstParameter))return Position{i,0.,true,false};
                if(parameter==(use.bReversed?use.FirstParameter:use.LastParameter))return Position{i+1,0.,true,false};
                return Position{i,parameter,false,use.bReversed};
            };
            groups[{arrival.SourcePathKey,reference.SidePathKey}].push_back({
                position(use.bReversed?last:first),position(use.bReversed?first:last),reference.ArrivalIndex});
        }
        // The same existing first-hit component may be certified from its
        // target-side native domain. Inverting a tolerant shared endpoint can
        // leave a positive source gap even when the unchanged target interval
        // reaches the real incident vertex exactly. This proves only these
        // arrivals; it neither fills that source gap nor moves either curve.
        // Limit the reverse-domain certificate to nonperiodic plane-line
        // charts with one ordered source S branch. Periodic generator images
        // and radial/curved normal families retain the original source-domain
        // proof until reverse correspondence continuity is separately proved.
        if(arrival.Method!="plane-line-first-physical-boundary")continue;
        const auto& sideUses=side->second->SourceUses;
        for(std::size_t i=0;i<sideUses.size();++i) {
            const auto& use=sideUses[i];
            if(use.EdgeId!=arrival.TargetEdgeId||use.SpanKey!=reference.SideSpanKey
                ||arrival.TargetFirstParameter<use.FirstParameter
                ||arrival.TargetLastParameter>use.LastParameter
                ||!(arrival.TargetLastParameter>arrival.TargetFirstParameter))continue;
            const auto position=[&](double parameter){
                if(parameter==(use.bReversed?use.LastParameter:use.FirstParameter))return Position{i,0.,true,false};
                if(parameter==(use.bReversed?use.FirstParameter:use.LastParameter))return Position{i+1,0.,true,false};
                return Position{i,parameter,false,use.bReversed};
            };
            targetGroups[{arrival.SourcePathKey,reference.SidePathKey}].push_back({
                position(use.bReversed?arrival.TargetLastParameter:arrival.TargetFirstParameter),
                position(use.bReversed?arrival.TargetFirstParameter:arrival.TargetLastParameter),reference.ArrivalIndex});
        }
    }
    std::map<std::size_t,EndSideCompletion> completed;
    for(auto& [key,ranges]:groups) {
        const auto& source=*byKey.at(key.first);const auto& side=*byKey.at(key.second);
        const auto* start=SideTouchesSkinEnd(source,side,true,built);
        const auto* end=SideTouchesSkinEnd(source,side,false,built);
        if(!start&&!end)continue;
        std::sort(ranges.begin(),ranges.end(),[&](const auto& a,const auto& b){
            return less(a.First,b.First)||(!less(b.First,a.First)&&less(a.Last,b.Last));});
        for(std::size_t begin=0;begin<ranges.size();) {
            std::size_t finish=begin+1;auto last=ranges[begin].Last;
            while(finish<ranges.size()&&!less(last,ranges[finish].First)) {
                if(less(last,ranges[finish].Last))last=ranges[finish].Last;
                ++finish;
            }
            const bool atStart=start&&ranges[begin].First.bBoundary&&ranges[begin].First.Index==0;
            const bool atEnd=end&&last.bBoundary&&last.Index==source.SourceUses.size();
            if(atStart||atEnd)for(std::size_t i=begin;i<finish;++i)
                completed.emplace(ranges[i].ArrivalIndex,EndSideCompletion{key.second,atStart?start:end});
            begin=finish;
        }
    }
    for(auto& [key,ranges]:targetGroups) {
        const auto& source=*byKey.at(key.first);const auto& side=*byKey.at(key.second);
        if(side.bClosed||source.SourceUses.empty()||side.SourceUses.empty())continue;
        const auto incidence=[&](bool sideStart)->const char* {
            const auto endpoint=SkinEndpoint(sideStart?side.SourceUses.front():side.SourceUses.back(),sideStart,built);
            if(const auto* basis=SharedSkinEndpointBasis(endpoint,SkinEndpoint(source.SourceUses.front(),true,built)))return basis;
            return SharedSkinEndpointBasis(endpoint,SkinEndpoint(source.SourceUses.back(),false,built));
        };
        const auto* start=incidence(true);const auto* end=incidence(false);
        if(!start&&!end)continue;
        std::sort(ranges.begin(),ranges.end(),[&](const auto& a,const auto& b){
            return less(a.First,b.First)||(!less(b.First,a.First)&&less(a.Last,b.Last));});
        for(std::size_t begin=0;begin<ranges.size();) {
            std::size_t finish=begin+1;auto last=ranges[begin].Last;
            while(finish<ranges.size()&&!less(last,ranges[finish].First)) {
                if(less(last,ranges[finish].Last))last=ranges[finish].Last;
                ++finish;
            }
            const bool atStart=start&&ranges[begin].First.bBoundary&&ranges[begin].First.Index==0;
            const bool atEnd=end&&last.bBoundary&&last.Index==side.SourceUses.size();
            if(atStart||atEnd)for(std::size_t i=begin;i<finish;++i)
                completed.emplace(ranges[i].ArrivalIndex,EndSideCompletion{key.second,
                    std::string(atStart?start:end)+";target-native-component"});
            begin=finish;
        }
    }
    return completed;
}

void BuildSkinEvidence(const O::SOpenCascadeBRepBuildResult& built,
    const SMachiningPathAnalysisResult& paths,SMachiningFaceCutCoverageResult& result)
{
    const auto sideCompletions=EndSideCompletions(built,paths,result);
    for(std::size_t index=0;index<result.Arrivals.size();++index) {
        const auto& arrival=result.Arrivals[index];SMachiningFaceCutEvidence evidence;
        evidence.FaceId=arrival.FaceId;evidence.SourcePathKey=arrival.SourcePathKey;
        evidence.SourceEdgeId=arrival.SourceEdgeId;
        evidence.SourceFirstParameter=std::min(arrival.SourceFirstParameter,arrival.SourceLastParameter);
        evidence.SourceLastParameter=std::max(arrival.SourceFirstParameter,arrival.SourceLastParameter);
        evidence.WitnessEdgeId=arrival.TargetEdgeId;evidence.WitnessLogicEdgeId=WitnessLogicKey(arrival,paths);
        evidence.WitnessSpanKey=arrival.TargetSpanKey;evidence.WitnessFirstParameter=arrival.TargetFirstParameter;
        evidence.WitnessLastParameter=arrival.TargetLastParameter;evidence.ArrivalIndex=index;
        if(arrival.bTargetSkinRelationValid&&arrival.TargetSkinRelation==ESkinRelation::OnSkin) {
            evidence.Kind=EFaceCutEvidenceKind::SkinExit;evidence.bThrough=evidence.bComplete=true;
            evidence.Reason="first-boundary-exits-at-stock-skin-including-same-logical-edge-far-span";
        } else if(arrival.bTargetConvexityValid&&arrival.TargetConvexity==EEdgeConvexity::Convex) {
            evidence.Kind=EFaceCutEvidenceKind::ConvexBoundary;evidence.bThrough=evidence.bComplete=true;
            evidence.Reason="first-boundary-is-convex-including-convex-logical-side";
        } else if(const auto side=sideCompletions.find(index);side!=sideCompletions.end()) {
            evidence.Kind=EFaceCutEvidenceKind::SideCompletion;evidence.bThrough=evidence.bComplete=true;
            evidence.WitnessLogicEdgeId=side->second.SidePathKey;
            evidence.Reason="first-boundary-side-component-reaches-a-shared-real-skin-trace-endpoint;"+side->second.IncidenceBasis;
        } else if(arrival.bTargetSkinRelationValid&&arrival.bTargetConvexityValid
            &&arrival.TargetConvexity==EEdgeConvexity::Concave) {
            evidence.Kind=EFaceCutEvidenceKind::ConcaveTermination;
            evidence.bComplete=true;
            evidence.Reason="first-boundary-is-concave-and-has-no-valid-end-side-completion";
        } else if(arrival.bTargetSkinRelationValid&&arrival.bTargetConvexityValid) {
            // A free or coplanar boundary is not proof of retained material.
            // It may be an unrecognized approximate stock-skin boundary or a
            // continuation into another face. Preserve the raw arrival, but
            // never turn absence of through evidence into a partial-cut fact.
            evidence.Kind=EFaceCutEvidenceKind::OtherBoundary;
            evidence.Reason="first-boundary-has-no-proven-exit-or-material-retention";
        } else {
            evidence.Kind=EFaceCutEvidenceKind::OtherBoundary;
            evidence.Reason="first-boundary-attributes-are-not-complete-for-coverage-classification";
        }
        result.Evidence.push_back(std::move(evidence));
    }
}

void BuildSourceSkinCoverage(const SMachiningPathAnalysisResult& paths,
    SMachiningFaceCutCoverageResult& result)
{
    std::map<std::string,const SMachiningFaceCutSource*> sourceInfo;
    for(const auto& source:result.Sources)sourceInfo.emplace(source.SourcePathKey,&source);
    for(const auto& path:paths.Edges)if(path.bSelectedPath) {
        std::vector<I::Input> inputs;
        for(const auto& use:path.SourceUses) {
            if(!use.bSkinRelationValid||use.SkinRelation!=ESkinRelation::OnSkin)
                throw std::runtime_error("selected-real-path-contains-a-non-skin-source-use");
            if(!std::isfinite(use.FirstParameter)||!std::isfinite(use.LastParameter)||use.FirstParameter>=use.LastParameter)
                throw std::runtime_error("skin-source-native-range-is-invalid");
            inputs.push_back({path.FaceId,use.EdgeId,use.FirstParameter,use.LastParameter,path.Key,I::Kind::Baseline,0});
        }
        for(std::size_t index=0;index<result.Evidence.size();++index) {
            const auto& evidence=result.Evidence[index];
            if(evidence.FaceId!=path.FaceId||evidence.SourcePathKey!=path.Key)continue;
            const bool belongs=std::any_of(path.SourceUses.begin(),path.SourceUses.end(),[&](const auto& use){
                return use.EdgeId==evidence.SourceEdgeId&&use.FirstParameter<=evidence.SourceFirstParameter
                    &&use.LastParameter>=evidence.SourceLastParameter;});
            if(!belongs)throw std::runtime_error("first-boundary-evidence-is-outside-its-native-skin-source-domain");
            inputs.push_back({path.FaceId,evidence.SourceEdgeId,evidence.SourceFirstParameter,evidence.SourceLastParameter,
                path.Key,I::Kind::Arrival,index});
        }
        for(const auto& atom:I::Build(inputs)) {
            if(atom.BaselineEvidenceIndices.empty())continue;
            SMachiningFaceCutCoverageSpan span;span.FaceId=path.FaceId;span.SourcePathKey=path.Key;
            span.SourceEdgeId=span.TargetEdgeId=atom.TargetEdgeId;
            span.FirstParameter=atom.First;span.LastParameter=atom.Last;
            span.SourcePathKeys=span.BaselineSourcePathKeys={path.Key};span.EvidenceIndices=atom.ArrivalEvidenceIndices;
            bool through=false,complete=!span.EvidenceIndices.empty();
            for(const auto index:span.EvidenceIndices) {
                const auto& evidence=result.Evidence.at(index);
                through=through||(evidence.bComplete&&evidence.bThrough);complete=complete&&evidence.bComplete;
                if(evidence.bHasArrival) {
                    span.ArrivalIndices.push_back(evidence.ArrivalIndex);
                    span.bExitCorrespondenceExpanded=true;
                }
            }
            std::sort(span.ArrivalIndices.begin(),span.ArrivalIndices.end());
            span.ArrivalIndices.erase(std::unique(span.ArrivalIndices.begin(),span.ArrivalIndices.end()),span.ArrivalIndices.end());
            span.bMappingComplete=through||complete;
            span.Kind=through?EFaceCutCoverageKind::Through:complete?EFaceCutCoverageKind::Partial:EFaceCutCoverageKind::Pending;
            span.Reason=through?"skin-native-interval-covered-by-geometric-through-evidence":complete?
                "skin-native-interval-has-concave-material-retention-evidence":
                (span.EvidenceIndices.empty()?"skin-native-interval-has-no-proven-first-boundary-arrival":
                    "skin-native-interval-has-incomplete-boundary-classification");
            if(span.Kind==EFaceCutCoverageKind::Pending) {
                // An arrival's own unresolved classification is local proof.
                // A source's last mapper diagnostic may describe a different
                // (even microscopic) cell, so never present it as this cell's
                // cause when actual arrival evidence already exists here.
                std::set<std::string> localReasons;
                for(const auto index:span.EvidenceIndices)
                    if(!result.Evidence.at(index).bComplete)localReasons.insert(result.Evidence.at(index).Reason);
                for(const auto& localReason:localReasons)span.Reason+=";"+localReason;
                if(span.EvidenceIndices.empty()&&sourceInfo.contains(path.Key))
                    span.Reason+=";whole-source-summary-not-cell-local;"+sourceInfo.at(path.Key)->Reason;
            }
            result.Coverage.push_back(std::move(span));
        }
    }
}
