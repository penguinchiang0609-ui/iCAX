#include "pch.h"
#include "MachiningFaceCutCoverageWork.h"
#include "MachiningPenetrationWork.h"
#include "MachiningPenetrationResidual.h"
#include "MachiningPenetrationStock.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopTools_ShapeMapHasher.hxx>

#include <numbers>
#include <numeric>

namespace iCAX::ExtrusionRecognition
{
    namespace
    {
        namespace O = iCAX::OpenCascade;
        using penetration_detail::RetainedSpan;
        using penetration_detail::StockExitSpan;
        constexpr double GeometryTolerance = 10 * Precision::Confusion();

        SMachiningPenetrationSpan SourceSpan(const SMachiningPathEdgeAnalysis& path,
            const SMachiningPathSourceUse& use)
        {
            SMachiningPenetrationSpan span;
            span.FaceId = path.FaceId;
            span.EdgeId = use.EdgeId;
            span.PathKey = path.Key;
            span.FirstParameter = use.FirstParameter;
            span.LastParameter = use.LastParameter;
            return span;
        }

        std::optional<gp_Dir> FixedAxis(const SMachiningPathEdgeAnalysis& path)
        {
            if (path.Status != EMachiningPathStatus::Ready || path.Samples.size() != 2 ||
                !path.Samples[0].bToolAxisValid || !path.Samples[1].bToolAxisValid)
                return std::nullopt;
            const auto toVector = [](const auto& axis) { return gp_Vec(axis.X, axis.Y, axis.Z); };
            const auto first = toVector(path.Samples.front().ToolAxis);
            const auto last = toVector(path.Samples.back().ToolAxis);
            if (first.SquareMagnitude() <= Precision::SquareConfusion() ||
                last.SquareMagnitude() <= Precision::SquareConfusion())
                return std::nullopt;
            const gp_Dir direction(first);
            if (!direction.IsEqual(gp_Dir(last), Precision::Angular()))
                return std::nullopt;
            return direction;
        }

        template<typename Span>
        const Span* Covering(const std::vector<Span>& spans, double parameter)
        {
            const auto found = std::ranges::find_if(spans, [&](const auto& span)
            {
                return span.First <= parameter && parameter <= span.Last;
            });
            return found == spans.end() ? nullptr : &*found;
        }

        std::optional<double> DepthAt(const std::optional<double>& first, const std::optional<double>& last,
            double low, double high, double parameter)
        {
            if (!first || !last || high <= low)
                return std::nullopt;
            return std::lerp(*first, *last, std::clamp((parameter - low) / (high - low), 0.0, 1.0));
        }

