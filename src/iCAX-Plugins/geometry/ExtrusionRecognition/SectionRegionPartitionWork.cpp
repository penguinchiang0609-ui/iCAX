#include "pch.h"
#include "SectionContourFitting.h"
#include "SectionRegionPartitionWork.h"
#include "SectionRegionPartitionDetail.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2dConvert.hxx>
#include <Geom2dConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GeomAPI.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_List.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ2d.hxx>
#include <gp_Elips2d.hxx>
#include <gp_Pln.hxx>

#include <boost/polygon/segment_data.hpp>
#include <boost/polygon/voronoi.hpp>
#include <numbers>
#include <numeric>
#include <ranges>

namespace iCAX::ExtrusionRecognition
{
    namespace
    {
        namespace detail = section_partition_detail;
        namespace occ = iCAX::OpenCascade;

        SSectionRegionPartitionResult Outcome(ESectionRegionPartitionStatus status, std::string reason)
        {
            SSectionRegionPartitionResult result;
            result.Status = status;
            result.Reason = std::move(reason);
            return result;
        }

        detail::Point ToPoint(const gp_Pnt2d& point)
        {
            return { point.X(), point.Y() };
        }

        std::optional<detail::Curve> AnalyticCurve(
            const Handle(Geom2d_Curve)& curve, double first, double last, bool reversed)
        {
            const Geom2dAdaptor_Curve adaptor(curve, first, last);
            const auto type = adaptor.GetType();
            if (type != GeomAbs_Line && type != GeomAbs_Circle)
                return std::nullopt;
            const auto start = reversed ? last : first;
            const auto end = reversed ? first : last;
            detail::Curve result;
            result.Start = ToPoint(curve->Value(start));
            result.End = ToPoint(curve->Value(end));
            if (type == GeomAbs_Line)
                return result;
            const gp_Circ2d circle = adaptor.Circle();
            gp_Pnt2d p;
            gp_Vec2d tangent;
            curve->D1(start, p, tangent);
            if (reversed)
                tangent.Reverse();
            const gp_Vec2d radial(circle.Location(), p);
            result.IsArc = true;
            result.Center = ToPoint(circle.Location());
            result.Radius = circle.Radius();
            result.StartAngle = std::atan2(radial.Y(), radial.X());
            result.Sweep = std::copysign(std::abs(end - start), radial.Crossed(tangent));
            return result;
        }

#include "SectionRegionCurvedWall.inc"
#include "SectionRegionCurvedOpenWall.inc"
#include "SectionRegionCurvedBranch.inc"

        struct AnalyticUse
        {
            detail::Curve Support;
            std::vector<TopoDS_Edge> NativeEdges;
            std::vector<std::uint64_t> SourceIds;
            bool Exact = false;
        };

        struct ClosedConic
        {
            gp_Elips2d Ellipse;
            bool IsCircular = true;
        };

        std::optional<ClosedConic> ReadClosedConic(const SSectionWire& wire, double tolerance)
        {
            std::optional<ClosedConic> result;
            double turn = 0;
            for (const auto& edge : wire.Edges)
            {
                const auto curve = occ::BuildOpenCascadeCurve2(edge.Curve);
                const Geom2dAdaptor_Curve trim(curve, edge.Range.First, edge.Range.Last);
                gp_Elips2d ellipse;
                const bool circular = trim.GetType() == GeomAbs_Circle;
                if (circular)
                {
                    const auto circle = trim.Circle();
                    ellipse = gp_Elips2d(circle.Position(), circle.Radius(), circle.Radius());
                }
                else if (trim.GetType() == GeomAbs_Ellipse)
                    ellipse = trim.Ellipse();
                else
                    return std::nullopt;

                if (ellipse.MinorRadius() <= tolerance)
                    return std::nullopt;
                if (result)
                {
                    const auto& first = result->Ellipse;
                    if (ellipse.Location().Distance(first.Location()) > tolerance ||
                        std::abs(ellipse.MajorRadius() - first.MajorRadius()) > tolerance ||
                        std::abs(ellipse.MinorRadius() - first.MinorRadius()) > tolerance ||
                        (!result->IsCircular && !circular &&
                            std::abs(ellipse.XAxis().Direction().Crossed(first.XAxis().Direction())) *
                                ellipse.MajorRadius() > tolerance))
                        return std::nullopt;
                }
                else
                    result = ClosedConic{ellipse, circular};

                gp_Pnt2d point;
                gp_Vec2d tangent;
                curve->D1(edge.Range.First, point, tangent);
                const double sense = gp_Vec2d(ellipse.Location(), point).Crossed(tangent) *
                    (edge.bReversed ? -1 : 1);
                if (sense == 0 || (turn != 0 && std::signbit(turn) != std::signbit(sense)))
                    return std::nullopt;
                turn += std::copysign(edge.Range.Last - edge.Range.First, sense);
            }
            if (!result || std::abs(std::abs(turn) - 2 * std::numbers::pi) * result->Ellipse.MajorRadius() > tolerance)
                return std::nullopt;
            return result;
        }

