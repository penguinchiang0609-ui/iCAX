#include "GeometryUtils.hxx"

#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace etp::detail
{
    namespace
    {
        constexpr double kTiny = 1.0e-12;

        std::size_t CurveSampleCount(
            const BRepAdaptor_Curve& curve,
            const std::size_t minimumSamples)
        {
            switch (curve.GetType())
            {
            case GeomAbs_Line:
                // Two endpoints are enough to describe a line geometrically,
                // but not enough to decide whether its interior crosses stock
                // material. Side/access recognition therefore samples lines
                // just like other boundary curves.
                return std::max<std::size_t>(minimumSamples, 3);
            case GeomAbs_Circle:
            case GeomAbs_Ellipse:
                return std::max<std::size_t>(minimumSamples, 25);
            case GeomAbs_BSplineCurve:
            case GeomAbs_BezierCurve:
            case GeomAbs_OffsetCurve:
            case GeomAbs_OtherCurve:
                return std::max<std::size_t>(minimumSamples, 33);
            default:
                return std::max<std::size_t>(minimumSamples, 17);
            }
        }
    }

    std::vector<gp_Pnt> SampleEdge(
        const TopoDS_Edge& edge,
        const std::size_t minimumSamples,
        const double /*deflection*/,
        const bool respectOrientation)
    {
        std::vector<gp_Pnt> result;
        if (edge.IsNull())
            return result;

        BRepAdaptor_Curve curve(edge);
        const auto first = curve.FirstParameter();
        const auto last = curve.LastParameter();
        if (!std::isfinite(first) || !std::isfinite(last) || last < first)
            return result;

        const auto count = CurveSampleCount(curve, std::max<std::size_t>(2, minimumSamples));
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto ratio = count == 1
                ? 0.0
                : static_cast<double>(index) / static_cast<double>(count - 1);
            const auto parameter = first + (last - first) * ratio;
            result.push_back(curve.Value(parameter));
        }

        if (respectOrientation && edge.Orientation() == TopAbs_REVERSED)
            std::reverse(result.begin(), result.end());
        return result;
    }

    std::vector<gp_Pnt> SampleWire(
        const TopoDS_Wire& wire,
        const std::size_t minimumSamples,
        const double deflection)
    {
        std::vector<gp_Pnt> result;
        for (BRepTools_WireExplorer explorer(wire); explorer.More(); explorer.Next())
        {
            auto edgePoints = SampleEdge(
                TopoDS::Edge(explorer.Current()),
                minimumSamples,
                deflection,
                true);
            if (edgePoints.empty())
                continue;
            if (!result.empty() && result.back().Distance(edgePoints.front()) <= Precision::Confusion())
                edgePoints.erase(edgePoints.begin());
            result.insert(result.end(), edgePoints.begin(), edgePoints.end());
        }
        if (result.size() > 2 && result.front().Distance(result.back()) > Precision::Confusion())
            result.push_back(result.front());
        return result;
    }

    bool IsGeometricallyStraight(
        const std::span<const gp_Pnt> samples,
        const double distanceTolerance,
        const double angularToleranceRadians)
    {
        if (samples.size() < 2)
            return false;

        const auto start = samples.front();
        const auto finish = samples.back();
        const gp_Vec baseline(start, finish);
        if (baseline.SquareMagnitude() <= distanceTolerance * distanceTolerance)
            return false;

        const gp_Dir direction(baseline);
        for (const auto& point : samples)
        {
            const gp_Vec offset(start, point);
            const auto transverse = offset.Crossed(gp_Vec(direction)).Magnitude();
            if (transverse > distanceTolerance)
                return false;
        }

        const auto minimumDot = std::cos(angularToleranceRadians);
        for (std::size_t index = 1; index < samples.size(); ++index)
        {
            const gp_Vec tangent(samples[index - 1], samples[index]);
            if (tangent.SquareMagnitude() <= distanceTolerance * distanceTolerance)
                continue;
            if (std::abs(gp_Dir(tangent).Dot(direction)) < minimumDot)
                return false;
        }
        return true;
    }

    double FaceArea(const TopoDS_Face& face)
    {
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties);
        return properties.Mass();
    }

    double EdgeLength(const TopoDS_Edge& edge)
    {
        GProp_GProps properties;
        BRepGProp::LinearProperties(edge, properties);
        return properties.Mass();
    }

    gp_Pnt2d ToSection2d(const gp_Pnt& point, const gp_Ax3& frame)
    {
        const gp_Vec offset(frame.Location(), point);
        return {
            offset.Dot(gp_Vec(frame.XDirection())),
            offset.Dot(gp_Vec(frame.YDirection()))
        };
    }

    gp_Pnt FromSection2d(
        const gp_Pnt2d& point,
        const double station,
        const gp_Ax3& frame)
    {
        return frame.Location().Translated(
            gp_Vec(frame.XDirection()) * point.X()
            + gp_Vec(frame.YDirection()) * point.Y()
            + gp_Vec(frame.Direction()) * station);
    }

    double AxialStation(const gp_Pnt& point, const gp_Ax3& frame)
    {
        return gp_Vec(frame.Location(), point).Dot(gp_Vec(frame.Direction()));
    }

    std::optional<gp_Dir> SafeDirection(const gp_Vec& vector)
    {
        if (vector.SquareMagnitude() <= kTiny)
            return std::nullopt;
        return gp_Dir(vector);
    }

    double DirectionAbsDot(const gp_Dir& left, const gp_Dir& right)
    {
        return std::abs(left.Dot(right));
    }

    std::vector<gp_Pnt> ResamplePolyline(
        const std::span<const gp_Pnt> points,
        const std::size_t count)
    {
        std::vector<gp_Pnt> result;
        if (points.empty() || count == 0)
            return result;
        if (points.size() == 1)
        {
            result.assign(count, points.front());
            return result;
        }

        std::vector<double> stations(points.size(), 0.0);
        for (std::size_t index = 1; index < points.size(); ++index)
            stations[index] = stations[index - 1] + points[index - 1].Distance(points[index]);
        const auto total = stations.back();
        if (total <= kTiny)
        {
            result.assign(count, points.front());
            return result;
        }

        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto target = count == 1
                ? 0.0
                : total * static_cast<double>(index) / static_cast<double>(count - 1);
            const auto upper = std::lower_bound(stations.begin(), stations.end(), target);
            if (upper == stations.begin())
            {
                result.push_back(points.front());
                continue;
            }
            if (upper == stations.end())
            {
                result.push_back(points.back());
                continue;
            }
            const auto rightIndex = static_cast<std::size_t>(upper - stations.begin());
            const auto leftIndex = rightIndex - 1;
            const auto span = stations[rightIndex] - stations[leftIndex];
            const auto ratio = span <= kTiny ? 0.0 : (target - stations[leftIndex]) / span;
            result.push_back(points[leftIndex].Translated(
                gp_Vec(points[leftIndex], points[rightIndex]) * ratio));
        }
        return result;
    }

    double PointPolylineDistance2d(
        const gp_Pnt2d& point,
        const std::span<const gp_Pnt2d> polyline,
        std::size_t* segmentIndex,
        double* segmentParameter)
    {
        auto best = (std::numeric_limits<double>::max)();
        std::size_t bestIndex = 0;
        double bestParameter = 0.0;
        if (polyline.size() < 2)
            return best;

        for (std::size_t index = 1; index < polyline.size(); ++index)
        {
            const auto& left = polyline[index - 1];
            const auto& right = polyline[index];
            const auto dx = right.X() - left.X();
            const auto dy = right.Y() - left.Y();
            const auto lengthSquared = dx * dx + dy * dy;
            auto parameter = lengthSquared <= kTiny
                ? 0.0
                : ((point.X() - left.X()) * dx + (point.Y() - left.Y()) * dy)
                    / lengthSquared;
            parameter = std::clamp(parameter, 0.0, 1.0);
            const auto x = left.X() + parameter * dx;
            const auto y = left.Y() + parameter * dy;
            const auto distance = std::hypot(point.X() - x, point.Y() - y);
            if (distance < best)
            {
                best = distance;
                bestIndex = index - 1;
                bestParameter = parameter;
            }
        }

        if (segmentIndex) *segmentIndex = bestIndex;
        if (segmentParameter) *segmentParameter = bestParameter;
        return best;
    }

    bool PointInPolygonEvenOdd(
        const gp_Pnt2d& point,
        const std::span<const gp_Pnt2d> polygon)
    {
        if (polygon.size() < 3)
            return false;

        bool inside = false;
        auto previous = polygon.size() - 1;
        for (std::size_t current = 0; current < polygon.size(); ++current)
        {
            const auto& a = polygon[current];
            const auto& b = polygon[previous];
            const auto crosses = (a.Y() > point.Y()) != (b.Y() > point.Y());
            if (crosses)
            {
                const auto denominator = b.Y() - a.Y();
                const auto x = a.X()
                    + (point.Y() - a.Y()) * (b.X() - a.X()) / denominator;
                if (point.X() < x)
                    inside = !inside;
            }
            previous = current;
        }
        return inside;
    }

    gp_Dir EstimateFeedDirection(
        const std::span<const gp_Pnt> points,
        const std::size_t index,
        const gp_Dir& fallback)
    {
        if (points.size() < 2)
            return fallback;
        const auto left = index == 0 ? 0 : index - 1;
        const auto right = index + 1 >= points.size() ? points.size() - 1 : index + 1;
        const gp_Vec tangent(points[left], points[right]);
        return SafeDirection(tangent).value_or(fallback);
    }
}