        std::vector<SMachiningPenetrationSpan> AssessLine(SMachiningPenetrationSpan source,
            const std::vector<StockExitSpan>& stock, const std::vector<RetainedSpan>& retained,
            const TopoDS_Face& machining, const gp_Pnt& start, const gp_Pnt& end, const gp_Dir& axis,
            const std::function<const TopoDS_Face&()>& nominalSection)
        {
            std::vector<double> boundaries{0, 1};
            const auto appendBoundaries = [&](const auto& spans)
            {
                for (const auto& span : spans)
                {
                    boundaries.push_back(span.First);
                    boundaries.push_back(span.Last);
                }
            };
            appendBoundaries(stock);
            appendBoundaries(retained);
            std::ranges::sort(boundaries);
            constexpr double eventRoundoff = 32 * std::numeric_limits<double>::epsilon();
            boundaries.erase(std::unique(boundaries.begin(), boundaries.end(), [](double a, double b) {
                return b - a <= eventRoundoff;
            }), boundaries.end());
            boundaries.back() = 1;
            const gp_Vec along(start, end);
            std::vector<SMachiningPenetrationSpan> result;
            for (std::size_t i = 1; i < boundaries.size(); ++i)
            {
                const double a = boundaries[i - 1], b = boundaries[i];
                if (b <= a)
                    continue;
                const double witness = std::midpoint(a, b);
                const auto* exit = Covering(stock, witness);
                const auto* stop = Covering(retained, witness);
                const auto exitAt = [&](double p)
                {
                    return exit ? DepthAt(exit->FirstDepth, exit->LastDepth, exit->First, exit->Last, p)
                        : std::nullopt;
                };
                const auto retainedAt = [&](double p)
                {
                    return stop && stop->Proven
                        ? DepthAt(stop->FirstDepth, stop->LastDepth, stop->First, stop->Last, p)
                        : std::nullopt;
                };
                const auto emit = [&](double first, double last)
                {
                    auto record = source;
                    record.FirstParameter = std::lerp(source.FirstParameter, source.LastParameter, first);
                    record.LastParameter = std::lerp(source.FirstParameter, source.LastParameter, last);
                    if (record.LastParameter <= record.FirstParameter)
                        return;
                    record.FirstExitDepthMm = exitAt(first);
                    record.LastExitDepthMm = exitAt(last);
                    if (!record.FirstExitDepthMm || !record.LastExitDepthMm)
                    {
                        record.Reason = exit ? exit->Reason : "stock-exit-unproved";
                        if (exit && exit->Reason == "stock-curved-entry-or-exit-unproved" &&
                            penetration_detail::ProjectedStockStripIsCovered(nominalSection(), machining,
                                start.Translated(along * first), start.Translated(along * last), axis,
                                GeometryTolerance))
                        {
                            record.Kind = EPenetrationKind::Through;
                            record.Exit = EPenetrationExit::StockSkin;
                            record.Reason = "machining-face-covers-projected-stock-strip";
                        }
                    }
                    else
                    {
                        record.Exit = exit->Continued ? EPenetrationExit::ContinuedStockSkin : EPenetrationExit::StockSkin;
                        record.ExitSupportFaceIds = exit->SupportFaceIds;
                        record.FirstRetainedDepthMm = retainedAt(first);
                        record.LastRetainedDepthMm = retainedAt(last);
                        if (!stop || !stop->Proven ||
                            (stop->FirstDepth.has_value() != stop->LastDepth.has_value()))
                        {
                            record.Reason = stop ? stop->Reason : "retained-material-unproved";
                            if (penetration_detail::MachiningFaceCoversStrip(machining,
                                start.Translated(along * first), start.Translated(along * last), axis,
                                *record.FirstExitDepthMm, *record.LastExitDepthMm, GeometryTolerance))
                            {
                                record.Kind = EPenetrationKind::Through;
                                record.Reason = "machining-face-covers-stock-exit-strip";
                            }
                        }
                        else
                        {
                            const auto material = retainedAt(std::midpoint(first, last));
                            if (material && *material < *exitAt(std::midpoint(first, last)) - GeometryTolerance)
                            {
                                record.Kind = EPenetrationKind::Blind;
                                record.BlockingFaceId = stop->BlockingFaceId;
                                record.Reason = "retained-material-before-wall-exit";
                            }
                            else
                            {
                                record.Kind = EPenetrationKind::Through;
                                record.Reason = "material-cleared-to-wall-exit";
                            }
                        }
                    }
                    result.push_back(std::move(record));
                };
                const auto exitA = exitAt(a), exitB = exitAt(b);
                const auto stopA = retainedAt(a), stopB = retainedAt(b);
                if (exitA && exitB && stopA && stopB)
                {
                    const double deltaA = *stopA - *exitA + GeometryTolerance;
                    const double deltaB = *stopB - *exitB + GeometryTolerance;
                    if ((deltaA < 0 && deltaB > 0) || (deltaA > 0 && deltaB < 0))
                    {
                        const auto split = std::lerp(a, b, deltaA / (deltaA - deltaB));
                        emit(a, split);
                        emit(split, b);
                        continue;
                    }
                }
                emit(a, b);
            }
            return result;
        }