        bool HasContinuousConicWall(const ClosedConic& outer, const ClosedConic& inner, double tolerance)
        {
            const auto& a = outer.Ellipse;
            const auto& b = inner.Ellipse;
            if (a.Location().Distance(b.Location()) > tolerance ||
                (!outer.IsCircular && !inner.IsCircular &&
                    std::abs(a.XAxis().Direction().Crossed(b.XAxis().Direction())) * a.MajorRadius() > tolerance))
                return false;

            const double major = a.MajorRadius(), minor = a.MinorRadius();
            const double innerMajor = b.MajorRadius(), innerMinor = b.MinorRadius();
            if (major - innerMajor <= tolerance || minor - innerMinor <= tolerance)
                return false;
            // In the common principal frame, an outer normal at (x,y)
            // reaches (x*(1-s/major^2), y*(1-s/minor^2)). One s
            // satisfying both open ranges places every such ray inside
            // the inner ellipse, so all first exits belong to the same ring.
            const double insetMajor = innerMajor - 3 * tolerance;
            const double insetMinor = innerMinor - 3 * tolerance;
            if (insetMinor <= 0)
                return false;
            return std::max(major * (major - insetMajor), minor * (minor - insetMinor)) <
                std::min(major * (major + insetMajor), minor * (minor + insetMinor));
        }

        bool HasConvexSolidBoundary(const detail::Ring& ring, double tolerance)
        {
            if (ring.IsInner || ring.Curves.empty())
                return false;
            const auto point = [](const detail::Point& p) { return gp_Pnt2d(p[0], p[1]); };
            const auto tangent = [&](const detail::Curve& curve, const detail::Point& at)
            {
                if (!curve.IsArc)
                    return gp_Vec2d(point(curve.Start), point(curve.End)).Normalized();
                const gp_Vec2d radial(point(curve.Center), point(at));
                return gp_Vec2d(-radial.Y(), radial.X()).Normalized() * std::copysign(1.0, curve.Sweep);
            };
            double total = 0;
            int turnSense = 0;
            const auto addTurn = [&](double value, double angularResolution)
            {
                total += value;
                if (std::abs(value) <= angularResolution)
                    return true;
                const int sense = value > 0 ? 1 : -1;
                if (turnSense == 0)
                    turnSense = sense;
                return sense == turnSense;
            };
            const auto length = [&](const detail::Curve& curve)
            {
                return curve.IsArc ? curve.Radius * std::abs(curve.Sweep) :
                    point(curve.Start).Distance(point(curve.End));
            };
            if (std::ranges::any_of(ring.Curves, [&](const auto& curve) { return length(curve) <= tolerance; }))
                return false;
            for (std::size_t i = 0; i < ring.Curves.size(); ++i)
            {
                const auto& curve = ring.Curves[i];
                const auto& next = ring.Curves[(i + 1) % ring.Curves.size()];
                if (curve.IsArc && !addTurn(curve.Sweep, 0))
                    return false;
                const auto a = tangent(curve, curve.End), b = tangent(next, next.Start);
                // The actual support lengths limit the angular uncertainty of
                // a join. Preserve every resolved concave turn and arc.
                const double joinTangentResolution = std::max(Precision::Angular(),
                    std::min(std::sqrt(Precision::Angular()), tolerance / std::max(length(curve), length(next))));
                if (!addTurn(std::atan2(a.Crossed(b), a.Dot(b)), joinTangentResolution))
                    return false;
            }
            const double turnTolerance = Precision::Angular() * ring.Curves.size();
            return turnSense != 0 && std::abs(std::abs(total) - 2 * std::numbers::pi) <= turnTolerance;
        }

