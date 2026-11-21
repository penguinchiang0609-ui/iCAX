// Included after Evidence.inc in the coverage Work's private namespace.
// Final source flags describe the exact exported proof domains. A mapper's
// conservative local return value remains diagnostic; it is not a second
// authority that can discard complete original-domain evidence.
namespace final_source_coverage
{
using Range=std::pair<double,double>;

bool DomainContains(const SMachiningPathEdgeAnalysis& path,std::uint64_t edgeId,
    double first,double last)
{
    if(!std::isfinite(first)||!std::isfinite(last)||!(last>first))return false;
    return std::any_of(path.SourceUses.begin(),path.SourceUses.end(),[&](const auto& use) {
        return use.EdgeId==edgeId&&use.FirstParameter<=first&&use.LastParameter>=last;
    });
}

bool ExactUnionCovers(double first,double last,std::vector<Range> ranges)
{
    if(!std::isfinite(first)||!std::isfinite(last)||!(last>first))return false;
    std::sort(ranges.begin(),ranges.end());double covered=first;
    for(const auto& [a,b]:ranges) {
        if(!std::isfinite(a)||!std::isfinite(b)||!(b>a))continue;
        const double lo=std::max(first,a),hi=std::min(last,b);
        if(!(hi>lo)||hi<=covered)continue;
        // An original positive gap, including one representable ulp, remains
        // a gap. Zero-length contacts never supply interval coverage.
        if(lo>covered)return false;
        covered=std::max(covered,hi);if(covered>=last)return true;
    }
    return covered>=last;
}

bool ClassificationAtom(const SMachiningFaceCutCoverageSpan& atom,
    const SMachiningPathEdgeAnalysis& path,const SMachiningFaceCutCoverageResult& result)
{
    if(atom.FaceId!=path.FaceId||atom.SourcePathKey!=path.Key||atom.SourceEdgeId!=atom.TargetEdgeId
        ||atom.Kind==EFaceCutCoverageKind::Pending||!atom.bMappingComplete||atom.EvidenceIndices.empty()
        ||!DomainContains(path,atom.SourceEdgeId,atom.FirstParameter,atom.LastParameter))return false;
    bool through=false,complete=true;
    for(const auto index:atom.EvidenceIndices) {
        if(index>=result.Evidence.size())return false;
        const auto& evidence=result.Evidence[index];
        if(evidence.FaceId!=path.FaceId||evidence.SourcePathKey!=path.Key
            ||evidence.SourceEdgeId!=atom.SourceEdgeId
            ||!DomainContains(path,evidence.SourceEdgeId,evidence.SourceFirstParameter,evidence.SourceLastParameter)
            ||evidence.SourceFirstParameter>atom.FirstParameter||evidence.SourceLastParameter<atom.LastParameter)return false;
        through=through||(evidence.bComplete&&evidence.bThrough);complete=complete&&evidence.bComplete;
    }
    return atom.Kind==EFaceCutCoverageKind::Through?through:
        atom.Kind==EFaceCutCoverageKind::Partial&&!through&&complete;
}

void AppendReason(std::string& reason,const std::string& token)
{
    if(reason.find(token)!=std::string::npos)return;
    if(!reason.empty())reason+=';';reason+=token;
}
}