        std::vector<TopoDS_Edge> PhysicalEdges(const TopoDS_Face& face)
        {
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> identities;
            std::vector<TopoDS_Edge> edges;
            for (TopExp_Explorer item(face, TopAbs_EDGE); item.More(); item.Next())
            {
                const auto edge = TopoDS::Edge(item.Current());
                if (BRep_Tool::Degenerated(edge) || BRep_Tool::IsClosed(edge, face) || identities.Contains(edge))
                    continue;
                identities.Add(edge);
                edges.push_back(edge);
            }
            return edges;
        }

#include "MachiningPenetrationBoreRims.inc"

        bool IsVerifiedSolid(const TopoDS_Shape& shape)
        {
            TopExp_Explorer solids(shape, TopAbs_SOLID);
            if (!solids.More())
                return false;
            const auto solid = solids.Current();
            solids.Next();
            if (solids.More())
                return false;
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> shapeFaces, solidFaces;
            TopExp::MapShapes(shape, TopAbs_FACE, shapeFaces);
            TopExp::MapShapes(solid, TopAbs_FACE, solidFaces);
            if (shapeFaces.Extent() != solidFaces.Extent() || !BRepCheck_Analyzer(solid).IsValid())
                return false;
            for (TopExp_Explorer shell(solid, TopAbs_SHELL); shell.More(); shell.Next())
                if (!BRep_Tool::IsClosed(shell.Current()))
                    return false;
            BRepClass3d_SolidClassifier orientation(solid);
            orientation.PerformInfinitePoint(GeometryTolerance);
            return orientation.State() == TopAbs_OUT;
        }

        bool CoversWholeSection(const TopoDS_Face& face, const gp_Pln& plane,
            const TopoDS_Face& stockSection)
        {
            if (stockSection.IsNull())
                return false;
            gp_Trsf translation;
            translation.SetTranslation(gp_Vec(plane.Location().X(), 0, 0));
            const auto stockAtFace = stockSection.Moved(TopLoc_Location(translation));
            Bnd_Box bounds;
            BRepBndLib::AddOptimal(face, bounds, false, true);
            if (bounds.IsVoid() || bounds.IsOpen())
                return false;
            Bnd_Box stockBounds;
            BRepBndLib::AddOptimal(stockAtFace, stockBounds, false, false);
            bounds.Enlarge(GeometryTolerance);
            if (stockBounds.IsVoid() || stockBounds.IsOpen() ||
                bounds.IsOut(stockBounds.CornerMin()) || bounds.IsOut(stockBounds.CornerMax()))
                return false;

            BRepAlgoAPI_Cut difference;
            NCollection_List<TopoDS_Shape> arguments, tools;
            arguments.Append(stockAtFace);
            tools.Append(face);
            difference.SetArguments(arguments);
            difference.SetTools(tools);
            difference.SetNonDestructive(true);
            difference.Build();
            return difference.IsDone() && !difference.HasErrors() && !difference.HasWarnings() &&
                !TopExp_Explorer(difference.Shape(), TopAbs_FACE).More();
        }

        std::uint64_t FlatFloor(const O::SOpenCascadeBRepBuildResult& built,
            const BoreRim& rim, const gp_Dir& direction)
        {
            std::uint64_t floor = 0;
            for (const auto& [id, face] : built.Faces)
            {
                const BRepAdaptor_Surface surface(face);
                if (surface.GetType() != GeomAbs_Plane ||
                    !surface.Plane().Axis().Direction().IsParallel(direction, Precision::Angular()))
                    continue;
                const auto edges = PhysicalEdges(face);
                if (edges.size() == rim.Edges.size() && std::ranges::all_of(rim.Edges, [&](const BoreRimEdge& item)
                {
                    return std::ranges::any_of(edges, [&](const TopoDS_Edge& edge) { return edge.IsSame(item.Edge); });
                }))
                {
                    if (floor)
                        return 0;
                    floor = id;
                }
            }
            return floor;
        }

