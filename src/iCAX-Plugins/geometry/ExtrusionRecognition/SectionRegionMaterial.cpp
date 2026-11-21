#include "pch.h"
#include "SectionContourFitting.h"
#include "SectionRegionPartitionDetail.h"
#include "SectionRegionPartitionWork.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <ElCLib.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GProp_GProps.hxx>
#include <IntTools_EdgeEdge.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include <iomanip>
#include <numeric>
#include <numbers>
#include <sstream>

namespace iCAX::ExtrusionRecognition::section_partition_detail
{
namespace
{
enum class BooleanOperation { Common, Cut, Fuse };

double Area(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return 0.0;
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(shape, properties, 1e-10);
    const double area = std::abs(properties.Mass());
    if (!std::isfinite(area)) throw std::runtime_error("Non-finite section material area");
    return area;
}

TopoDS_Shape Boolean(const TopoDS_Shape& left, const TopoDS_Shape& right, BooleanOperation kind)
{
    if (left.IsNull()) return kind == BooleanOperation::Fuse ? right : TopoDS_Shape{};
    if (right.IsNull()) return kind == BooleanOperation::Common ? TopoDS_Shape{} : left;
    if (left.IsSame(right)) return kind == BooleanOperation::Cut ? TopoDS_Shape{} : left;

    NCollection_List<TopoDS_Shape> arguments, tools;
    arguments.Append(left);
    tools.Append(right);
    auto build = [&](auto& operation, const char* name)
    {
        operation.SetArguments(arguments);
        operation.SetTools(tools);
        operation.SetNonDestructive(true);
        operation.SetFuzzyValue(0.0);
        operation.Build();
        if (!operation.IsDone() || operation.HasErrors())
            throw std::runtime_error(std::string("Native section material Boolean failed: ") + name);
        return operation.Shape();
    };
    switch (kind)
    {
    case BooleanOperation::Common:
    {
        BRepAlgoAPI_Common operation;
        return build(operation, "common");
    }
    case BooleanOperation::Cut:
    {
        BRepAlgoAPI_Cut operation;
        return build(operation, "cut");
    }
    default:
    {
        BRepAlgoAPI_Fuse operation;
        return build(operation, "fuse");
    }
    }
}

TopoDS_Face MakeCoverageFace(const std::vector<Point>& points)
{
    if (points.size() < 3) throw std::runtime_error("Section sweep coverage has fewer than three corners");
    BRepBuilderAPI_MakeWire wire;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        BRepBuilderAPI_MakeEdge edge(gp_Pnt(0, a[0], a[1]), gp_Pnt(0, b[0], b[1]));
        if (!edge.IsDone()) throw std::runtime_error("Could not construct a section sweep support edge");
        wire.Add(edge.Edge());
    }
    if (!wire.IsDone()) throw std::runtime_error("Section sweep support does not close");
    const gp_Pln plane(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)));
    BRepBuilderAPI_MakeFace face(plane, wire.Wire(), true);
    if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
        throw std::runtime_error("Section sweep support forms an invalid native face");
    return face.Face();
}

#include "SectionRegionMultipleInnerRings.inc"

Bnd_Box Bounds(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box, false);
    return box;
}

struct BoundedEdge
{
    TopoDS_Edge Edge;
    Bnd_Box Box;
};

std::vector<BoundedEdge> BoundaryEdges(const TopoDS_Shape& shape)
{
    std::vector<BoundedEdge> edges;
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next())
        edges.push_back({TopoDS::Edge(explorer.Current()), Bounds(explorer.Current())});
    return edges;
}

bool SharesBoundaryInterval(const std::vector<BoundedEdge>& first,
                            const std::vector<BoundedEdge>& second,
                            double tolerance)
{
    for (const auto& a : first)
        for (const auto& b : second)
        {
            if (a.Box.IsOut(b.Box)) continue;
            IntTools_EdgeEdge intersection(a.Edge, b.Edge);
            intersection.Perform();
            if (!intersection.IsDone()) continue;
            for (const auto& part : intersection.CommonParts())
            {
                if (part.Type() != TopAbs_EDGE) continue;
                double start = 0, end = 0;
                part.Range1(start, end);
                if (GCPnts_AbscissaPoint::Length(BRepAdaptor_Curve(part.Edge1()), start, end) > 10 * tolerance)
                    return true;
            }
        }
    return false;
}

