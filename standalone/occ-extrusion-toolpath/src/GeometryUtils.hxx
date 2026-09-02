#pragma once

#include "etp/Types.hxx"

#include <Geom_Surface.hxx>
#include <gp_Pnt2d.hxx>

#include <optional>
#include <span>
#include <vector>

namespace etp::detail
{
    [[nodiscard]] std::vector<gp_Pnt> SampleEdge(
        const TopoDS_Edge& edge,
        std::size_t minimumSamples,
        double deflection,
        bool respectOrientation = true);

    [[nodiscard]] std::vector<gp_Pnt> SampleWire(
        const TopoDS_Wire& wire,
        std::size_t minimumSamples,
        double deflection);

    [[nodiscard]] bool IsGeometricallyStraight(
        std::span<const gp_Pnt> samples,
        double distanceTolerance,
        double angularToleranceRadians);

    [[nodiscard]] double FaceArea(const TopoDS_Face& face);
    [[nodiscard]] double EdgeLength(const TopoDS_Edge& edge);

    [[nodiscard]] gp_Pnt2d ToSection2d(const gp_Pnt& point, const gp_Ax3& frame);
    [[nodiscard]] gp_Pnt FromSection2d(
        const gp_Pnt2d& point,
        double station,
        const gp_Ax3& frame);
    [[nodiscard]] double AxialStation(const gp_Pnt& point, const gp_Ax3& frame);

    [[nodiscard]] std::optional<gp_Dir> SafeDirection(const gp_Vec& vector);
    [[nodiscard]] double DirectionAbsDot(const gp_Dir& left, const gp_Dir& right);

    [[nodiscard]] std::vector<gp_Pnt> ResamplePolyline(
        std::span<const gp_Pnt> points,
        std::size_t count);

    [[nodiscard]] double PointPolylineDistance2d(
        const gp_Pnt2d& point,
        std::span<const gp_Pnt2d> polyline,
        std::size_t* segmentIndex = nullptr,
        double* segmentParameter = nullptr);

    [[nodiscard]] bool PointInPolygonEvenOdd(
        const gp_Pnt2d& point,
        std::span<const gp_Pnt2d> polygon);

    [[nodiscard]] gp_Dir EstimateFeedDirection(
        std::span<const gp_Pnt> points,
        std::size_t index,
        const gp_Dir& fallback);
}

