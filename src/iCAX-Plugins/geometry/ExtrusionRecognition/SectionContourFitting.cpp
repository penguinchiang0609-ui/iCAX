#include "pch.h"
#include "SectionContourFitting.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <ElCLib.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2dConvert.hxx>
#include <Geom2dConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GeomAPI.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <GeomConvert_CurveToAnaCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Line.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <gp_Circ2d.hxx>
#include <gp_Elips.hxx>
#include <gp_Elips2d.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec2d.hxx>
#include <math_Gauss.hxx>
#include <math_Matrix.hxx>
#include <math_Vector.hxx>
#include <numbers>

namespace iCAX::ExtrusionRecognition
{
    namespace
    {
        using P = gp_Pnt2d;
        using Bezier = Handle(Geom2d_BezierCurve);
        const gp_Pln XY(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));

        struct Source
        {
            Handle(Geom2d_Curve) curve;
            double first = 0, last = 0;
            int wire = 0, index = 0;
            std::size_t inputEdgeIndex = 0;
            bool entireEdge = true;
            bool reverse = false;
            std::vector<Bezier> pieces;
            std::vector<int> sourceIndices;
            std::vector<std::string> originalKinds;
            std::string fitKind = "original";
            double fitBound = 0;
        };

        struct Domain
        {
            double tol = Precision::Confusion();
            std::vector<Source> sources;
            std::vector<Source> referenceSources;
        };

        double cross(P a, P b, P c)
        {
            return (b.X() - a.X()) * (c.Y() - a.Y()) - (b.Y() - a.Y()) * (c.X() - a.X());
        }
        double segmentDistance(P p, P a, P b)
        {
            gp_Vec2d v(a, b);
            double n = v.SquareMagnitude();
            double t = n ? std::clamp(gp_Vec2d(a, p).Dot(v) / n, 0., 1.) : 0.;
            return p.Distance(a.Translated(v * t));
        }
        std::vector<P> hull(std::vector<P> p)
        {
            std::sort(p.begin(), p.end(), [](P a, P b) { return a.X() < b.X() || (a.X() == b.X() && a.Y() < b.Y()); });
            p.erase(std::unique(p.begin(), p.end(), [](P a, P b) { return a.IsEqual(b, 0); }), p.end());
            if (p.size() < 3)
            {
                return p;
            }
            std::vector<P> h;
            for (auto x : p)
            {
                while (h.size() > 1 && cross(h[h.size() - 2], h.back(), x) <= 0)
                {
                    h.pop_back();
                }
                h.push_back(x);
            }
            auto k = h.size();
            for (int i = int(p.size()) - 2; i >= 0; --i)
            {
                while (h.size() > k && cross(h[h.size() - 2], h.back(), p[i]) <= 0)
                {
                    h.pop_back();
                }
                h.push_back(p[i]);
            }
            h.pop_back();
            return h;
        }
        double hullDistance(P p, const std::vector<P>& h)
        {
            if (h.size() == 1)
            {
                return h[0].Distance(p);
            }
            bool inside = h.size() > 2;
            double d = 1.e100;
            for (size_t i = 0; i < h.size(); ++i)
            {
                auto a = h[i], b = h[(i + 1) % h.size()];
                inside &= cross(a, b, p) >= 0;
                d = std::min(d, segmentDistance(p, a, b));
            }
            return inside ? 0 : d;
        }
        std::vector<P> poles(Bezier b)
        {
            std::vector<P> v;
            for (int i = 1; i <= b->NbPoles(); ++i)
            {
                if (b->Weight(i) <= 0)
                {
                    throw std::runtime_error("non-positive rational Bezier weight");
                }
                v.push_back(b->Pole(i));
            }
            return v;
        }
        std::pair<Bezier, Bezier> halves(Bezier b)
        {
            auto l = occ::down_cast<Geom2d_BezierCurve>(b->Copy()), r = occ::down_cast<Geom2d_BezierCurve>(b->Copy());
            l->Segment(0, .5);
            r->Segment(.5, 1);
            return {l, r};
        }