bool IsConnectedWall(const TopoDS_Shape& shape)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    if (faces.Extent() <= 1) return faces.Extent() == 1;
    std::vector<int> parents(faces.Extent());
    std::iota(parents.begin(), parents.end(), 0);
    auto root = [&](int i)
    {
        while (parents[i] != i) i = parents[i];
        return i;
    };
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> adjacency;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, adjacency);
    for (int i = 1; i <= adjacency.Extent(); ++i)
    {
        int previous = -1;
        for (const auto& face : adjacency(i))
        {
            const int index = faces.FindIndex(face) - 1;
            if (previous >= 0) parents[root(index)] = root(previous);
            previous = index;
        }
    }
    for (int i = 1; i < faces.Extent(); ++i)
        if (root(i) != root(0)) return false;
    return true;
}

struct PatchEvidence
{
    std::size_t WallIndex;
    std::vector<int> AdjacentWallIds;
    std::vector<BoundedEdge> Edges;
};

std::vector<TopoDS_Shape> BuildWalls(const TopoDS_Face& material,
                                    const std::vector<TopoDS_Shape>& originalCurves,
                                    const Plan& plan, double tolerance, double minArea)
{
    std::vector<bool> patchWalls(plan.Walls.size());
    std::vector<PatchEvidence> rules;
    for (const auto& patch : plan.Patches)
    {
        if (patch.CurveId < 0 || static_cast<std::size_t>(patch.CurveId) >= originalCurves.size())
            throw std::runtime_error("Section corner lacks an original trim");
        if (patch.WallIndex >= plan.Walls.size() || patchWalls[patch.WallIndex])
            throw std::runtime_error("Section corner has invalid region identity");
        patchWalls[patch.WallIndex] = true;
        rules.push_back({patch.WallIndex, patch.AdjacentWallIds, BoundaryEdges(originalCurves[patch.CurveId])});
    }

    std::vector<TopoDS_Shape> walls;
    TopoDS_Shape coreUnion;
    for (const auto& candidate : plan.Walls)
    {
        if (plan.RemainderWallIndex == walls.size() || patchWalls[walls.size()])
        {
            walls.emplace_back();
            continue;
        }
        auto core = candidate.Polygon.empty() ? TopoDS_Shape(material)
            : Boolean(material, MakeCoverageFace(candidate.Polygon), BooleanOperation::Common);
        if (Area(core) <= minArea || !BRepCheck_Analyzer(core).IsValid())
            throw std::runtime_error("Section sweep wall has no valid material intersection");
        coreUnion = Boolean(coreUnion, core, BooleanOperation::Fuse);
        walls.push_back(std::move(core));
    }

    const auto remainder = Boolean(material, coreUnion, BooleanOperation::Cut);
    if (plan.RemainderWallIndex)
    {
        if (Area(remainder) <= minArea)
            throw std::runtime_error("Section sweep leaves no material for the remaining region");
        walls[*plan.RemainderWallIndex] = remainder;
    }
    else if (Area(remainder) > minArea)
    {
        // Match each original curved boundary to a separate native face.
        std::vector<std::vector<BoundedEdge>> coreEdges;
        for (const auto& core : walls)
            coreEdges.push_back(core.IsNull() ? std::vector<BoundedEdge>{} : BoundaryEdges(core));

        for (TopExp_Explorer face(remainder, TopAbs_FACE); face.More(); face.Next())
        {
            const auto& shape = face.Current();
            if (Area(shape) <= minArea) continue;
            const auto edges = BoundaryEdges(shape);
            std::vector<int> adjacent;
            for (std::size_t i = 0; i < coreEdges.size(); ++i)
                if (SharesBoundaryInterval(edges, coreEdges[i], tolerance))
                    adjacent.push_back(static_cast<int>(i + 1));

            const PatchEvidence* match = nullptr;
            for (const auto& rule : rules)
            {
                if (!SharesBoundaryInterval(edges, rule.Edges, tolerance)) continue;
                if (adjacent != rule.AdjacentWallIds)
                    throw std::runtime_error("Section corner adjacency disagrees with the sweep");
                if (match)
                    throw std::runtime_error("Section corner matches more than one original trim");
                match = &rule;
            }
            if (!match || !walls[match->WallIndex].IsNull())
                throw std::runtime_error("Section remainder does not match one independent corner region");
            walls[match->WallIndex] = shape;
        }
    }
    for (const auto& wall : walls)
        if (wall.IsNull() || !BRepCheck_Analyzer(wall).IsValid() || !IsConnectedWall(wall))
            throw std::runtime_error("Section sweep wall is disconnected or has invalid native topology");
    return walls;
}

