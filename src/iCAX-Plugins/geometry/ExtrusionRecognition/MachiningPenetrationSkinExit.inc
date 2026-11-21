        void AddCertifiedSkinExits(SMachiningPenetrationResult& result,
            const O::SOpenCascadeBRepBuildResult& built, const SSectionFaceClassificationResult& classification,
            const SMachiningEdgeAnalysisResult& edgeAnalysis, const SMachiningPathAnalysisResult& paths)
        {
            if (std::ranges::none_of(result.Spans, [](const auto& span) { return span.Kind == EPenetrationKind::Pending; }))
                return;
            const auto coverage = MachiningFaceCutCoverageWork{}.Run(classification, edgeAnalysis, paths);
            if (!coverage.bSuccess)
                return;

            std::map<std::string_view, const SMachiningEdgeAnalysis*> attributes;
            for (const auto& edge : edgeAnalysis.Edges)
                attributes.emplace(edge.Key, &edge);
            std::map<std::string_view, bool> trusted;
            const auto stockSkin = [&](std::string_view key, std::uint64_t edgeId, double first, double last)
                -> const SMachiningEdgeAnalysis*
            {
                const auto found = attributes.find(key);
                if (found == attributes.end())
                    return nullptr;
                const auto& attr = *found->second;
                if (attr.EdgeId != edgeId || attr.FirstParameter > first || attr.LastParameter < last ||
                    !attr.bSourceSkinMembership || !attr.bSkinRelationValid || attr.SkinRelation != ESkinRelation::OnSkin)
                    return nullptr;
                auto [saved, inserted] = trusted.try_emplace(key, false);
                if (inserted)
                {
                    saved->second = attr.SkinReason.starts_with("source-skin-native-bspline-whole-domain-certificate;");
                    const auto edge = built.Edges.find(edgeId);
                    if (!saved->second && edge != built.Edges.end())
                        for (const auto skinId : attr.SourceSkinFaceIds)
                        {
                            const auto face = built.Faces.find(skinId);
                            if (face == built.Faces.end())
                                continue;
                            for (TopExp_Explorer coedge(face->second, TopAbs_EDGE); coedge.More(); coedge.Next())
                                if (coedge.Current().IsSame(edge->second))
                                {
                                    saved->second = true;
                                    break;
                                }
                            if (saved->second)
                                break;
                        }
                }
                return saved->second ? &attr : nullptr;
            };

            struct Proof
            {
                const SMachiningFaceCutEvidence* Evidence;
                std::vector<std::uint64_t> SkinFaces;
            };
            using Source = std::tuple<std::uint64_t, std::string_view, std::uint64_t>;
            std::map<Source, std::vector<Proof>> proofs;
            std::map<std::string_view, const SMachiningPathEdgeAnalysis*> pathByKey;
            for (const auto& path : paths.Edges)
                pathByKey.emplace(path.Key, &path);
            for (const auto& evidence : coverage.Evidence)
            {
                if (!evidence.bThrough || !evidence.bComplete ||
                    (evidence.Kind != EFaceCutEvidenceKind::SkinExit &&
                        evidence.Kind != EFaceCutEvidenceKind::SkinExitBoundarySet) ||
                    !std::isfinite(evidence.SourceFirstParameter) || !std::isfinite(evidence.SourceLastParameter) ||
                    evidence.SourceLastParameter <= evidence.SourceFirstParameter)
                    continue;
                const auto path = pathByKey.find(evidence.SourcePathKey);
                if (path == pathByKey.end() || path->second->FaceId != evidence.FaceId ||
                    !std::ranges::any_of(path->second->SourceUses, [&](const auto& use)
                    {
                        return stockSkin(use.SpanKey, evidence.SourceEdgeId,
                            evidence.SourceFirstParameter, evidence.SourceLastParameter);
                    }))
                    continue;
                std::set<std::uint64_t> skinFaces;
                const auto witness = [&](std::string_view key, std::uint64_t edgeId, double first, double last)
                {
                    const auto* attr = stockSkin(key, edgeId, first, last);
                    if (!attr)
                        return false;
                    skinFaces.insert(attr->SourceSkinFaceIds.begin(), attr->SourceSkinFaceIds.end());
                    return true;
                };
                const bool valid = evidence.Kind == EFaceCutEvidenceKind::SkinExit
                    ? witness(evidence.WitnessSpanKey, evidence.WitnessEdgeId,
                        evidence.WitnessFirstParameter, evidence.WitnessLastParameter)
                    : !evidence.BoundaryWitnesses.empty() &&
                        std::ranges::all_of(evidence.BoundaryWitnesses, [&](const auto& boundary)
                        {
                            return witness(boundary.SpanKey, boundary.EdgeId, boundary.FirstParameter, boundary.LastParameter);
                        });
                if (valid)
                    proofs[{evidence.FaceId, evidence.SourcePathKey, evidence.SourceEdgeId}].push_back(
                        {&evidence, {skinFaces.begin(), skinFaces.end()}});
            }

            std::vector<SMachiningPenetrationSpan> resolved;
            for (const auto& span : result.Spans)
            {
                const auto found = proofs.find({span.FaceId, span.PathKey, span.EdgeId});
                if (span.Kind != EPenetrationKind::Pending || found == proofs.end())
                {
                    resolved.push_back(span);
                    continue;
                }
                std::vector<double> events{span.FirstParameter, span.LastParameter};
                for (const auto& proof : found->second)
                    for (const auto event : {proof.Evidence->SourceFirstParameter, proof.Evidence->SourceLastParameter})
                        if (span.FirstParameter < event && event < span.LastParameter)
                            events.push_back(event);
                std::ranges::sort(events);
                events.erase(std::unique(events.begin(), events.end()), events.end());
                for (std::size_t i = 1; i < events.size(); ++i)
                {
                    auto part = span;
                    part.FirstParameter = events[i - 1];
                    part.LastParameter = events[i];
                    const auto proof = std::ranges::find_if(found->second, [&](const auto& candidate)
                    {
                        return candidate.Evidence->SourceFirstParameter <= part.FirstParameter &&
                            candidate.Evidence->SourceLastParameter >= part.LastParameter;
                    });
                    if (proof != found->second.end())
                    {
                        part.Kind = EPenetrationKind::Through;
                        part.Exit = EPenetrationExit::MachiningFaceSkin;
                        part.ExitWitnessEdgeId = proof->Evidence->WitnessEdgeId;
                        part.ExitSupportFaceIds = proof->SkinFaces;
                        part.FirstExitDepthMm.reset();
                        part.LastExitDepthMm.reset();
                        part.FirstRetainedDepthMm.reset();
                        part.LastRetainedDepthMm.reset();
                        part.Reason = proof->Evidence->Kind == EFaceCutEvidenceKind::SkinExit
                            ? "first-machining-face-boundary-is-source-stock-skin"
                            : "entire-machining-face-boundary-is-source-stock-skin";
                    }
                    else
                    {
                        part.FirstExitDepthMm = DepthAt(span.FirstExitDepthMm, span.LastExitDepthMm,
                            span.FirstParameter, span.LastParameter, part.FirstParameter);
                        part.LastExitDepthMm = DepthAt(span.FirstExitDepthMm, span.LastExitDepthMm,
                            span.FirstParameter, span.LastParameter, part.LastParameter);
                    }
                    resolved.push_back(std::move(part));
                }
            }
            result.Spans = std::move(resolved);
        }