void FinalizeSourceCoverageState(const SMachiningPathAnalysisResult& paths,
    SMachiningFaceCutCoverageResult& result,std::set<std::uint64_t>& upstreamIncomplete)
{
    using namespace final_source_coverage;
    std::map<std::string,const SMachiningPathEdgeAnalysis*> byKey;
    std::map<std::string,std::vector<const SMachiningFaceCutCoverageSpan*>> atoms;
    std::map<std::string,std::vector<const SMachiningFaceCutArrival*>> arrivals;
    for(const auto& path:paths.Edges)byKey.emplace(path.Key,&path);
    for(const auto& atom:result.Coverage)atoms[atom.SourcePathKey].push_back(&atom);
    for(const auto& arrival:result.Arrivals)arrivals[arrival.SourcePathKey].push_back(&arrival);
    // Keep upstream absence/invalid eligibility independent from a resolved
    // neighboring source. A complete local proof must not clear these faces.
    for(const auto& unresolved:paths.UnresolvedBoundaries)upstreamIncomplete.insert(unresolved.FaceId);
    std::set<std::string> sourceKeys;
    std::size_t classificationRestored=0,classificationDowngraded=0,correspondenceRestored=0,correspondenceDowngraded=0;
    for(auto& source:result.Sources) {
        sourceKeys.insert(source.SourcePathKey);
        const auto found=byKey.find(source.SourcePathKey);
        bool classificationComplete=found!=byKey.end(),correspondenceExpanded=classificationComplete;
        const auto* path=classificationComplete?found->second:nullptr;
        if(!path||path->FaceId!=source.FaceId||!path->bSelectedPath||!path->bOnSkinPath||path->SourceUses.empty())
            classificationComplete=correspondenceExpanded=false;
        const bool eligible=classificationComplete;
        if(path&&eligible)for(const auto& use:path->SourceUses) {
            if(!use.bSkinRelationValid||use.SkinRelation!=ESkinRelation::OnSkin
                ||!std::isfinite(use.FirstParameter)||!std::isfinite(use.LastParameter)
                ||!(use.LastParameter>use.FirstParameter)) {
                classificationComplete=correspondenceExpanded=false;break;
            }
            std::vector<Range> classified,expanded;
            for(const auto* atom:atoms[path->Key])if(atom->SourceEdgeId==use.EdgeId&&ClassificationAtom(*atom,*path,result))
                classified.emplace_back(atom->FirstParameter,atom->LastParameter);
            for(const auto* arrival:arrivals[path->Key]) {
                if(arrival->FaceId!=path->FaceId||arrival->SourceEdgeId!=use.EdgeId)continue;
                const double first=std::min(arrival->SourceFirstParameter,arrival->SourceLastParameter);
                const double last=std::max(arrival->SourceFirstParameter,arrival->SourceLastParameter);
                if(DomainContains(*path,arrival->SourceEdgeId,first,last))expanded.emplace_back(first,last);
            }
            classificationComplete=ExactUnionCovers(use.FirstParameter,use.LastParameter,std::move(classified))&&classificationComplete;
            correspondenceExpanded=ExactUnionCovers(use.FirstParameter,use.LastParameter,std::move(expanded))&&correspondenceExpanded;
        }
        if(classificationComplete!=source.bComplete) {
            if(classificationComplete)++classificationRestored;else ++classificationDowngraded;
            AppendReason(source.Reason,classificationComplete?"classification-complete-by-exact-final-native-source-coverage-union":
                "classification-incomplete-by-exact-final-native-source-coverage-union");
        }
        if(correspondenceExpanded!=source.bExitCorrespondenceExpanded) {
            if(correspondenceExpanded)++correspondenceRestored;else ++correspondenceDowngraded;
            AppendReason(source.Reason,correspondenceExpanded?"exit-correspondence-expanded-by-exact-final-native-source-arrival-union":
                "exit-correspondence-not-expanded-by-exact-final-native-source-arrival-union");
        }
        source.bComplete=classificationComplete;source.bExitCorrespondenceExpanded=correspondenceExpanded;
        if(!classificationComplete)upstreamIncomplete.insert(source.FaceId);
    }
    for(const auto& path:paths.Edges)if(path.bSelectedPath&&!sourceKeys.contains(path.Key)) {
        upstreamIncomplete.insert(path.FaceId);
        result.Diagnostics.push_back("final-source-state-missing-selected-source-record:"+path.Key);
    }
    if(classificationRestored||classificationDowngraded||correspondenceRestored||correspondenceDowngraded)
        result.Diagnostics.push_back("final-source-state-from-exact-native-unions;classification-restored="+std::to_string(classificationRestored)
            +";classification-downgraded="+std::to_string(classificationDowngraded)
            +";correspondence-restored="+std::to_string(correspondenceRestored)
            +";correspondence-downgraded="+std::to_string(correspondenceDowngraded)+";mapper-diagnostics-retained");
}