void AppendCells(std::vector<NativeCell>& cells, const TopoDS_Shape& shape,
                 const std::vector<int>& owners, double minArea)
{
    for (TopExp_Explorer face(shape, TopAbs_FACE); face.More(); face.Next())
    {
        const double area = Area(face.Current());
        if (area > minArea) cells.push_back({TopoDS::Face(face.Current()), owners, area});
    }
}

TopoDS_Shape CutSectionMaterial(const TopoDS_Face& material,
    const std::vector<std::array<Point, 2>>& cuts, double tolerance)
{
    NCollection_List<TopoDS_Shape> arguments, tools;
    arguments.Append(material);
    for (const auto& cut : cuts)
    {
        const gp_Pnt a(0, cut[0][0], cut[0][1]), b(0, cut[1][0], cut[1][1]);
        if (a.Distance(b) <= tolerance)
            throw std::runtime_error("Finite normal cut has no material span");
        // Continue across the fitting uncertainty so the split ends on the
        // original boundary rather than at a fitted point inside material.
        const gp_Vec extension = gp_Vec(a, b).Normalized() * std::max(2 * SectionContourFitToleranceMm, 4 * tolerance);
        BRepBuilderAPI_MakeEdge edge(a.Translated(-extension), b.Translated(extension));
        if (!edge.IsDone())
            throw std::runtime_error("Could not construct finite normal cut");
        tools.Append(edge.Edge());
    }
    BRepAlgoAPI_Splitter splitter;
    splitter.SetArguments(arguments);
    splitter.SetTools(tools);
    splitter.SetNonDestructive(true);
    splitter.Build();
    if (!splitter.IsDone() || splitter.HasErrors())
        throw std::runtime_error("Original section could not be split by its finite normal cuts");
    auto divided = splitter.Shape();
    // At a branch root the overrun may leave an internal tool edge in one
    // face. Remove it while preserving the connectivity of the cut boundary.
    BRepTools::RemoveInternals(divided);
    if (!BRepCheck_Analyzer(divided).IsValid())
        throw std::runtime_error("Finite normal cuts produced invalid material faces");
    return divided;
}

std::vector<TopoDS_Shape> SplitCrosscutWalls(const TopoDS_Face& material,
    const std::vector<TopoDS_Shape>& originalCurves, const Plan& plan, double tolerance, double minArea)
{
    if (plan.RemainderWallIndex || !plan.Junctions.empty() || !plan.Patches.empty())
        throw std::runtime_error("Finite normal cuts require independent regions");
    const auto divided = CutSectionMaterial(material, plan.Crosscuts, tolerance);

    std::vector<TopoDS_Shape> walls(plan.Walls.size());
    std::vector<std::vector<std::vector<BoundedEdge>>> guides;
    for (const auto& wall : plan.Walls)
    {
        if (wall.GuideIds.empty())
            throw std::runtime_error("Finite normal cut region has no original boundary guide");
        auto& wallGuides = guides.emplace_back();
        for (const int id : wall.GuideIds)
            wallGuides.push_back(BoundaryEdges(originalCurves.at(id)));
    }
    for (TopExp_Explorer face(divided, TopAbs_FACE); face.More(); face.Next())
    {
        const auto& shape = face.Current();
        if (Area(shape) <= minArea)
            continue;
        const auto edges = BoundaryEdges(shape);
        std::optional<std::size_t> owner;
        for (std::size_t i = 0; i < guides.size(); ++i)
            if (std::ranges::all_of(guides[i], [&](const auto& guide) { return SharesBoundaryInterval(edges, guide, tolerance); }))
            {
                if (owner)
                    throw std::runtime_error("Finite normal cuts leave more than one guide region on a face");
                owner = i;
            }
        if (!owner || !walls[*owner].IsNull())
            throw std::runtime_error("Finite normal cuts do not identify one native face per guide region");
        walls[*owner] = shape;
    }
    if (std::ranges::any_of(walls, [](const auto& wall) { return wall.IsNull(); }))
        throw std::runtime_error("Finite normal cuts did not separate all guide regions");
    return walls;
}

