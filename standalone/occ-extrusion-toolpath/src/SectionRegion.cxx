#include "SectionRegion.hxx"

#include "GeometryUtils.hxx"

#include <BRepTools_WireExplorer.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace etp::detail
{
    SectionRegion::SectionRegion(
        const std::vector<TopoDS_Wire>& wires,
        const gp_Ax3& frame,
        const AnalyzerOptions& options)
        : m_Frame(frame), m_Options(options)
    {
        m_Loops.reserve(wires.size());
        for (std::size_t wireIndex = 0; wireIndex < wires.size(); ++wireIndex)
        {
            SectionPolyline loop;
            loop.WireIndex = wireIndex;
            std::size_t edgeIndex = 0;
            for (BRepTools_WireExplorer explorer(wires[wireIndex]); explorer.More(); explorer.Next(), ++edgeIndex)
            {
                auto samples = SampleEdge(
                    TopoDS::Edge(explorer.Current()),
                    options.MinimumEdgeSamples,
                    options.SamplingDeflection,
                    true);
                if (samples.size() < 2)
                    continue;

                if (!loop.Points.empty()
                    && loop.Points.back().Distance(ToSection2d(samples.front(), frame))
                        <= options.DistanceTolerance)
                {
                    samples.erase(samples.begin());
                }

                for (const auto& point : samples)
                {
                    if (!loop.Points.empty())
                        loop.SegmentEdgeIndices.push_back(edgeIndex);
                    loop.Points.push_back(ToSection2d(point, frame));
                }
            }

            if (loop.Points.size() > 2)
            {
                if (loop.Points.front().Distance(loop.Points.back())
                    > options.DistanceTolerance)
                {
                    loop.SegmentEdgeIndices.push_back(
                        loop.SegmentEdgeIndices.empty() ? 0 : loop.SegmentEdgeIndices.back());
                    loop.Points.push_back(loop.Points.front());
                }
                m_Loops.push_back(std::move(loop));
            }
        }
    }

    SectionHit SectionRegion::Classify(const gp_Pnt& point) const
    {
        SectionHit result;
        result.AxialStation = AxialStation(point, m_Frame);
        const auto point2d = ToSection2d(point, m_Frame);

        auto bestDistance = (std::numeric_limits<double>::max)();
        std::optional<SectionCurveRef> bestBoundary;
        for (const auto& loop : m_Loops)
        {
            std::size_t segment = 0;
            double parameter = 0.0;
            const auto distance = PointPolylineDistance2d(
                point2d, loop.Points, &segment, &parameter);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                const auto edgeIndex = segment < loop.SegmentEdgeIndices.size()
                    ? loop.SegmentEdgeIndices[segment]
                    : segment;
                bestBoundary = SectionCurveRef{ loop.WireIndex, edgeIndex, parameter };
            }
        }

        result.BoundaryDistance = bestDistance;
        if (bestBoundary && bestDistance <= m_Options.DistanceTolerance)
        {
            result.State = SectionPointState::OnBoundary;
            result.Boundary = bestBoundary;
            return result;
        }

        bool inMaterial = false;
        for (const auto& loop : m_Loops)
        {
            if (PointInPolygonEvenOdd(point2d, loop.Points))
                inMaterial = !inMaterial;
        }
        result.State = inMaterial ? SectionPointState::InMaterial : SectionPointState::InVoid;
        return result;
    }

    bool SectionRegion::IsValid() const noexcept
    {
        return !m_Loops.empty();
    }

    const gp_Ax3& SectionRegion::Frame() const noexcept
    {
        return m_Frame;
    }

    const std::vector<SectionPolyline>& SectionRegion::Loops() const noexcept
    {
        return m_Loops;
    }

    gp_Ax3 InferSectionFrame(
        const std::vector<TopoDS_Wire>& wires,
        const double tolerance)
    {
        std::vector<gp_Pnt> points;
        for (const auto& wire : wires)
        {
            const auto sampled = SampleWire(wire, 7, tolerance * 5.0);
            points.insert(points.end(), sampled.begin(), sampled.end());
        }
        if (points.size() < 3)
            throw std::invalid_argument("section wires do not contain three usable points");

        const auto origin = points.front();
        std::optional<gp_Vec> firstDirection;
        for (std::size_t index = 1; index < points.size(); ++index)
        {
            gp_Vec candidate(origin, points[index]);
            if (candidate.SquareMagnitude() > tolerance * tolerance)
            {
                firstDirection = candidate;
                break;
            }
        }
        if (!firstDirection)
            throw std::invalid_argument("section wires are degenerate");

        std::optional<gp_Vec> normal;
        for (std::size_t index = 2; index < points.size(); ++index)
        {
            const gp_Vec second(origin, points[index]);
            const auto cross = firstDirection->Crossed(second);
            if (cross.SquareMagnitude() > tolerance * tolerance)
            {
                normal = cross;
                break;
            }
        }
        if (!normal)
            throw std::invalid_argument("section wires are collinear");

        const gp_Dir axis(*normal);
        gp_Vec xVector = *firstDirection
            - gp_Vec(axis) * firstDirection->Dot(gp_Vec(axis));
        if (xVector.SquareMagnitude() <= tolerance * tolerance)
            throw std::invalid_argument("failed to construct section frame");
        return gp_Ax3(origin, axis, gp_Dir(xVector));
    }
}