        // Certify the fitting tolerance on the complete positive-weight Bezier curves.
        // The source section and its topology tolerances stay in the Contour result.
        struct AnalyticMetric
        {
            int kind = 0;
            P origin, a, b;
            gp_Dir2d x {1, 0}, y {0, 1};
            double major = 1, minor = 1, first = 0, last = 0;
        };
        AnalyticMetric metric(const occ::handle<Geom2d_Curve>& curve, double first, double last)
        {
            AnalyticMetric m;
            Geom2dAdaptor_Curve a(curve, first, last);
            m.first = first;
            m.last = last;
            if (a.GetType() == GeomAbs_Line)
            {
                m.kind = 1;
                m.a = curve->Value(first);
                m.b = curve->Value(last);
            }
            else if (a.GetType() == GeomAbs_Circle)
            {
                auto c = a.Circle();
                m.kind = 2;
                m.origin = c.Location();
                m.x = c.Position().XDirection();
                m.y = c.Position().YDirection();
                m.major = m.minor = c.Radius();
            }
            else if (a.GetType() == GeomAbs_Ellipse)
            {
                auto c = a.Ellipse();
                m.kind = 3;
                m.origin = c.Location();
                m.x = c.XAxis().Direction();
                m.y = c.YAxis().Direction();
                m.major = c.MajorRadius();
                m.minor = c.MinorRadius();
            }
            return m;
        }
        P normalized(P p, const AnalyticMetric& m)
        {
            gp_Vec2d v(m.origin, p);
            return P(v.Dot(gp_Vec2d(m.x)) / m.major, v.Dot(gp_Vec2d(m.y)) / m.minor);
        }
        bool certifyPiece(Bezier b, const AnalyticMetric& m, double tolerance, double& maximum, double& sweep,
                          int depth = 0)
        {
            auto controls = poles(b);
            double bound = 0, angle = 0;
            if (m.kind == 1)
            {
                for (auto p : controls)
                {
                    bound = std::max(bound, segmentDistance(p, m.a, m.b));
                }
            }
            else
            {
                std::vector<P> ps;
                double hi = 0;
                for (auto p : controls)
                {
                    auto q = normalized(p, m);
                    ps.push_back(q);
                    hi = std::max(hi, q.Distance(P(0, 0)));
                }
                double lo = hullDistance(P(0, 0), hull(ps));
                bound = m.major * std::max(std::abs(hi - 1), std::abs(lo - 1));
                auto a = normalized(b->StartPoint(), m), z = normalized(b->EndPoint(), m);
                angle = std::atan2(a.X() * z.Y() - a.Y() * z.X(), a.X() * z.X() + a.Y() * z.Y());
                if (lo <= 0)
                {
                    bound = std::max(bound, tolerance * 2);
                }
            }
            if (bound <= tolerance)
            {
                maximum = std::max(maximum, bound);
                sweep += angle;
                return true;
            }
            // Reject using actual curve endpoints before subdividing the control hull.
            for (auto p : {b->StartPoint(), b->EndPoint()})
            {
                double lower = m.kind == 1 ? segmentDistance(p, m.a, m.b)
                                           : m.minor * std::abs(normalized(p, m).Distance(P(0, 0)) - 1);
                if (lower > tolerance)
                {
                    return false;
                }
            }
            if (depth >= 26)
            {
                return false;
            }
            auto [l, r] = halves(b);
            return certifyPiece(l, m, tolerance, maximum, sweep, depth + 1) &&
                   certifyPiece(r, m, tolerance, maximum, sweep, depth + 1);
        }
        std::vector<Bezier> orientedPieces(const Source& s)
        {
            auto out = s.pieces;
            if (s.reverse)
            {
                std::reverse(out.begin(), out.end());
                for (auto& b : out)
                {
                    b = occ::down_cast<Geom2d_BezierCurve>(b->Copy());
                    b->Reverse();
                }
            }
            return out;
        }
        occ::handle<Geom_BoundedCurve> curve3d(const Source& s)
        {
            auto curve =
                occ::handle<Geom_TrimmedCurve>(new Geom_TrimmedCurve(GeomAPI::To3d(s.curve, XY), s.first, s.last));
            if (s.reverse)
            {
                curve->Reverse();
            }
            return curve;
        }
        std::string originalKind(const Source& s)
        {
            auto t = Geom2dAdaptor_Curve(s.curve, s.first, s.last).GetType();
            return t == GeomAbs_Line      ? "line"
                   : t == GeomAbs_Circle  ? "circle"
                   : t == GeomAbs_Ellipse ? "ellipse"
                                          : "spline";
        }
        occ::handle<Geom_Curve> ellipseCandidate(const std::vector<Source>& sources, double& first, double& last)
        {
            // These points propose a conic only. Acceptance still uses the full original
            // Bezier control-hull bound in certifyPiece, not a sampled residual.
            std::vector<P> points;
            P center(0, 0);
            for (auto& s : sources)
            {
                for (int i = 0; i <= 8; i++)
                {
                    double t = double(i) / 8;
                    if (s.reverse)
                    {
                        t = 1 - t;
                    }
                    auto p = s.curve->Value(s.first + t * (s.last - s.first));
                    points.push_back(p);
                    center.SetX(center.X() + p.X());
                    center.SetY(center.Y() + p.Y());
                }
            }
            center.SetX(center.X() / points.size());
            center.SetY(center.Y() / points.size());
            double scale = 0;
            for (auto p : points)
            {
                scale = std::max(scale, p.Distance(center));
            }
            if (scale <= Precision::Confusion())
            {
                return {};
            }
            math_Matrix matrix(1, 5, 1, 5, 0.);
            math_Vector rhs(1, 5, 0.), solution(1, 5);
            for (auto p : points)
            {
                double x = (p.X() - center.X()) / scale, y = (p.Y() - center.Y()) / scale;
                double f[5] = {x * x, x * y, y * y, x, y};
                for (int i = 0; i < 5; i++)
                {
                    rhs(i + 1) += f[i];
                    for (int j = 0; j < 5; j++)
                    {
                        matrix(i + 1, j + 1) += f[i] * f[j];
                    }
                }
            }
            math_Gauss solver(matrix);
            if (!solver.IsDone())
            {
                return {};
            }
            solver.Solve(rhs, solution);
            double a = solution(1), b = solution(2) / 2, c = solution(3), d = solution(4), e = solution(5),
                   det = a * c - b * b;
            if (det <= 0)
            {
                return {};
            }
            double cx = -(c * d - b * e) / (2 * det), cy = -(-b * d + a * e) / (2 * det),
                   k = 1 + a * cx * cx + 2 * b * cx * cy + c * cy * cy, delta = std::hypot(a - c, 2 * b),
                   lo = (a + c - delta) / 2, hi = (a + c + delta) / 2;
            if (lo <= 0 || k <= 0)
            {
                return {};
            }
            double major = scale * std::sqrt(k / lo), minor = scale * std::sqrt(k / hi),
                   angle = .5 * std::atan2(2 * b, a - c) + std::numbers::pi / 2;
            P origin(center.X() + cx * scale, center.Y() + cy * scale);
            auto bs = orientedPieces(sources.front());
            P point;
            gp_Vec2d tangent;
            bs.front()->D1(0, point, tangent);
            double sense = gp_Vec2d(origin, point).Crossed(tangent) >= 0 ? 1 : -1;
            gp_Elips ellipse(gp_Ax2(gp_Pnt(origin.X(), origin.Y(), 0), gp_Dir(0, 0, sense),
                                    gp_Dir(std::cos(angle), std::sin(angle), 0)),
                             major, minor);
            first = ElCLib::Parameter(ellipse, gp_Pnt(points[0].X(), points[0].Y(), 0));
            double sweep = 0, previous = first;
            for (size_t i = 1; i < points.size(); i++)
            {
                double u = ElCLib::Parameter(ellipse, gp_Pnt(points[i].X(), points[i].Y(), 0));
                sweep += std::remainder(u - previous, 2 * std::numbers::pi);
                previous = u;
            }
            last = first + sweep;
            if (last <= first)
            {
                return {};
            }
            return new Geom_Ellipse(ellipse);
        }
        std::optional<Source> fitAnalytic(const std::vector<Source>& originals, double tolerance, int lastKind = 3)
        {
            GeomConvert_CompCurveToBSplineCurve joined(curve3d(originals[0]));
            for (size_t i = 1; i < originals.size(); i++)
            {
                if (!joined.Add(curve3d(originals[i]), tolerance, true, false, std::numeric_limits<int>::max()))
                {
                    return std::nullopt;
                }
            }
            auto raw = joined.BSplineCurve();
            std::vector<Bezier> reference;
            std::vector<int> origins;
            for (auto& s : originals)
            {
                auto bs = orientedPieces(s);
                reference.insert(reference.end(), bs.begin(), bs.end());
                if (s.sourceIndices.empty())
                {
                    origins.push_back(s.index);
                }
                else
                {
                    origins.insert(origins.end(), s.sourceIndices.begin(), s.sourceIndices.end());
                }
            }
            for (int kind = 1; kind <= lastKind; kind++)
            {
                double first = 0, last = 0, gap = 0;
                occ::handle<Geom_Curve> result;
                try
                {
                    if (kind == 1)
                    {
                        result = GeomConvert_CurveToAnaCurve::ComputeLine(raw, tolerance, raw->FirstParameter(),
                                                                          raw->LastParameter(), first, last, gap);
                    }
                    else if (kind == 2)
                    {
                        result = GeomConvert_CurveToAnaCurve::ComputeCircle(raw, tolerance, raw->FirstParameter(),
                                                                            raw->LastParameter(), first, last, gap);
                    }
                    else
                    {
                        result = GeomConvert_CurveToAnaCurve::ComputeEllipse(raw, tolerance, raw->FirstParameter(),
                                                                             raw->LastParameter(), first, last, gap);
                        if (result.IsNull())
                        {
                            result = ellipseCandidate(originals, first, last);
                        }
                    }
                }
                catch (const Standard_Failure&)
                {
                    continue;
                }
                if (result.IsNull() || !(last > first))
                {
                    continue;
                }
                auto curve = GeomAPI::To2d(result, XY);
                auto m = metric(curve, first, last);
                if (!m.kind)
                {
                    continue;
                }
                double maximum = 0, sweep = 0;
                bool valid = true;
                for (auto b : reference)
                {
                    if (!certifyPiece(b, m, tolerance, maximum, sweep))
                    {
                        valid = false;
                        break;
                    }
                }

                double endpointBound = std::max(reference.front()->StartPoint().Distance(curve->Value(first)),
                                                reference.back()->EndPoint().Distance(curve->Value(last)));
                if (!valid || endpointBound > tolerance)
                {
                    continue;
                }
                maximum = std::max(maximum, endpointBound);
                if (m.kind != 1 && std::abs(sweep - (last - first)) * m.minor > 2 * tolerance)
                {
                    continue;
                }
                Source out = originals[0];
                out.curve = curve;
                out.first = first;
                out.last = last;
                out.reverse = false;
                out.pieces.clear();
                out.sourceIndices = origins;
                out.originalKinds.clear();
                for (auto& s : originals)
                {
                    out.originalKinds.push_back(originalKind(s));
                }
                out.fitKind = kind == 1 ? "line" : kind == 2 ? "circle" : "ellipse";
                out.fitBound = maximum;
                auto bs = Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(curve, first, last));
                Geom2dConvert_BSplineCurveToBezierCurve bz(bs);
                for (int i = 1; i <= bz.NbArcs(); i++)
                {
                    out.pieces.push_back(bz.Arc(i));
                }
                return out;
            }
            return std::nullopt;
        }
        bool facetedCircleChain(const std::vector<Source>& chain, double tolerance)
        {
            // This recognizes the vertices of a polygonized round transition only.
            // The actual line segments are retained exactly as a C0 composite spline;
            // the guide circle never replaces their geometry or contributes fit error.
            std::vector<P> vertices;
            for (auto& s : chain)
            {
                auto ps = orientedPieces(s);
                if (vertices.empty())
                {
                    vertices.push_back(ps.front()->StartPoint());
                }
                vertices.push_back(ps.back()->EndPoint());
            }
            if (vertices.front().Distance(vertices.back()) <= tolerance)
            {
                vertices.pop_back();
            }
            if (vertices.size() < 5)
            {
                return false;
            }
            P origin = vertices[0];
            double scale = 0;
            for (auto p : vertices)
            {
                scale = std::max(scale, p.Distance(origin));
            }
            if (scale <= tolerance)
            {
                return false;
            }
            math_Matrix matrix(1, 3, 1, 3, 0.);
            math_Vector rhs(1, 3, 0.), solution(1, 3);
            for (auto p : vertices)
            {
                double x = (p.X() - origin.X()) / scale, y = (p.Y() - origin.Y()) / scale, f[3] = {x, y, 1},
                       z = -(x * x + y * y);
                for (int i = 0; i < 3; i++)
                {
                    rhs(i + 1) += f[i] * z;
                    for (int j = 0; j < 3; j++)
                    {
                        matrix(i + 1, j + 1) += f[i] * f[j];
                    }
                }
            }
            math_Gauss solver(matrix);
            if (!solver.IsDone())
            {
                return false;
            }
            solver.Solve(rhs, solution);
            P center(origin.X() - scale * solution(1) / 2, origin.Y() - scale * solution(2) / 2);
            double r2 = (solution(1) * solution(1) + solution(2) * solution(2)) / 4 - solution(3);
            if (r2 <= 0)
            {
                return false;
            }
            double radius = scale * std::sqrt(r2);
            for (auto p : vertices)
            {
                if (std::abs(p.Distance(center) - radius) > tolerance)
                {
                    return false;
                }
            }
            int sign = 0;
            for (size_t i = 1; i + 1 < vertices.size(); i++)
            {
                double turn = cross(vertices[i - 1], vertices[i], vertices[i + 1]);
                if (std::abs(turn) <= tolerance * vertices[i - 1].Distance(vertices[i + 1]))
                {
                    return false;
                }
                int current = turn > 0 ? 1 : -1;
                if (sign && current != sign)
                {
                    return false;
                }
                sign = current;
            }
            return true;
        }
        void groupFacetedTransitions(std::vector<Source>& output, const Domain& d, double tolerance)
        {
            for (bool changed = true; changed;)
            {
                changed = false;
                size_t bestStart = 0, bestCount = 0;
                std::vector<size_t> best;
                for (size_t start = 0; start < output.size(); start++)
                {
                    size_t first = start, last = start + 1;
                    while (first && output[first - 1].wire == output[start].wire)
                    {
                        --first;
                    }
                    while (last < output.size() && output[last].wire == output[start].wire)
                    {
                        ++last;
                    }
                    std::vector<Source> chain;
                    std::vector<size_t> indices;
                    for (size_t n = 0; n < last - first; n++)
                    {
                        size_t at = first + (start - first + n) % (last - first);
                        auto& s = output[at];
                        if (s.fitKind != "line" || !std::all_of(s.originalKinds.begin(), s.originalKinds.end(),
                                                                [](const auto& k) { return k == "line"; }))
                        {
                            break;
                        }
                        chain.push_back(s);
                        indices.push_back(at);
                        if (chain.size() >= 4 && chain.size() > bestCount && facetedCircleChain(chain, tolerance))
                        {
                            bestStart = start;
                            bestCount = chain.size();
                            best = indices;
                        }
                    }
                }
                if (bestCount < 4)
                {
                    break;
                }
                std::vector<int> ids;
                for (size_t at : best)
                {
                    for (int id : output[at].sourceIndices)
                    {
                        ids.push_back(id);
                    }
                }
                GeomConvert_CompCurveToBSplineCurve joined(curve3d(d.sources[ids[0]]));
                bool okay = true;
                double gap = 0;
                for (size_t i = 1; i < ids.size(); i++)
                {
                    auto a = orientedPieces(d.sources[ids[i - 1]]), b = orientedPieces(d.sources[ids[i]]);
                    gap = std::max(gap, a.back()->EndPoint().Distance(b.front()->StartPoint()));
                    okay &= joined.Add(curve3d(d.sources[ids[i]]), d.tol, true, false, std::numeric_limits<int>::max());
                }
                if (!okay)
                {
                    break;
                }
                Source merged = output[bestStart];
                auto curve = joined.BSplineCurve();
                merged.curve = GeomAPI::To2d(curve, XY);
                merged.first = curve->FirstParameter();
                merged.last = curve->LastParameter();
                merged.reverse = false;
                merged.fitKind = "spline";
                merged.fitBound = gap;
                merged.sourceIndices = ids;
                merged.originalKinds.assign(ids.size(), "line");
                merged.pieces.clear();
                for (int id : ids)
                {
                    for (auto b : orientedPieces(d.sources[id]))
                    {
                        merged.pieces.push_back(b);
                    }
                }
                // A cyclic group is emitted at its last ordered slot; the next source then
                // remains its geometric successor, including a group crossing the seam.
                size_t insertion = *std::max_element(best.begin(), best.end());
                std::set<size_t> removed(best.begin(), best.end());
                std::vector<Source> next;
                for (size_t i = 0; i < output.size(); i++)
                {
                    if (i == insertion)
                    {
                        next.push_back(merged);
                    }
                    else if (!removed.count(i))
                    {
                        next.push_back(output[i]);
                    }
                }
                output = std::move(next);
                changed = true;
            }
        }
        void fitSources(Domain& d, double tolerance)
        {
            // A composite spline may contain genuine straight spans and curved spans.
            // Expose that existing change of geometry without exploding a C0 polyline
            // (all spans straight) back into thousands of independent supports.
            std::vector<Source> intrinsic;
            for (auto s : d.sources)
            {
                bool straight = false, curved = false;
                for (auto b : s.pieces)
                {
                    double bound = 0;
                    for (auto p : poles(b))
                    {
                        bound = std::max(bound, segmentDistance(p, b->StartPoint(), b->EndPoint()));
                    }
                    bool line = b->StartPoint().Distance(b->EndPoint()) > d.tol && bound <= d.tol;
                    straight |= line;
                    curved |= !line;
                }
                if (straight && curved && s.pieces.size() > 1)
                {
                    for (auto b : orientedPieces(s))
                    {
                        Source part = s;
                        part.curve = b;
                        part.first = 0;
                        part.last = 1;
                        part.reverse = false;
                        part.entireEdge = false;
                        part.pieces = {b};
                        intrinsic.push_back(part);
                    }
                }
                else
                {
                    intrinsic.push_back(s);
                }
            }
            for (size_t i = 0; i < intrinsic.size(); i++)
            {
                intrinsic[i].index = int(i);
            }
            d.sources = std::move(intrinsic);
            d.referenceSources = d.sources;
            // Preserve faceted transitions before any tolerance-dependent local circle
            // approximation; this makes structural support ownership scale-consistent.
            std::vector<Source> preliminary = d.sources;
            for (auto& s : preliminary)
            {
                s.sourceIndices = {s.index};
                s.originalKinds = {originalKind(s)};
                s.fitKind = originalKind(s);
            }
            groupFacetedTransitions(preliminary, d, tolerance);
            for (auto& s : preliminary)
            {
                if (s.sourceIndices.size() == 1)
                {
                    s.sourceIndices.clear();
                }
            }
            // Whole-loop recognition precedes fragment fitting. An ellipse split into
            // short spline edges may be ill-conditioned locally but well-defined as a loop.
            std::vector<Source> whole;
            bool allWhole = true;
            for (size_t i = 0; i < preliminary.size();)
            {
                size_t j = i;
                std::vector<Source> wire;
                while (j < preliminary.size() && preliminary[j].wire == preliminary[i].wire)
                {
                    wire.push_back(preliminary[j++]);
                }
                auto fit = fitAnalytic(wire, tolerance);
                if (fit)
                {
                    whole.push_back(*fit);
                }
                else
                {
                    allWhole = false;
                    whole.insert(whole.end(), wire.begin(), wire.end());
                }
                i = j;
            }
            if (allWhole)
            {
                d.sources = std::move(whole);
                for (size_t i = 0; i < d.sources.size(); i++)
                {
                    d.sources[i].index = int(i);
                }
                return;
            }
            // Recognize entire curved runs before short fragments can be mistaken for
            // small circles. Only straightness at native precision separates runs; the
            // much larger fitting allowance must not create false breaks in an ellipse.
            auto nativeStraight = [&](const Source& s) {
                auto bs = orientedPieces(s);
                P a = bs.front()->StartPoint(), b = bs.back()->EndPoint();
                if (a.Distance(b) <= d.tol)
                {
                    return false;
                }
                for (auto piece : bs)
                {
                    for (auto p : poles(piece))
                    {
                        if (segmentDistance(p, a, b) > d.tol)
                        {
                            return false;
                        }
                    }
                }
                return true;
            };
            std::vector<Source> runs;
            for (size_t i = 0; i < whole.size();)
            {
                size_t j = i;
                while (j < whole.size() && whole[j].wire == whole[i].wire)
                {
                    ++j;
                }
                size_t anchor = j;
                for (size_t k = i; k < j; k++)
                {
                    if (nativeStraight(whole[k]))
                    {
                        anchor = k;
                        break;
                    }
                }
                if (anchor == j || j - i == 1)
                {
                    runs.insert(runs.end(), whole.begin() + i, whole.begin() + j);
                    i = j;
                    continue;
                }
                std::vector<Source> curved;
                auto flush = [&]() {
                    if (curved.empty())
                    {
                        return;
                    }
                    auto fit = fitAnalytic(curved, tolerance);
                    if (fit)
                    {
                        runs.push_back(*fit);
                    }
                    else
                    {
                        runs.insert(runs.end(), curved.begin(), curved.end());
                    }
                    curved.clear();
                };
                for (size_t k = 0; k < j - i; k++)
                {
                    auto& s = whole[i + (anchor - i + k) % (j - i)];
                    if (nativeStraight(s))
                    {
                        flush();
                        runs.push_back(s);
                    }
                    else
                    {
                        curved.push_back(s);
                    }
                }
                flush();
                i = j;
            }
            whole = std::move(runs);
            std::vector<Source> output;
            for (auto s : whole)
            {
                if (!s.sourceIndices.empty())
                {
                    output.push_back(s);
                    continue;
                }
                s.sourceIndices = {s.index};
                s.originalKinds = {originalKind(s)};
                s.fitKind = originalKind(s);
                if (s.fitKind == "line")
                {
                    output.push_back(s);
                    continue;
                }
                auto fit = fitAnalytic({s}, tolerance, s.fitKind == "circle" ? 1 : s.fitKind == "ellipse" ? 2 : 3);
                if (fit)
                {
                    output.push_back(*fit);
                }
                else
                {
                    output.push_back(s);
                }
            }
            // Refit merged groups against their original sources, so repeated merging
            // cannot accumulate the allowed approximation error.
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (size_t i = 0; i < output.size(); i++)
                {
                    size_t j = i + 1;
                    if (j == output.size() || output[j].wire != output[i].wire)
                    {
                        j = i;
                        while (j && output[j - 1].wire == output[i].wire)
                        {
                            --j;
                        }
                    }
                    if (j == i || output[i].wire != output[j].wire)
                    {
                        continue;
                    }
                    if (output[i].fitKind == "spline" && output[j].fitKind == "spline")
                    {
                        continue;
                    }
                    auto pa = orientedPieces(output[i]), pb = orientedPieces(output[j]);
                    if (pa.back()->EndPoint().Distance(pb.front()->StartPoint()) > d.tol)
                    {
                        continue;
                    }
                    std::vector<Source> original;
                    for (auto* g : {&output[i], &output[j]})
                    {
                        for (int id : g->sourceIndices)
                        {
                            original.push_back(d.sources[id]);
                        }
                    }
                    auto fit = fitAnalytic(original, tolerance);
                    if (!fit)
                    {
                        continue;
                    }
                    if (j > i)
                    {
                        output[i] = *fit;
                        output.erase(output.begin() + j);
                    }
                    else
                    {
                        output[j] = *fit;
                        output.erase(output.begin() + i);
                    }
                    changed = true;
                    break;
                }
            }
            std::vector<Source> grouped;
            for (size_t i = 0; i < output.size();)
            {
                size_t j = i;
                std::vector<int> ids;
                bool hasLine = false;
                while (j < output.size() && output[j].wire == output[i].wire)
                {
                    hasLine |= output[j].fitKind == "line";
                    for (int id : output[j].sourceIndices)
                    {
                        ids.push_back(id);
                    }
                    ++j;
                }
                if (j - i > 1 && !hasLine)
                {
                    GeomConvert_CompCurveToBSplineCurve joined(curve3d(d.sources[ids[0]]));
                    bool okay = true;
                    double gap = 0;
                    for (size_t k = 1; k < ids.size(); k++)
                    {
                        auto a = orientedPieces(d.sources[ids[k - 1]]), b = orientedPieces(d.sources[ids[k]]);
                        gap = std::max(gap, a.back()->EndPoint().Distance(b.front()->StartPoint()));
                        okay &= joined.Add(curve3d(d.sources[ids[k]]), std::min(tolerance, d.tol), true, false,
                                           std::numeric_limits<int>::max());
                    }
                    if (okay && gap <= tolerance)
                    {
                        Source s = output[i];
                        auto curve = joined.BSplineCurve();
                        s.curve = GeomAPI::To2d(curve, XY);
                        s.first = curve->FirstParameter();
                        s.last = curve->LastParameter();
                        s.reverse = false;
                        s.sourceIndices = ids;
                        s.originalKinds.clear();
                        for (int id : ids)
                        {
                            s.originalKinds.push_back(originalKind(d.sources[id]));
                        }
                        s.fitKind = "spline";
                        s.fitBound = gap;
                        s.pieces.clear();
                        for (int id : ids)
                        {
                            for (auto b : orientedPieces(d.sources[id]))
                            {
                                s.pieces.push_back(b);
                            }
                        }
                        grouped.push_back(s);
                        i = j;
                        continue;
                    }
                }
                grouped.insert(grouped.end(), output.begin() + i, output.begin() + j);
                i = j;
            }
            groupFacetedTransitions(grouped, d, tolerance);
            d.sources = std::move(grouped);
            for (size_t i = 0; i < d.sources.size(); i++)
            {
                d.sources[i].index = int(i);
            }
        }

        std::vector<Bezier> BezierPieces(const Handle(Geom2d_Curve) & curve, double first, double last)
        {
            const auto spline = Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(curve, first, last));
            Geom2dConvert_BSplineCurveToBezierCurve converter(spline);
            std::vector<Bezier> result;
            for (int i = 1; i <= converter.NbArcs(); ++i)
            {
                result.push_back(converter.Arc(i));
            }
            return result;
        }

        double EndGap(const Source& left, const Source& right)
        {
            return left.curve->Value(left.reverse ? left.first : left.last)
                .Distance(right.curve->Value(right.reverse ? right.last : right.first));
        }

    } // namespace

    SSectionContourFit FitSectionContour(const SSectionWiresResult& section, double connectionTolerance)
    {
        SSectionContourFit result;
        if (!section.bSuccess || section.Wires.empty() || !std::isfinite(connectionTolerance) ||
            connectionTolerance <= 0)
        {
            result.Reason = "Contour fitting requires a valid section and connection tolerance";
            return result;
        }

        try
        {
            Domain domain;
            for (std::size_t wireIndex = 0; wireIndex < section.Wires.size(); ++wireIndex)
            {
                const auto& wire = section.Wires[wireIndex];
                if (!wire.bClosed || wire.Edges.empty())
                {
                    throw std::runtime_error("Contour fitting requires a closed wire");
                }
                const auto first = domain.sources.size();
                for (std::size_t edgeIndex = 0; edgeIndex < wire.Edges.size(); ++edgeIndex)
                {
                    const auto& edge = wire.Edges[edgeIndex];
                    Source source;
                    source.curve = iCAX::OpenCascade::BuildOpenCascadeCurve2(edge.Curve);
                    source.first = edge.Range.First;
                    source.last = edge.Range.Last;
                    source.reverse = edge.bReversed;
                    source.wire = static_cast<int>(wireIndex);
                    source.index = static_cast<int>(domain.sources.size());
                    source.inputEdgeIndex = edgeIndex;
                    if (source.curve.IsNull() || !std::isfinite(source.first) || !std::isfinite(source.last) ||
                        source.last <= source.first)
                    {
                        throw std::runtime_error("Invalid original section curve or trim");
                    }
                    source.pieces = BezierPieces(source.curve, source.first, source.last);
                    if (source.pieces.empty())
                    {
                        throw std::runtime_error("Section trim has no Bezier representation");
                    }
                    domain.sources.push_back(std::move(source));
                }
                for (std::size_t i = 0; i < wire.Edges.size(); ++i)
                {
                    const auto gap =
                        EndGap(domain.sources[first + i], domain.sources[first + (i + 1) % wire.Edges.size()]);
                    if (gap > connectionTolerance)
                    {
                        throw std::runtime_error("Original contour is open beyond connection tolerance");
                    }
                    domain.tol = std::max(domain.tol, std::nextafter(gap, std::numeric_limits<double>::infinity()));
                }
            }

            result.Wires.resize(section.Wires.size());
            fitSources(domain, SectionContourFitToleranceMm);
            for (const auto& fitted : domain.sources)
            {
                auto& entry = result.Wires.at(fitted.wire).Curves.emplace_back();
                entry.Kind = fitted.fitKind;
                entry.Curve = fitted.curve;
                entry.First = fitted.first;
                entry.Last = fitted.last;
                entry.Reversed = fitted.reverse;
                entry.DeviationUpperBound = fitted.fitBound;
                for (const int index : fitted.sourceIndices)
                {
                    const auto& source = domain.referenceSources.at(index);
                    entry.Sources.push_back({source.inputEdgeIndex, source.curve, source.first, source.last,
                                             source.reverse, source.entireEdge});
                }
                if (entry.Sources.empty() || !std::isfinite(entry.DeviationUpperBound) ||
                    entry.DeviationUpperBound > SectionContourFitToleranceMm)
                {
                    throw std::runtime_error("Section support lacks a certified fit or source linkage");
                }
            }
            result.bSuccess = true;
        }
        catch (const Standard_Failure& error)
        {
            result.Reason = error.GetMessageString() ? error.GetMessageString() : "Native contour fitting failed";
        }
        catch (const std::exception& error)
        {
            result.Reason = error.what();
        }
        return result;
    }
} // namespace iCAX::ExtrusionRecognition