std::vector<NativeCell> SplitByMembership(const std::vector<TopoDS_Shape>& walls, double minArea)
{
    std::vector<NativeCell> cells;
    TopoDS_Shape covered;
    for (std::size_t i = 0; i < walls.size(); ++i)
    {
        const auto& wall = walls[i];
        const Bnd_Box wallBox = Bounds(wall);
        const int id = static_cast<int>(i + 1);
        std::vector<NativeCell> split;
        for (const auto& cell : cells)
        {
            const auto intersection = wallBox.IsOut(Bounds(cell.Face)) ? TopoDS_Shape{}
                : Boolean(cell.Face, wall, BooleanOperation::Common);
            if (Area(intersection) <= minArea)
            {
                split.push_back(cell);
                continue;
            }
            AppendCells(split, Boolean(cell.Face, wall, BooleanOperation::Cut), cell.WallIds, minArea);
            auto owners = cell.WallIds;
            owners.push_back(id);
            AppendCells(split, intersection, owners, minArea);
        }
        AppendCells(split, Boolean(wall, covered, BooleanOperation::Cut), {id}, minArea);
        covered = Boolean(covered, wall, BooleanOperation::Fuse);
        cells = std::move(split);
    }
    return cells;
}

void VerifyPartition(const TopoDS_Face& material, const std::vector<TopoDS_Shape>& walls,
                     const std::vector<NativeCell>& cells, double coverageTol, double overlapTol)
{
    TopoDS_Shape united;
    std::vector<TopoDS_Shape> memberships(walls.size());
    std::vector<Bnd_Box> boxes;
    double overlap = 0;
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        const auto& cell = cells[i];
        if (!BRepCheck_Analyzer(cell.Face).IsValid())
            throw std::runtime_error("Section material block has invalid native topology");
        const auto box = Bounds(cell.Face);
        for (std::size_t j = 0; j < i; ++j)
            if (!box.IsOut(boxes[j]))
                overlap += Area(Boolean(cell.Face, cells[j].Face, BooleanOperation::Common));
        boxes.push_back(box);
        united = Boolean(united, cell.Face, BooleanOperation::Fuse);
        for (const auto owner : cell.WallIds)
        {
            auto& reconstructed = memberships[owner - 1];
            reconstructed = Boolean(reconstructed, cell.Face, BooleanOperation::Fuse);
        }
    }

    const double missing = Area(Boolean(material, united, BooleanOperation::Cut));
    const double excess = Area(Boolean(united, material, BooleanOperation::Cut));
    if (missing > coverageTol || excess > coverageTol || overlap > overlapTol)
    {
        std::ostringstream message;
        message << std::setprecision(12) << "Section material partition proof failed (missing=" << missing
                << ", excess=" << excess << ", overlap=" << overlap << ')';
        throw std::runtime_error(message.str());
    }
    for (std::size_t i = 0; i < walls.size(); ++i)
        if (memberships[i].IsNull()
            || Area(Boolean(walls[i], memberships[i], BooleanOperation::Cut)) > overlapTol
            || Area(Boolean(memberships[i], walls[i], BooleanOperation::Cut)) > overlapTol)
            throw std::runtime_error("Material block ownership does not reconstruct wall " + std::to_string(i + 1));
}

void IdentifyJunctions(std::vector<NativeCell>& cells, const Plan& plan,
    const std::vector<TopoDS_Shape>& originalCurves, double tolerance)
{
    std::vector<std::vector<BoundedEdge>> sourceEdges(plan.Junctions.size());
    for (std::size_t i = 0; i < plan.Junctions.size(); ++i)
    {
        const auto source = plan.Junctions[i].SourceCurveId;
        if (source)
            sourceEdges[i] = BoundaryEdges(originalCurves.at(*source));
    }
    for (auto& cell : cells)
    {
        if (cell.WallIds.size() < 2)
            continue;
        std::vector<BoundedEdge> cellEdges;
        for (std::size_t i = 0; i < plan.Junctions.size(); ++i)
        {
            const auto& junction = plan.Junctions[i];
            if (junction.WallIds != cell.WallIds)
                continue;
            if (junction.SourceCurveId)
            {
                if (cellEdges.empty())
                    cellEdges = BoundaryEdges(cell.Face);
                if (!SharesBoundaryInterval(cellEdges, sourceEdges[i], tolerance))
                    continue;
            }
            if (cell.JunctionIndex)
                throw std::runtime_error("Shared material cell matches multiple proven junctions");
            cell.JunctionIndex = i;
        }
    }
}

using ParameterInterval = iCAX::GeometryData::ParameterRange;

struct BoundaryContact
{
    double First = 0;
    double Last = 0;
    std::uint64_t AdjacentCell = 0;
    const NativeContourEdge* Contour = nullptr;
    std::vector<ParameterInterval> ContourParameters;
};