        bool ExtendSupport(AnalyticUse& first, const AnalyticUse& next, double tolerance)
        {
            auto& a = first.Support;
            const auto& b = next.Support;
            const auto point = [](auto value) { return gp_Pnt2d(value[0], value[1]); };
            if (!first.Exact || !next.Exact || a.IsArc != b.IsArc ||
                a.IsFacetedTransition != b.IsFacetedTransition || point(a.End).Distance(point(b.Start)) > tolerance)
                return false;
            if (a.IsArc)
            {
                if (a.Sweep * b.Sweep <= 0 ||
                    point(a.Center).Distance(point(b.Center)) > tolerance ||
                    std::abs(a.Radius - b.Radius) > tolerance ||
                    std::abs(a.Sweep + b.Sweep) > 2 * std::numbers::pi + 1e-9)
                    return false;
                a.Sweep += b.Sweep;
            }
            else
            {
                const gp_Vec2d u(point(a.Start), point(a.End)), v(point(b.Start), point(b.End));
                const double length = u.Magnitude();
                if (std::abs(u.Crossed(v)) > 1e-8 * length * v.Magnitude())
                    return false;
                // Collinear consecutive uses can retrace a terminal interval.
                // Reduce that signed zero-area use only while the same run
                // still has a positive forward span.
                const gp_Vec2d retained(point(a.Start), point(b.End));
                if (u.Dot(v) <= 0 && (retained.Dot(u) <= tolerance * length ||
                    std::abs(retained.Crossed(u)) > tolerance * length))
                    return false;
            }
            a.End = b.End;
            first.NativeEdges.insert(first.NativeEdges.end(), next.NativeEdges.begin(), next.NativeEdges.end());
            first.SourceIds.insert(first.SourceIds.end(), next.SourceIds.begin(), next.SourceIds.end());
            return true;
        }

        double CertifyWireJoins(std::vector<TopoDS_Edge>& edges, double tolerance)
        {
            BRep_Builder builder;
            double nativeTolerance = Precision::Confusion();
            for (std::size_t i = 0; i < edges.size(); ++i)
            {
                TopoDS_Vertex first, last;
                TopExp::Vertices(edges[i], first, last, true);
                TopoDS_Vertex nextFirst, nextLast;
                TopExp::Vertices(edges[(i + 1) % edges.size()], nextFirst, nextLast, true);
                if (last.IsNull() || nextFirst.IsNull())
                    throw std::runtime_error("Original section edge has no end vertex");
                const auto gap = BRep_Tool::Pnt(last).Distance(BRep_Tool::Pnt(nextFirst));
                if (gap > tolerance)
                    throw std::runtime_error("Original section wire is open beyond contour tolerance");
                const auto joinTolerance = std::max({
                    nativeTolerance, BRep_Tool::Tolerance(last), BRep_Tool::Tolerance(nextFirst),
                    std::nextafter(gap, std::numeric_limits<double>::infinity())
                });
                if (gap > std::max(BRep_Tool::Tolerance(last), BRep_Tool::Tolerance(nextFirst)))
                {
                    builder.UpdateVertex(last, joinTolerance);
                    builder.UpdateVertex(nextFirst, joinTolerance);
                }
                nativeTolerance = joinTolerance;
            }
            return nativeTolerance;
        }
    }