        bool IsSkinRimEdge(const O::SOpenCascadeBRepBuildResult& built, const SMachiningPathAnalysisResult& paths,
            const BoreRimEdge& rim)
        {
            std::vector<std::pair<double, double>> intervals;
            for (const auto& path : paths.Edges)
                if (path.FaceId == rim.FaceId && path.bOnSkinPath)
                    for (const auto& source : path.SourceUses)
                    {
                        if (!source.bSkinRelationValid || source.SkinRelation != ESkinRelation::OnSkin ||
                            !std::isfinite(source.FirstParameter) || !std::isfinite(source.LastParameter) ||
                            source.LastParameter <= source.FirstParameter)
                            continue;
                        const auto edge = built.Edges.find(source.EdgeId);
                        if (edge != built.Edges.end() && edge->second.IsSame(rim.Edge))
                            intervals.emplace_back(source.FirstParameter, source.LastParameter);
                    }
            if (intervals.empty())
                return false;
            const BRepAdaptor_Curve curve(rim.Edge);
            const double tolerance = GeometryTolerance / curve.Circle().Radius();
            std::ranges::sort(intervals);
            double covered = curve.FirstParameter();
            for (const auto& [first, last] : intervals)
            {
                if (first > covered + tolerance)
                    return false;
                covered = std::max(covered, last);
            }
            return covered >= curve.LastParameter() - tolerance;
        }

        bool IsSkinOpening(const O::SOpenCascadeBRepBuildResult& built, const SMachiningPathAnalysisResult& paths,
            const BoreRim& rim)
        {
            return std::ranges::all_of(rim.Edges, [&](const BoreRimEdge& edge) { return IsSkinRimEdge(built, paths, edge); });
        }

        SMachiningPenetrationSpan AssessBore(SMachiningPenetrationSpan record,
            const O::SOpenCascadeBRepBuildResult& built, const SSectionWiresResult& section,
            const SMachiningPathAnalysisResult& paths, const TopoDS_Face& face,
            const TopoDS_Edge& source, const gp_Dir& direction, bool validSolid)
        {
            record.Reason = "cylindrical-bore-proof-unavailable";
            const auto cylinder = BRepAdaptor_Surface(face).Cylinder();
            if (!validSolid || cylinder.Radius() <= GeometryTolerance ||
                std::abs(direction.X()) > Precision::Angular() ||
                !direction.IsParallel(cylinder.Axis().Direction(), Precision::Angular()))
                return record;
            const auto rims = FindBoreRims(built, record.FaceId, cylinder);
            if (rims.empty())
                return record;
            const auto entry = std::ranges::find_if(rims, [&](const BoreRim& rim)
            {
                return std::ranges::any_of(rim.Edges, [&](const BoreRimEdge& edge)
                {
                    return edge.FaceId == record.FaceId && edge.Edge.IsSame(source);
                });
            });
            if (entry == rims.end() || !IsSkinOpening(built, paths, *entry))
                return record;
            const auto& target = entry == rims.begin() ? rims.back() : rims.front();
            const double depth = gp_Vec(entry->Center, target.Center).Dot(gp_Vec(direction));
            if (depth <= GeometryTolerance)
                return record;
            // The stock is X-extruded. A transverse bore projects its full
            // circular mouth to this chord, including every generator.
            const gp_Vec across(gp_Dir(gp_Vec(1, 0, 0).Crossed(gp_Vec(direction))));
            const gp_Vec radius = across * cylinder.Radius();
            const auto stock = penetration_detail::ResolveStockExit(section, entry->Center.Translated(-radius),
                entry->Center.Translated(radius), direction, GeometryTolerance);
            double minimum = std::numeric_limits<double>::infinity(), maximum = 0;
            double covered = 0;
            for (const auto& span : stock)
            {
                if (span.First != covered || !span.FirstDepth || !span.LastDepth || span.Continued)
                    return record;
                minimum = std::min({minimum, *span.FirstDepth, *span.LastDepth});
                maximum = std::max({maximum, *span.FirstDepth, *span.LastDepth});
                for (const auto id : span.SupportFaceIds)
                    if (std::ranges::find(record.ExitSupportFaceIds, id) == record.ExitSupportFaceIds.end())
                        record.ExitSupportFaceIds.push_back(id);
                covered = span.Last;
            }
            if (covered != 1)
                return record;
            record.Exit = EPenetrationExit::StockSkin;
            const bool leavesWall = std::abs(minimum - depth) <= GeometryTolerance &&
                std::abs(maximum - depth) <= GeometryTolerance;
            const auto floor = !leavesWall && depth < minimum - GeometryTolerance
                ? FlatFloor(built, target, direction) : 0;
            if ((!leavesWall && !floor) || (leavesWall && !IsSkinOpening(built, paths, target)))
                return record;

            BRepPrimAPI_MakeCylinder cavity(gp_Ax2(entry->Center, direction), cylinder.Radius(), depth);
            BRepAlgoAPI_Common overlap(built.Shape, cavity.Shape());
            if (!overlap.IsDone() || overlap.HasErrors())
                return record;
            GProp_GProps retained;
            BRepGProp::VolumeProperties(overlap.Shape(), retained);
            if (std::abs(retained.Mass()) > std::numbers::pi * cylinder.Radius() * cylinder.Radius() * GeometryTolerance)
            {
                record.Reason = "retained-material-in-cylindrical-bore";
                return record;
            }
            if (maximum - minimum <= GeometryTolerance)
                record.FirstExitDepthMm = record.LastExitDepthMm = minimum;
            if (leavesWall)
            {
                record.Kind = EPenetrationKind::Through;
                record.Reason = "coaxial-skin-openings-and-cleared-bore";
            }
            else
            {
                record.Kind = EPenetrationKind::Blind;
                record.BlockingFaceId = floor;
                record.FirstRetainedDepthMm = record.LastRetainedDepthMm = depth;
                record.Reason = "closed-bore-floor-before-wall-exit";
            }
            return record;
        }
#include "MachiningPenetrationSkinExit.inc"
    }