struct CellEdge
{
    std::uint64_t CellFaceId;
    std::uint64_t EdgeId;
    BoundedEdge Boundary;
    std::vector<BoundaryContact> Contacts;
};

template <typename Receive>
bool AnalyticEdgeIntervals(const BRepAdaptor_Curve& first, const BRepAdaptor_Curve& second,
    double tolerance, Receive& receive)
{
    const auto firstType = first.GetType(), secondType = second.GetType();
    if (firstType != secondType)
        return false;
    const double firstLow = first.FirstParameter(), firstHigh = first.LastParameter();
    const double secondLow = second.FirstParameter(), secondHigh = second.LastParameter();
    const auto intersect = [&](double offset, double direction)
    {
        const double a = (secondLow - offset) * direction;
        const double b = (secondHigh - offset) * direction;
        const double low = std::max(firstLow, std::min(a, b));
        const double high = std::min(firstHigh, std::max(a, b));
        if (low >= high)
            return;
        const double q0 = offset + direction * low, q1 = offset + direction * high;
        const double otherLow = std::max(secondLow, std::min(q0, q1));
        const double otherHigh = std::min(secondHigh, std::max(q0, q1));
        if (otherLow < otherHigh)
            receive(low, high, std::vector<ParameterInterval>{{otherLow, otherHigh}});
    };
    if (firstType == GeomAbs_Line)
    {
        const auto a = first.Line(), b = second.Line();
        if (!a.Direction().IsParallel(b.Direction(), Precision::Angular()) ||
            b.Distance(first.Value(firstLow)) > tolerance || b.Distance(first.Value(firstHigh)) > tolerance)
            return false;
        intersect(ElCLib::Parameter(b, a.Location()), a.Direction().Dot(b.Direction()) < 0 ? -1 : 1);
        return true;
    }
    double offset = 0, direction = 1;
    if (firstType == GeomAbs_Circle)
    {
        const auto a = first.Circle(), b = second.Circle();
        if (a.Location().Distance(b.Location()) > tolerance ||
            std::abs(a.Radius() - b.Radius()) > tolerance ||
            !a.Axis().Direction().IsParallel(b.Axis().Direction(), Precision::Angular()))
            return false;
        offset = ElCLib::Parameter(b, ElCLib::Value(0.0, a));
        direction = a.Axis().Direction().Dot(b.Axis().Direction()) < 0 ? -1 : 1;
    }
    else if (firstType == GeomAbs_Ellipse)
    {
        const auto a = first.Ellipse(), b = second.Ellipse();
        if (a.Location().Distance(b.Location()) > tolerance ||
            std::abs(a.MajorRadius() - b.MajorRadius()) > tolerance ||
            std::abs(a.MinorRadius() - b.MinorRadius()) > tolerance ||
            !a.Axis().Direction().IsParallel(b.Axis().Direction(), Precision::Angular()) ||
            !a.Position().XDirection().IsParallel(b.Position().XDirection(), Precision::Angular()))
            return false;
        offset = ElCLib::Parameter(b, ElCLib::Value(0.0, a));
        direction = a.Axis().Direction().Dot(b.Axis().Direction()) < 0 ? -1 : 1;
    }
    else
        return false;
    constexpr double period = 2 * std::numbers::pi;
    if (firstHigh - firstLow > period + Precision::PConfusion() ||
        secondHigh - secondLow > period + Precision::PConfusion())
        return false;
    const double align = std::round((std::midpoint(secondLow, secondHigh) -
        (offset + direction * std::midpoint(firstLow, firstHigh))) / period) * period;
    for (const double shift : {align - period, align, align + period})
        intersect(offset + shift, direction);
    return true;
}

template <typename Receive>
void CommonEdgeIntervals(const BoundedEdge& first, const BoundedEdge& second, double tolerance, Receive receive)
{
    if (first.Box.IsOut(second.Box))
        return;
    if (first.Edge.IsSame(second.Edge))
    {
        double low = 0, high = 0;
        BRep_Tool::Range(first.Edge, low, high);
        receive(low, high, std::vector<ParameterInterval>{{low, high}});
        return;
    }
    if (AnalyticEdgeIntervals(BRepAdaptor_Curve(first.Edge), BRepAdaptor_Curve(second.Edge), tolerance, receive))
        return;
    IntTools_EdgeEdge intersection(first.Edge, second.Edge);
    intersection.Perform();
    if (!intersection.IsDone())
        return;
    for (const auto& common : intersection.CommonParts())
    {
        if (common.Type() != TopAbs_EDGE)
            continue;
        double low = 0, high = 0;
        common.Range1(low, high);
        std::vector<ParameterInterval> opposite;
        for (const auto& range : common.Ranges2())
            if (range.First() < range.Last())
                opposite.push_back({range.First(), range.Last()});
        if (std::isfinite(low) && std::isfinite(high) && low < high && !opposite.empty())
            receive(low, high, std::move(opposite));
    }
}