    SSectionRegionPartitionResult SectionRegionPartitionWork::Run(
        const SSectionWiresResult& section,
        const SSectionFaceClassificationResult& classification,
        double connectionTolerance)
    {
        using Status = ESectionRegionPartitionStatus;
        if (!classification.bSuccess)
            return Outcome(Status::NotRun, "Classify did not complete");
        if (!section.bSuccess || section.Wires.empty())
            return Outcome(Status::NotRun, "No completed section contour");
        if (!std::isfinite(connectionTolerance) || connectionTolerance <= 0)
            return Outcome(Status::Failed, "Invalid section connection tolerance");

        try
        {
            const auto localFit = section.ContourFit ? std::optional<SSectionContourFit>{}
                : std::optional{FitSectionContour(section, connectionTolerance)};
            const auto& fit = section.ContourFit ? *section.ContourFit : *localFit;
            if (!fit.bSuccess || fit.Wires.size() != section.Wires.size())
                return Outcome(Status::Failed, fit.Reason.empty() ? "Missing certified contour fitting" : fit.Reason);
            const gp_Pln plane(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)));
            TopoDS_Wire outer;
            std::vector<TopoDS_Wire> holes;
            std::vector<detail::Ring> rings;
            std::optional<detail::Ring> originalSolid;
            std::vector<detail::NativeContourEdge> contourEdges;
            std::vector<std::optional<ClosedConic>> conics;
            std::vector<TopoDS_Shape> sourceCurves;
            std::vector<std::vector<std::uint64_t>> sourceIds;
            std::set<std::string> retainedKinds;
            double nativeTolerance = Precision::Confusion();
            bool allSupportsExact = true;
            for (std::size_t wireIndex = 0; wireIndex < section.Wires.size(); ++wireIndex)
            {
                const auto& wire = section.Wires[wireIndex];
                if (!wire.bClosed || wire.Edges.empty())
                    return Outcome(Status::Failed, "Section wire is open or empty");
                if (!wire.bInner && !outer.IsNull())
                    return Outcome(Status::Unsupported, "Multiple material contours");

                detail::Ring ring;
                ring.IsInner = wire.bInner;
                const auto& fittedCurves = fit.Wires[wireIndex].Curves;
                const bool hasUnsupportedFit = std::ranges::any_of(fittedCurves, [](const auto& fitted)
                {
                    return fitted.Kind != "line" && fitted.Kind != "circle";
                });
                std::set<std::size_t> facetedTransitions;
                for (const auto& fitted : fittedCurves)
                    if (fitted.Kind == "spline" && fitted.Sources.size() > 1 &&
                        std::ranges::all_of(fitted.Sources, [](const auto& source)
                        {
                            return !source.Curve.IsNull() && source.EntireEdge &&
                                Geom2dAdaptor_Curve(source.Curve, source.First, source.Last).GetType() == GeomAbs_Line;
                        }))
                        for (const auto& source : fitted.Sources)
                            facetedTransitions.insert(source.EdgeIndex);
                std::vector<detail::Curve> originalSupports;
                std::vector<TopoDS_Edge> edges;
                for (std::size_t edgeIndex = 0; edgeIndex < wire.Edges.size(); ++edgeIndex)
                {
                    const auto& edge = wire.Edges[edgeIndex];
                    const auto curve = occ::BuildOpenCascadeCurve2(edge.Curve);
                    if (curve.IsNull() || !std::isfinite(edge.Range.First) ||
                        !std::isfinite(edge.Range.Last) || edge.Range.First >= edge.Range.Last)
                        return Outcome(Status::Failed, "Invalid section curve or trim");
                    if (hasUnsupportedFit || section.Wires.size() != 2)
                    {
                        auto original = AnalyticCurve(curve, edge.Range.First, edge.Range.Last, edge.bReversed);
                        if (original)
                        {
                            original->IsFacetedTransition = facetedTransitions.contains(edgeIndex);
                            originalSupports.push_back(*original);
                        }
                    }
                    BRepBuilderAPI_MakeEdge maker(GeomAPI::To3d(curve, plane), edge.Range.First, edge.Range.Last);
                    if (!maker.IsDone())
                        return Outcome(Status::Failed, "Could not construct original section edge");
                    auto native = maker.Edge();
                    if (edge.bReversed)
                        native.Reverse();
                    edges.push_back(native);
                }
                const auto wireTolerance = CertifyWireJoins(edges, connectionTolerance);
                nativeTolerance = std::max(nativeTolerance, wireTolerance);
                if (section.Wires.size() == 1 && originalSupports.size() == edges.size())
                    originalSolid.emplace(detail::Ring{wire.bInner, originalSupports});
                conics.push_back(ReadClosedConic(wire, wireTolerance));
                for (std::size_t index = 0; index < edges.size(); ++index)
                {
                    const auto& source = wire.Edges[index];
                    contourEdges.push_back({edges[index], wireIndex, index, source.SourceEdgeId,
                        source.SourceFaceId, source.Range.First, source.Range.Last});
                }
                BRepBuilderAPI_MakeWire nativeWire;
                for (const auto& edge : edges)
                {
                    nativeWire.Add(edge);
                    if (!nativeWire.IsDone())
                        return Outcome(Status::Failed, "Could not join original section edges");
                }

                // Plan from certified supports; construct material and patch
                // evidence from the exact original curves and finite trims.
                std::vector<AnalyticUse> supports;
                const auto appendSupport = [&](AnalyticUse use)
                {
                    if (supports.empty() || !ExtendSupport(supports.back(), use, wireTolerance))
                        supports.push_back(std::move(use));
                };
                const auto nativeSpan = [&](const SSectionContourFitSpan& source)
                {
                    if (source.EntireEdge)
                        return edges.at(source.EdgeIndex);
                    BRepBuilderAPI_MakeEdge maker(GeomAPI::To3d(source.Curve, plane), source.First, source.Last);
                    if (!maker.IsDone())
                        throw std::runtime_error("Could not reconstruct an original contour source span");
                    auto edge = maker.Edge();
                    if (source.Reversed)
                        edge.Reverse();
                    return edge;
                };
                // Refitting can absorb a short straight piece into a conic.
                // Recover the whole analytic wire so the original collinear pieces can join.
                // Web roots must retain their actual finite trim attachment
                // on each hole, including when fitting moved a join within
                // its error bound.
                const bool useOriginalWire = (hasUnsupportedFit || section.Wires.size() > 2) &&
                    originalSupports.size() == edges.size();
                if (useOriginalWire)
                    for (std::size_t i = 0; i < edges.size(); ++i)
                    {
                        AnalyticUse original{originalSupports[i], {edges[i]}, {wire.Edges[i].SourceEdgeId}, true};
                        if (section.Wires.size() > 2)
                            supports.push_back(std::move(original));
                        else
                            appendSupport(std::move(original));
                    }

                for (const auto& fitted : fittedCurves)
                {
                    if (fitted.Curve.IsNull() || !std::isfinite(fitted.DeviationUpperBound) ||
                        fitted.DeviationUpperBound < 0 || fitted.DeviationUpperBound > SectionContourFitToleranceMm ||
                        fitted.Sources.empty())
                        return Outcome(Status::Failed, "Contour support lacks certified geometry or source spans");
                    if (useOriginalWire)
                        continue;
                    const auto analytic = AnalyticCurve(fitted.Curve, fitted.First, fitted.Last, fitted.Reversed);
                    if (!analytic)
                    {
                        if (fitted.Kind == "line" || fitted.Kind == "circle")
                            return Outcome(Status::Failed, "Certified contour support has an inconsistent analytic type");

                        // An ellipse fit may simplify an already exact chain of arcs.
                        // Keep those source supports available to the normal sweep.
                        std::vector<detail::Curve> exact;
                        for (const auto& source : fitted.Sources)
                        {
                            const auto original = AnalyticCurve(source.Curve, source.First, source.Last, source.Reversed);
                            if (!original)
                                break;
                            exact.push_back(*original);
                        }
                        if (exact.size() != fitted.Sources.size())
                        {
                            retainedKinds.insert(fitted.Kind);
                            continue;
                        }
                        for (std::size_t i = 0; i < exact.size(); ++i)
                        {
                            const auto& source = fitted.Sources[i];
                            exact[i].IsFacetedTransition = facetedTransitions.contains(source.EdgeIndex);
                            appendSupport({ exact[i], { nativeSpan(source) },
                                { wire.Edges.at(source.EdgeIndex).SourceEdgeId }, true });
                        }
                        continue;
                    }
                    AnalyticUse use{*analytic, {}, {}, fitted.DeviationUpperBound == 0};
                    for (const auto& source : fitted.Sources)
                    {
                        use.SourceIds.push_back(wire.Edges.at(source.EdgeIndex).SourceEdgeId);
                        use.NativeEdges.push_back(nativeSpan(source));
                    }
                    appendSupport(std::move(use));
                }
                if (section.Wires.size() <= 2 && supports.size() > 1 &&
                    ExtendSupport(supports.back(), supports.front(), wireTolerance))
                    supports.erase(supports.begin());
                for (auto& support : supports)
                {
                    allSupportsExact &= support.Exact;
                    support.Support.Id = static_cast<int>(sourceCurves.size());
                    ring.Curves.push_back(support.Support);
                    if (support.NativeEdges.size() == 1)
                        sourceCurves.push_back(support.NativeEdges.front());
                    else
                    {
                        BRep_Builder builder;
                        TopoDS_Compound originalUses;
                        builder.MakeCompound(originalUses);
                        for (const auto& use : support.NativeEdges)
                            builder.Add(originalUses, use);
                        sourceCurves.push_back(originalUses);
                    }
                    sourceIds.push_back(std::move(support.SourceIds));
                }
                rings.push_back(std::move(ring));
                if (wire.bInner)
                    holes.push_back(nativeWire.Wire());
                else
                    outer = nativeWire.Wire();
            }
            if (outer.IsNull())
                return Outcome(Status::Failed, "Section has no outer boundary");
            const bool allConic = std::ranges::all_of(conics, [](const auto& conic) { return conic.has_value(); });
            bool continuousConic = false;
            if (allConic && conics.size() == 1)
                continuousConic = true;
            else if (allConic && conics.size() == 2)
            {
                const std::size_t outerIndex = section.Wires.front().bInner ? 1 : 0;
                continuousConic = HasContinuousConicWall(*conics[outerIndex], *conics[1 - outerIndex], nativeTolerance);
            }
            BRepBuilderAPI_MakeFace nativeMaterial(plane, outer, true);
            for (const auto& hole : holes)
                nativeMaterial.Add(hole);
            if (!nativeMaterial.IsDone() || !BRepCheck_Analyzer(nativeMaterial.Face()).IsValid())
                return Outcome(Status::Failed, "Invalid original section material face");

