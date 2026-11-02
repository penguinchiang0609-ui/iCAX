#pragma once
#include <GeometryData/GeometryData.h>
#include <GeometryData/CurveOffset3D.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace iCAX::GeometryServices::OffsetParameters
{
    inline bool NeedsIndependentQuadrature(const GeometryData::OffsetCurvePiece3& piece)
    {
        using namespace GeometryData;
        if(const auto* law=std::get_if<DirectionalCurveOffset3>(&piece.Geometry))
        {
            if(std::holds_alternative<Clothoid3>(law->Source.Curve))return true;
            if(const auto* field=std::get_if<ParametricVectorField3>(&law->ReferenceDirection))
                return std::visit([](const auto& c){return std::is_same_v<std::decay_t<decltype(c)>,Clothoid3>;},field->Coefficients);
            return false;
        }
        return std::holds_alternative<Clothoid3>(std::get<CurveSegment3>(piece.Geometry).Curve);
    }
    // Enumerate native knot images without changing the caller's unwrapped u.
    // continuity < 0 includes all knots (sampling); otherwise only split knots.
    template<class T> void AppendKnots(const T& c, const GeometryData::ParameterRange& range,
        int continuity, std::vector<double>& out, std::size_t limit)
    {
        using namespace GeometryData;
        const auto append = [&](double u, bool periodicImage=false) {
            const double roundoff=periodicImage?4*std::numeric_limits<double>::epsilon()
                *std::max({std::abs(u),std::abs(range.First),std::abs(range.Last)}):0;
            if (u-range.First > roundoff && range.Last-u > roundoff) {
                if (out.size() >= limit) throw std::length_error("Parameter subdivision budget exceeded");
                out.push_back(u);
            }
        };
        if constexpr (std::is_same_v<T, BSpline3> || std::is_same_v<T, NURBS3>) {
            if (c.Knots.size() < 2 || c.Knots.size() != c.Multiplicities.size())
                throw std::invalid_argument("Invalid knot arrays");
            for (std::size_t k = 0; k < c.Knots.size(); ++k)
                if (!std::isfinite(c.Knots[k]) || (k && c.Knots[k] <= c.Knots[k-1]))
                    throw std::invalid_argument("Knots must increase");
            const double period = c.Knots.back() - c.Knots.front();
            if (!std::isfinite(period)) throw std::invalid_argument("Invalid knot period");
            for (std::size_t k = c.Periodic ? 0 : 1; k + 1 < c.Knots.size(); ++k) {
                if (continuity >= 0 && c.Degree - c.Multiplicities[k] >= continuity) continue;
                if (!c.Periodic) { append(c.Knots[k]); continue; }
                const long double first = std::floor((static_cast<long double>(range.First) - c.Knots[k]) / period);
                const long double last = std::ceil((static_cast<long double>(range.Last) - c.Knots[k]) / period);
                if (!std::isfinite(first) || !std::isfinite(last) || std::abs(first) > 0x1p48L || std::abs(last) > 0x1p48L)
                    throw std::invalid_argument("Periodic parameter loses knot precision");
                if (last-first > static_cast<long double>(limit)) throw std::length_error("Periodic subdivision budget exceeded");
                for (long double turn = first; turn <= last; ++turn)
                    append(static_cast<double>(c.Knots[k] + turn * period),true);
            }
        } else if constexpr (std::is_same_v<T, Polyline3>) {
            // Consecutive point indices are the existing neutral polyline domain.
            for (std::size_t i = 1; i < c.Points.size(); ++i) append(static_cast<double>(i));
        }
    }
}