std::optional<double> ParameterOnTrim(const gp_Pnt& point, const TopoDS_Edge& edge,
    const ParameterInterval& trim, double tolerance)
{
    double first = 0, last = 0;
    const auto curve = BRep_Tool::Curve(edge, first, last);
    if (curve.IsNull())
        return std::nullopt;
    const double low = std::max(first, trim.First), high = std::min(last, trim.Last);
    if (low >= high)
        return std::nullopt;
    GeomAPI_ProjectPointOnCurve projection(point, curve, low, high);
    const auto start = curve->Value(low).Distance(point), end = curve->Value(high).Distance(point);
    if (start <= end && start <= tolerance)
        return low;
    if (end <= tolerance)
        return high;
    if (projection.NbPoints() > 0 && projection.LowerDistance() <= tolerance)
        return projection.LowerDistanceParameter();
    return std::nullopt;
}

std::vector<ParameterInterval> RestrictContourParameters(const BoundaryContact& contact,
    const BRepAdaptor_Curve& cellCurve, double first, double last, double parameterTolerance, double tolerance)
{
    if (std::abs(first - contact.First) <= parameterTolerance &&
        std::abs(last - contact.Last) <= parameterTolerance)
        return contact.ContourParameters;
    std::vector<ParameterInterval> restricted;
    const auto& edge = contact.Contour->Edge;
    for (const auto& range : contact.ContourParameters)
    {
        const auto a = ParameterOnTrim(cellCurve.Value(first), edge, range, tolerance);
        const auto b = ParameterOnTrim(cellCurve.Value(last), edge, range, tolerance);
        if (a && b && *a != *b)
            restricted.push_back({std::min(*a, *b), std::max(*a, *b)});
    }
    return restricted;
}

bool AppendBoundarySpans(const CellEdge& edge, double tolerance, std::vector<SSectionRegionBoundarySpan>& spans)
{
    const BRepAdaptor_Curve curve(edge.Boundary.Edge);
    const double first = curve.FirstParameter(), last = curve.LastParameter();
    if (!std::isfinite(first) || !std::isfinite(last) || first >= last)
        return false;
    const double resolution = std::min(curve.Resolution(tolerance), (last - first) / 4);
    std::vector<double> events{first, last};
    for (const auto& contact : edge.Contacts)
        for (const double parameter : {contact.First, contact.Last})
            if (parameter > first + resolution && parameter < last - resolution)
                events.push_back(parameter);
    std::ranges::sort(events);
    events.erase(std::unique(events.begin(), events.end(), [&](double a, double b)
    {
        return b - a <= resolution;
    }), events.end());
    bool complete = true;
    for (std::size_t i = 1; i < events.size(); ++i)
    {
        SSectionRegionBoundarySpan span{edge.CellFaceId, edge.EdgeId, events[i - 1], events[i]};
        const double middle = std::midpoint(span.FirstParameter, span.LastParameter);
        for (const auto& contact : edge.Contacts)
        {
            if (middle <= contact.First || middle >= contact.Last ||
                contact.First > span.FirstParameter + resolution || contact.Last < span.LastParameter - resolution)
                continue;
            if (contact.AdjacentCell)
            {
                if (std::ranges::find(span.AdjacentCellFaceIds, contact.AdjacentCell) == span.AdjacentCellFaceIds.end())
                    span.AdjacentCellFaceIds.push_back(contact.AdjacentCell);
                continue;
            }
            const auto& source = *contact.Contour;
            const auto parameters = RestrictContourParameters(contact, curve, span.FirstParameter,
                span.LastParameter, resolution, tolerance);
            double nativeFirst = 0, nativeLast = 0;
            BRep_Tool::Range(source.Edge, nativeFirst, nativeLast);
            if (!(source.FirstParameter < source.LastParameter))
                continue;
            // A periodic edge builder can shift the native trim by one or
            // more periods. Keep the incoming contour's parameter domain.
            const double sourceShift = source.FirstParameter - nativeFirst;
            if (std::abs((nativeLast + sourceShift) - source.LastParameter) >
                BRepAdaptor_Curve(source.Edge).Resolution(tolerance))
                continue;
            for (const auto& range : parameters)
            {
                const double low = std::clamp(range.First + sourceShift, source.FirstParameter, source.LastParameter);
                const double high = std::clamp(range.Last + sourceShift, source.FirstParameter, source.LastParameter);
                if (low < high)
                    span.ContourSpans.push_back({source.WireIndex, source.EdgeIndex, source.SourceEdgeId,
                        source.SourceFaceId, low, high});
            }
        }
        if ((span.AdjacentCellFaceIds.empty() && span.ContourSpans.empty()) ||
            (!span.AdjacentCellFaceIds.empty() && !span.ContourSpans.empty()) || span.AdjacentCellFaceIds.size() > 1)
        {
            complete = false;
            continue;
        }
        spans.push_back(std::move(span));
    }
    return complete;
}
}