    SMachiningPenetrationResult MachiningPenetrationWork::Run(
        const SSectionFaceClassificationResult& classification, const SSectionWiresResult& section,
        const SMachiningEdgeAnalysisResult& edgeAnalysis, const SMachiningPathAnalysisResult& pathAnalysis) const
    {
        SMachiningPenetrationResult result;
        if (!classification.bSuccess || !section.bSuccess || !edgeAnalysis.bSuccess || !pathAnalysis.bSuccess)
        {
            result.Diagnostics.emplace_back("Successful Classify, section contour, edge analysis and path analysis are required");
            return result;
        }
        const auto built = O::ReconstructOpenCascadeShape(classification.SplitGeometry);
        if (!built.bOK || built.Shape.IsNull())
        {
            result.Diagnostics.emplace_back("Cannot reconstruct the classified finished model");
            return result;
        }
        std::optional<bool> verifiedSolid;
        std::optional<TopoDS_Face> stockSection;
        std::map<std::uint64_t, bool> wholeSectionFaces;
        std::optional<penetration_detail::RetainedMaterialQuery> retainedQuery;
        const auto nominalSection = [&]() -> const TopoDS_Face&
        {
            if (!stockSection)
                stockSection = toolpath_detail::BuildStockSectionFace(section, GeometryTolerance);
            return *stockSection;
        };
        const auto isVerifiedSolid = [&]
        {
            if (!verifiedSolid)
                verifiedSolid = IsVerifiedSolid(built.Shape);
            return *verifiedSolid;
        };
        for (const auto& path : pathAnalysis.Edges)
        {
            if (!path.bSelectedPath || !path.bOnSkinPath)
                continue;
            for (const auto& use : path.SourceUses)
            {
                auto record = SourceSpan(path, use);
                try
                {
                    const auto face = built.Faces.find(path.FaceId);
                    const auto edge = built.Edges.find(use.EdgeId);
                    const auto axis = FixedAxis(path);
                    if (face == built.Faces.end() || edge == built.Edges.end() ||
                        !use.bSkinRelationValid || use.SkinRelation != ESkinRelation::OnSkin ||
                        !std::isfinite(use.FirstParameter) || !std::isfinite(use.LastParameter) ||
                        use.LastParameter <= use.FirstParameter)
                    {
                        record.Reason = "entry-or-fixed-geometric-axis-unproved";
                    }
                    else
                    {
                        const BRepAdaptor_Surface surface(face->second);
                        const BRepAdaptor_Curve curve(edge->second);
                        // A closed finished solid and a machining face covering
                        // the complete stock section also prove wall removal
                        // at a transverse end, including curved skin paths.
                        bool wholeSection = false;
                        if (surface.GetType() == GeomAbs_Plane &&
                            surface.Plane().Axis().Direction().IsParallel(gp_Dir(1, 0, 0), Precision::Angular()) &&
                            isVerifiedSolid())
                        {
                            auto [cached, inserted] = wholeSectionFaces.try_emplace(path.FaceId, false);
                            if (inserted)
                            {
                                cached->second = CoversWholeSection(face->second, surface.Plane(), nominalSection());
                            }
                            wholeSection = cached->second;
                        }
                        if (wholeSection)
                        {
                            record.Kind = EPenetrationKind::Through;
                            record.Exit = EPenetrationExit::FullSectionTermination;
                            record.Reason = "machining-face-covers-entire-stock-section";
                        }
                        else if (!axis)
                            record.Reason = "entry-or-fixed-geometric-axis-unproved";
                        else if (surface.GetType() == GeomAbs_Cylinder && curve.GetType() == GeomAbs_Circle)
                        {
                            record = AssessBore(record, built, section, pathAnalysis, face->second, edge->second,
                                *axis, isVerifiedSolid());
                        }
                        else if (surface.GetType() == GeomAbs_Plane && curve.GetType() == GeomAbs_Line &&
                            axis->IsNormal(curve.Line().Direction(), Precision::Angular()) &&
                            axis->IsNormal(surface.Plane().Axis().Direction(), Precision::Angular()))
                        {
                            const auto start = curve.Value(use.FirstParameter), end = curve.Value(use.LastParameter);
                            const auto stock = penetration_detail::ResolveStockExit(section, start, end, *axis, GeometryTolerance);
                            double maximumDepth = 0;
                            for (const auto& span : stock)
                                if (span.FirstDepth && span.LastDepth)
                                    maximumDepth = std::max({maximumDepth, *span.FirstDepth, *span.LastDepth});
                            std::vector<RetainedSpan> retained;
                            if (maximumDepth > 0)
                            {
                                if (!retainedQuery)
                                    retainedQuery.emplace(built, classification);
                                retained = retainedQuery->Resolve(path.FaceId, start, end, *axis,
                                    GeometryTolerance, maximumDepth);
                            }
                            auto spans = AssessLine(record, stock, retained, face->second, start, end, *axis, nominalSection);
                            result.Spans.insert(result.Spans.end(), std::make_move_iterator(spans.begin()),
                                std::make_move_iterator(spans.end()));
                            continue;
                        }
                        else
                            record.Reason = "source-curve-or-swept-face-outside-certified-geometry";
                    }
                }
                catch (const Standard_Failure& error)
                {
                    record.Reason = error.GetMessageString() ? error.GetMessageString() : "native-geometry-query-failed";
                }
                catch (const std::exception& error)
                {
                    record.Reason = error.what();
                }
                result.Spans.push_back(std::move(record));
            }
        }
        AddCertifiedSkinExits(result, built, classification, edgeAnalysis, pathAnalysis);
        for (const auto& wire : pathAnalysis.UnresolvedBoundaries)
            result.Diagnostics.push_back("Unresolved path wire on machining face " + std::to_string(wire.FaceId));
        result.bSuccess = true;
        result.bComplete = pathAnalysis.bComplete && result.Diagnostics.empty() && std::ranges::none_of(result.Spans,
            [](const auto& span) { return span.Kind == EPenetrationKind::Pending; });
        return result;
    }
}