            std::optional<detail::Plan> plan;
            if (retainedKinds.empty())
                plan = rings.size() > 2
                    ? detail::PlanMultipleInnerRings(nativeMaterial.Face(), rings, sourceCurves, connectionTolerance)
                    : detail::PlanNormalSweep(rings, connectionTolerance);
            if (!plan && !retainedKinds.empty() && !continuousConic)
            {
                plan = PlanCurvedWall(section, connectionTolerance);
                if (!plan)
                    plan = PlanCurvedOpenWall(section, nativeMaterial.Face(), contourEdges,
                        sourceCurves, sourceIds, plane, nativeTolerance);
                if (!plan)
                    plan = PlanCurvedBranch(section, nativeMaterial.Face(), contourEdges,
                        sourceCurves, sourceIds, plane, nativeTolerance);
            }
            const bool continuousSolid = !plan && retainedKinds.empty() && rings.size() == 1 &&
                ((originalSolid && HasConvexSolidBoundary(*originalSolid, nativeTolerance)) ||
                    (allSupportsExact && HasConvexSolidBoundary(rings.front(), nativeTolerance)));
            if (!plan && (continuousConic ||
                continuousSolid))
            {
                // A convex solid has no inner wall that could delimit a
                // different region. A certified conic band has one continuous
                // pair of skins. Keep the original native face whole.
                plan.emplace();
                plan->Walls.push_back({continuousConic ? "连续壁区" : "实心区", {}, {}, {}});
            }
            if (!plan)
            {
                if (!retainedKinds.empty())
                {
                    std::string reason = "Could not prove the native normal sweep for retained ";
                    for (auto kind = retainedKinds.begin(); kind != retainedKinds.end(); ++kind)
                        reason += (kind == retainedKinds.begin() ? "" : ", ") + *kind;
                    return Outcome(Status::Unresolved, reason + " boundary");
                }
                if (rings.size() > 2)
                    return Outcome(Status::Unresolved, "Could not prove finite wall and web connections across all inner loops");
                if (rings.size() == 2)
                    return Outcome(Status::Unresolved, "Could not connect closed wall sweeps around the inner loop");
                return Outcome(Status::Unresolved, "Could not connect continuous open walls, corners or branches");
            }
            const auto cells = detail::BuildMaterialCells(nativeMaterial.Face(), sourceCurves, *plan, nativeTolerance);
            if (cells.empty())
                return Outcome(Status::Failed, "Normal sweep returned no material cells");
            std::vector<bool> supportedJunctions(plan->Junctions.size());
            for (const auto& cell : cells)
            {
                if (cell.WallIds.size() < 2)
                    continue;
                if (!cell.JunctionIndex)
                    return Outcome(Status::Failed, "Shared material cell has no proven junction");
                supportedJunctions[*cell.JunctionIndex] = true;
            }
            if (std::ranges::find(supportedJunctions, false) != supportedJunctions.end())
                return Outcome(Status::Unresolved, "A junction has no shared material");

            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            for (const auto& cell : cells)
                builder.Add(compound, cell.Face);
            SSectionRegionPartitionResult result;
            result.Geometry = occ::ConvertOpenCascadeShapeToBRepGeometry(compound, "Section partition", "");
            const auto& roots = result.Geometry.RootShapes;
            if (roots.size() != 1 || result.Geometry.Compounds.size() != 1 ||
                result.Geometry.Faces.size() != cells.size())
                return Outcome(Status::Failed, "Could not preserve native material cell identities");
            const auto& children = result.Geometry.Compounds.front().Children;
            if (children.size() != cells.size() || std::ranges::any_of(children, [](const auto& child) {
                return child.Kind != iCAX::GeometryData::EBRepShapeKind::Face;
            }))
                return Outcome(Status::Failed, "Section partition compound does not contain each material face");
            for (std::size_t i = 0; i < cells.size(); ++i)
            {
                const auto& cell = cells[i];
                const int junctionId = cell.JunctionIndex ? static_cast<int>(*cell.JunctionIndex + 1) : 0;
                result.Cells.push_back({ children[i].Id, cell.Area, cell.WallIds, junctionId });
                if (junctionId != 0)
                    result.SharedArea += cell.Area;
            }
            for (const auto& wall : plan->Walls)
            {
                SSectionRegionWall output{ static_cast<int>(result.Walls.size()) + 1, wall.Label };
                for (const auto id : wall.SupportIds)
                {
                    for (const auto source : sourceIds.at(id))
                        if (source && std::find(output.SourceEdgeIds.begin(), output.SourceEdgeIds.end(), source) == output.SourceEdgeIds.end())
                            output.SourceEdgeIds.push_back(source);
                }
                if (wall.SupportIds.empty() && plan->Walls.size() == 1)
                    for (const auto& source : contourEdges)
                        if (source.SourceEdgeId && std::ranges::find(output.SourceEdgeIds, source.SourceEdgeId) == output.SourceEdgeIds.end())
                            output.SourceEdgeIds.push_back(source.SourceEdgeId);
                result.Walls.push_back(std::move(output));
            }
            for (const auto& junction : plan->Junctions)
                result.Junctions.push_back({ junction.Label, junction.WallIds });
            detail::BuildBoundaryProvenance(result, contourEdges, nativeTolerance);
            result.Status = Status::Complete;
            return result;
        }
        catch (const Standard_Failure& error)
        {
            return Outcome(Status::Failed, error.GetMessageString() ? error.GetMessageString() : "Native partition failure");
        }
        catch (const std::exception& error)
        {
            return Outcome(Status::Failed, error.what());
        }
    }
}