std::optional<Plan> PlanMultipleInnerRings(
    const TopoDS_Face& material, const std::vector<Ring>& rings,
    const std::vector<TopoDS_Shape>& originalCurves, double tolerance)
{
    if (auto plan = multiple_inner_rings::BuildPlan(material, rings, tolerance))
        return plan;

    std::vector<Ring> joined;
    std::vector<std::vector<BoundedEdge>> sourceEdges(originalCurves.size());
    std::vector<const Curve*> exterior(originalCurves.size());
    for (const auto& ring : rings)
    {
        const auto boundary = multiple_inner_rings::MakeBoundary(ring, tolerance);
        if (!boundary)
            return std::nullopt;
        auto& continuous = joined.emplace_back(Ring{ring.IsInner, {}});
        for (const auto& trim : boundary->Trims)
            continuous.Curves.push_back(trim.Shape);
        for (const auto& curve : ring.Curves)
        {
            sourceEdges.at(curve.Id) = BoundaryEdges(originalCurves.at(curve.Id));
            if (!ring.IsInner)
                exterior.at(curve.Id) = &curve;
        }
    }
    auto cuts = PlanNormalBoundaryCuts(joined, tolerance);
    if (!cuts)
        return std::nullopt;

    const auto divided = CutSectionMaterial(material, *cuts, tolerance);
    std::vector<std::vector<int>> supports;
    std::vector<int> uses(originalCurves.size());
    for (TopExp_Explorer face(divided, TopAbs_FACE); face.More(); face.Next())
    {
        if (Area(face.Current()) <= 4 * tolerance * tolerance)
            continue;
        const auto edges = BoundaryEdges(face.Current());
        auto& supported = supports.emplace_back();
        for (std::size_t i = 0; i < sourceEdges.size(); ++i)
            if (SharesBoundaryInterval(edges, sourceEdges[i], tolerance))
            {
                supported.push_back(static_cast<int>(i));
                ++uses[i];
            }
        if (supported.size() < 2)
            return std::nullopt;
    }
    if (supports.size() < 2)
        return std::nullopt;

    Plan plan;
    plan.Crosscuts = std::move(*cuts);
    for (auto& supported : supports)
    {
        // A web crossing can share an outer trim with its neighbour. Give
        // each cell only guide IDs that witness this exact cut cell uniquely.
        std::vector<int> guides;
        for (const int id : supported)
            if (exterior[id] && uses[id] == 1)
                guides.push_back(id);
        if (guides.empty())
            return std::nullopt;
        const bool arc = std::ranges::all_of(guides, [&](int id) { return exterior[id]->IsArc; });
        const auto label = std::string(arc ? "曲壁 " : "壁区 ") + std::to_string(plan.Walls.size() + 1);
        plan.Walls.push_back({label, std::move(guides), std::move(supported), {}});
    }
    return plan;
}

std::vector<NativeCell> BuildMaterialCells(const TopoDS_Face& material,
                                          const std::vector<TopoDS_Shape>& originalCurves,
                                          const Plan& plan, double tolerance)
{
    if (material.IsNull() || !BRepCheck_Analyzer(material).IsValid() || plan.Walls.empty()
        || !std::isfinite(tolerance) || tolerance <= 0)
        throw std::runtime_error("Section partition requires a valid native material face and sweep plan");
    if (plan.RemainderWallIndex && (*plan.RemainderWallIndex >= plan.Walls.size() ||
        plan.Walls.size() < 2 || !plan.Junctions.empty() || !plan.Patches.empty()))
        throw std::runtime_error("The remaining material must have a single independent region");
    if (!plan.Patches.empty() && !plan.Junctions.empty())
        throw std::runtime_error("Independent section corners cannot have shared ownership");
    const double materialArea = Area(material);
    const auto box = Bounds(material);
    if (box.IsVoid()) throw std::runtime_error("Section material face has no finite bounds");
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double diagonal = std::hypot(ymax - ymin, zmax - zmin);
    if (!(materialArea > 0) || !(diagonal > 0) || !std::isfinite(diagonal))
        throw std::runtime_error("Section material face has no positive area");

    const double minArea = std::max(4 * tolerance * tolerance,
        128 * std::numeric_limits<double>::epsilon() * materialArea);
    const double ownershipTolerance = 16 * minArea;
    const auto walls = plan.Crosscuts.empty() ? BuildWalls(material, originalCurves, plan, tolerance, minArea) :
        SplitCrosscutWalls(material, originalCurves, plan, tolerance, minArea);
    std::vector<NativeCell> cells;
    if (plan.RemainderWallIndex || !plan.Patches.empty() || !plan.Crosscuts.empty())
    {
        for (std::size_t i = 0; i < walls.size(); ++i)
        {
            AppendCells(cells, walls[i], {static_cast<int>(i + 1)}, minArea);
            if (cells.size() != i + 1)
                throw std::runtime_error("Each independent section region must be one native face");
        }
    }
    else
        cells = SplitByMembership(walls, minArea);
    if (cells.empty()) throw std::runtime_error("Native section partition produced no material blocks");
    VerifyPartition(material, walls, cells, std::max(ownershipTolerance, 20 * tolerance * diagonal),
                    ownershipTolerance);
    IdentifyJunctions(cells, plan, originalCurves, tolerance);
    return cells;
}

void BuildBoundaryProvenance(SSectionRegionPartitionResult& result,
    const std::vector<NativeContourEdge>& contourEdges, double tolerance)
{
    if (result.Cells.empty() || contourEdges.empty())
        return;
    const auto rebuilt = iCAX::OpenCascade::ReconstructOpenCascadeShape(result.Geometry);
    if (!rebuilt.bOK)
        return;
    NCollection_IndexedDataMap<TopoDS_Shape, std::uint64_t, TopTools_ShapeMapHasher> edgeIds;
    for (const auto& [id, edge] : rebuilt.Edges)
        edgeIds.Add(edge, id);
    std::vector<CellEdge> cellEdges;
    for (const auto& cell : result.Cells)
    {
        const auto face = rebuilt.Faces.find(cell.FaceId);
        if (face == rebuilt.Faces.end())
            return;
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
        TopExp::MapShapes(face->second, TopAbs_EDGE, edges);
        for (int i = 1; i <= edges.Extent(); ++i)
        {
            const auto edge = TopoDS::Edge(edges(i));
            if (BRep_Tool::Degenerated(edge))
                continue;
            const auto id = edgeIds.FindIndex(edge);
            if (!id)
                return;
            cellEdges.push_back({cell.FaceId, edgeIds.FindFromIndex(id), {edge, Bounds(edge)}});
        }
    }
    if (cellEdges.empty())
        return;
    std::vector<BoundedEdge> original;
    original.reserve(contourEdges.size());
    for (const auto& edge : contourEdges)
        original.push_back({edge.Edge, Bounds(edge.Edge)});
    for (std::size_t i = 0; i < cellEdges.size(); ++i)
    {
        auto& edge = cellEdges[i];
        for (std::size_t j = 0; j < contourEdges.size(); ++j)
            CommonEdgeIntervals(edge.Boundary, original[j], tolerance, [&](double first, double last, auto parameters)
            {
                edge.Contacts.push_back({first, last, 0, &contourEdges[j], std::move(parameters)});
            });
        for (std::size_t j = 0; j < i; ++j)
        {
            auto& other = cellEdges[j];
            if (other.CellFaceId == edge.CellFaceId)
                continue;
            CommonEdgeIntervals(edge.Boundary, other.Boundary, tolerance, [&](double first, double last, const auto& parameters)
            {
                edge.Contacts.push_back({first, last, other.CellFaceId});
                for (const auto& range : parameters)
                    other.Contacts.push_back({range.First, range.Last, edge.CellFaceId});
            });
        }
    }
    result.bBoundaryProvenanceComplete = true;
    for (const auto& edge : cellEdges)
        result.bBoundaryProvenanceComplete &= AppendBoundarySpans(edge, tolerance, result.BoundarySpans);
}
}
