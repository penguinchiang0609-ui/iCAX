// Included inside MachiningEdgeAnalysisWork's anonymous namespace after
// SectionCurve and SkinDistance. OCC headers must be included at file scope.
// Support checks below are finite geometric evidence, not continuous error
// certificates. Break parameters come from curve intersections/native support
// changes, never from a distance-to-skin tolerance contour.
namespace skin_spans
{
constexpr double SupportTolerance = 1.0e-7;
constexpr double IntersectionTolerance = 1.0e-7;
constexpr double PointResidualTolerance = 1.0e-9;
constexpr double TangentCosine = 1.0 - 5.0e-9;

struct Projected final
{
    Handle(Geom2d_Curve) Curve;
    Handle(Geom_Curve) Source;
    std::vector<double> NativeBreaks;
    gp_Pnt2d Position;
    bool Collapsed = false;
};

double ParameterTolerance(const double first, const double last)
{
    return std::max(1.0e-12, std::abs(last - first) * 1.0e-11);
}

Projected ProjectEdge(const TopoDS_Edge& edge, const double first, const double last)
{
    Projected result;
    double nativeFirst = 0.0, nativeLast = 0.0;
    result.Source = BRep_Tool::Curve(edge, nativeFirst, nativeLast);
    if (result.Source.IsNull()) return result;
    result.NativeBreaks = { first, last };
    BRepAdaptor_Curve adaptor(edge);
    if (adaptor.GetType() == GeomAbs_BSplineCurve)
    {
        const auto spline = adaptor.BSpline();
        for (int i = 1; i <= spline->NbKnots(); ++i)
            if (spline->Knot(i) > first && spline->Knot(i) < last)
                result.NativeBreaks.push_back(spline->Knot(i));
    }
    std::sort(result.NativeBreaks.begin(), result.NativeBreaks.end());
    result.NativeBreaks.erase(std::unique(result.NativeBreaks.begin(), result.NativeBreaks.end()), result.NativeBreaks.end());
    const auto anchor = result.Source->Value(first);
    result.Position.SetCoord(anchor.Y(), anchor.Z());
    result.Collapsed = true;
    for (int i = 0; i <= 8; ++i)
    {
        gp_Pnt point;
        gp_Vec tangent;
        result.Source->D1(first + (last - first) * double(i) / 8.0, point, tangent);
        if (gp_Pnt2d(point.Y(), point.Z()).Distance(result.Position) > SupportTolerance
            || std::hypot(tangent.Y(), tangent.Z()) > 1.0e-12)
            result.Collapsed = false;
    }
    if (result.Collapsed) return result;
    // X is the extrusion direction; the plane's U/V axes are world Y/Z.
    const gp_Pln plane(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)));
    const Handle(Geom_Curve) trimmed = new Geom_TrimmedCurve(result.Source, first, last);
    const auto projected = GeomProjLib::ProjectOnPlane(trimmed, new Geom_Plane(plane), gp_Dir(1, 0, 0), true);
    if (!projected.IsNull()) result.Curve = GeomAPI::To2d(projected, plane);
    if (!result.Curve.IsNull())
        for (int i = 0; i <= 2; ++i)
        {
            const double parameter = first + (last - first) * double(i) / 2.0;
            const auto source = result.Source->Value(parameter);
            if (result.Curve->Value(parameter).Distance(gp_Pnt2d(source.Y(), source.Z())) > SupportTolerance)
            {
                result.Curve.Nullify();
                break;
            }
        }
    return result;
}

Handle(Geom2d_Curve) Basis(Handle(Geom2d_Curve) curve)
{
    while (!curve.IsNull())
    {
        const auto trimmed = Handle(Geom2d_TrimmedCurve)::DownCast(curve);
        if (trimmed.IsNull()) break;
        curve = trimmed->BasisCurve();
    }
    return curve;
}

struct Nearest final
{
    double Distance = (std::numeric_limits<double>::max)();
    double Parameter = 0.0;
};

Nearest Closest(const gp_Pnt2d& point, const SectionCurve& target, const bool finite)
{
    Nearest result;
    const auto curve = finite ? target.Curve : Basis(target.Curve);
    const double first = finite ? target.First : curve->FirstParameter();
    const double last = finite ? target.Last : curve->LastParameter();
    if (std::isfinite(first) && std::abs(first) < 1.0e90)
        result = { point.Distance(curve->Value(first)), first };
    if (std::isfinite(last) && std::abs(last) < 1.0e90)
    {
        const double distance = point.Distance(curve->Value(last));
        if (distance < result.Distance) result = { distance, last };
    }
    Geom2dAPI_ProjectPointOnCurve project;
    if (finite) project.Init(point, curve, first, last);
    else project.Init(point, curve);
    if (project.NbPoints() && project.LowerDistance() < result.Distance)
        result = { project.LowerDistance(), project.LowerDistanceParameter() };
    return result;
}

bool OnOneSupport(const Handle(Geom2d_Curve)& curve, const SectionCurve& target,
    const double first, const double last, const bool finite)
{
    const auto profile = finite ? target.Curve : Basis(target.Curve);
    for (int i = 0; i <= 18; ++i)
    {
        // Endpoints check finite coverage; open samples check the tangent law.
        const double fraction = i == 0 ? 0.0 : i == 18 ? 1.0 : (double(i) - 0.5) / 17.0;
        gp_Pnt2d point;
        gp_Vec2d tangent;
        curve->D1(first + (last - first) * fraction, point, tangent);
        const auto closest = Closest(point, target, finite);
        if (closest.Distance > SupportTolerance) return false;
        if (i == 0 || i == 18 || tangent.SquareMagnitude() <= 1.0e-24) continue;
        gp_Pnt2d other;
        gp_Vec2d otherTangent;
        profile->D1(closest.Parameter, other, otherTangent);
        if (otherTangent.SquareMagnitude() <= 1.0e-24) return false;
        const double cosine = std::abs(tangent.Dot(otherTangent))
            / std::sqrt(tangent.SquareMagnitude() * otherTangent.SquareMagnitude());
        if (cosine < TangentCosine) return false;
    }
    return true;
}

bool Covered(const Projected& projected, const std::vector<SectionCurve>& section,
    const double first, const double last)
{
    if (projected.Source.IsNull()) return false;
    if (projected.Collapsed) return SkinDistance(projected.Source->Value((first + last) * 0.5), section) <= SupportTolerance;
    if (projected.Curve.IsNull()) return false;
    for (const auto& profile : section)
        if (OnOneSupport(projected.Curve, profile, first, last, true)) return true;
    // A complete skin chain can cross several finite profile primitives.
    // Keep their union; no single quarter-arc need contain a whole circle.
    for (int i = 0; i <= 18; ++i)
    {
        const double fraction = i == 0 ? 0.0 : i == 18 ? 1.0 : (double(i) - 0.5) / 17.0;
        gp_Pnt2d point;
        gp_Vec2d tangent;
        projected.Curve->D1(first + (last - first) * fraction, point, tangent);
        bool covered = false;
        for (const auto& profile : section)
        {
            const auto closest = Closest(point, profile, true);
            if (closest.Distance > SupportTolerance) continue;
            if (i == 0 || i == 18 || tangent.SquareMagnitude() <= 1.0e-24) { covered = true; break; }
            gp_Pnt2d other;
            gp_Vec2d otherTangent;
            profile.Curve->D1(closest.Parameter, other, otherTangent);
            if (otherTangent.SquareMagnitude() <= 1.0e-24) continue;
            const double cosine = std::abs(tangent.Dot(otherTangent))
                / std::sqrt(tangent.SquareMagnitude() * otherTangent.SquareMagnitude());
            if (cosine >= TangentCosine) { covered = true; break; }
        }
        if (!covered) return false;
    }
    return true;
}

bool NativeLaw(const Projected& projected, const std::vector<SectionCurve>& section,
    const double first, const double last)
{
    if (projected.Collapsed) return SkinDistance(projected.Source->Value((first + last) * 0.5), section) <= SupportTolerance;
    if (projected.Curve.IsNull()) return false;
    for (const auto& profile : section)
        if (OnOneSupport(projected.Curve, profile, first, last, false)) return true;
    return false;
}

struct Interval final { double First = 0.0, Last = 0.0; };

double MinimizeContact(const Handle(Geom2d_Curve)& curve, const SectionCurve& target,
    double first, double last)
{
    // An intersector's tangency segment is a numerical tolerance region.
    // Reduce it to its geometric distance minimum, not its tube endpoints.
    constexpr double ratio = 0.6180339887498948482;
    const auto distance = [&](const double t) { return Closest(curve->Value(t), target, true).Distance; };
    const double originalFirst = first, originalLast = last;
    double a = last - ratio * (last - first), b = first + ratio * (last - first);
    double da = distance(a), db = distance(b);
    for (int i = 0; i < 64; ++i)
    {
        if (da <= db) { last = b; b = a; db = da; a = last - ratio * (last - first); da = distance(a); }
        else { first = a; a = b; da = db; b = first + ratio * (last - first); db = distance(b); }
    }
    double best = da <= db ? a : b, bestDistance = std::min(da, db);
    for (const double endpoint : { originalFirst, originalLast })
        if (distance(endpoint) <= bestDistance) { best = endpoint; bestDistance = distance(endpoint); }
    return best;
}

bool RefinePoint(const Handle(Geom2d_Curve)& curve, const SectionCurve& target,
    double& parameter, double otherParameter, const double first, const double last)
{
    for (int i = 0; i < 16; ++i)
    {
        gp_Pnt2d a, b;
        gp_Vec2d ta, tb;
        curve->D1(parameter, a, ta);
        target.Curve->D1(otherParameter, b, tb);
        const gp_Vec2d difference(a, b);
        const double determinant = ta.Crossed(tb);
        if (std::abs(determinant) <= 1.0e-20) break;
        const double step = difference.Crossed(tb) / determinant;
        const double otherStep = difference.Crossed(ta) / determinant;
        parameter = std::clamp(parameter + step, first, last);
        otherParameter = std::clamp(otherParameter + otherStep, target.First, target.Last);
        if (std::abs(step) <= ParameterTolerance(first, last)) break;
    }
    return Closest(curve->Value(parameter), target, true).Distance <= PointResidualTolerance;
}
}

bool SkinSpanOnSupport(const TopoDS_Edge& edge, const std::vector<SectionCurve>& section,
    const double first, const double last)
{
    if (!std::isfinite(first) || !std::isfinite(last) || first >= last) return false;
    const BRepAdaptor_Curve native(edge);
    const auto projected = skin_spans::ProjectEdge(edge, native.FirstParameter(), native.LastParameter());
    bool covered = false;
    for (std::size_t i = 1; i < projected.NativeBreaks.size(); ++i)
    {
        const double begin = std::max(first, projected.NativeBreaks[i - 1]);
        const double end = std::min(last, projected.NativeBreaks[i]);
        if (begin >= end) continue;
        if (!skin_spans::NativeLaw(projected, section, projected.NativeBreaks[i - 1], projected.NativeBreaks[i])
            || !skin_spans::Covered(projected, section, begin, end)) return false;
        covered = true;
    }
    return covered;
}

std::vector<double> CollectSkinBreaks(const TopoDS_Edge& edge, const std::vector<SectionCurve>& section,
    const double first, const double last)
{
    using namespace skin_spans;
    std::vector<double> result{ first, last };
    if (!std::isfinite(first) || !std::isfinite(last) || first >= last) return result;
    const BRepAdaptor_Curve native(edge);
    const auto projected = ProjectEdge(edge, native.FirstParameter(), native.LastParameter());
    if (projected.Collapsed || projected.Curve.IsNull()) return result;
    const double epsilon = ParameterTolerance(first, last);
    std::vector<double> contacts;
    std::vector<Interval> supported;
    for (std::size_t i = 1; i < projected.NativeBreaks.size(); ++i)
    {
        const double begin = std::max(first, projected.NativeBreaks[i - 1]);
        const double end = std::min(last, projected.NativeBreaks[i]);
        if (begin < end && NativeLaw(projected, section, projected.NativeBreaks[i - 1], projected.NativeBreaks[i])
            && Covered(projected, section, begin, end)) supported.push_back({ begin, end });
    }
    const Handle(Geom2d_Curve) finiteSource = new Geom2d_TrimmedCurve(projected.Curve, first, last);
    for (const auto& profile : section)
    {
        const Handle(Geom2d_Curve) finiteProfile = new Geom2d_TrimmedCurve(profile.Curve, profile.First, profile.Last);
        Geom2dAPI_InterCurveCurve intersections(finiteSource, finiteProfile, IntersectionTolerance);
        const auto& intersector = intersections.Intersector();
        if (!intersector.IsDone()) continue;
        for (int i = 1; i <= intersector.NbPoints(); ++i)
        {
            const auto& point = intersector.Point(i);
            double parameter = std::clamp(point.ParamOnFirst(), first, last);
            if (RefinePoint(projected.Curve, profile, parameter, point.ParamOnSecond(), first, last)) contacts.push_back(parameter);
        }
        for (int i = 1; i <= intersector.NbSegments(); ++i)
        {
            const auto& segment = intersector.Segment(i);
            const double a = segment.HasFirstPoint() ? std::clamp(segment.FirstPoint().ParamOnFirst(), first, last) : first;
            const double b = segment.HasLastPoint() ? std::clamp(segment.LastPoint().ParamOnFirst(), first, last) : last;
            const double low = std::min(a, b), high = std::max(a, b);
            bool intervalAccepted = false;
            for (std::size_t j = 1; j < projected.NativeBreaks.size(); ++j)
            {
                const double nativeFirst = projected.NativeBreaks[j - 1], nativeLast = projected.NativeBreaks[j];
                const double begin = std::max(low, nativeFirst), end = std::min(high, nativeLast);
                if (end - begin <= epsilon) continue;
                // A tiny tangency tube cannot establish a shared support law.
                if (OnOneSupport(projected.Curve, profile, nativeFirst, nativeLast, false)
                    && OnOneSupport(projected.Curve, profile, begin, end, true))
                {
                    supported.push_back({ begin, end }); intervalAccepted = true;
                }
            }
            if (!intervalAccepted && high - low > epsilon)
            {
                const double contact = MinimizeContact(projected.Curve, profile, low, high);
                if (Closest(projected.Curve->Value(contact), profile, true).Distance <= PointResidualTolerance)
                    contacts.push_back(contact);
            }
        }
    }
    std::sort(supported.begin(), supported.end(), [](const auto& a, const auto& b) { return a.First < b.First; });
    std::vector<Interval> merged;
    for (const auto& interval : supported)
    {
        if (merged.empty() || interval.First > merged.back().Last + epsilon) merged.push_back(interval);
        else merged.back().Last = std::max(merged.back().Last, interval.Last);
    }
    for (const auto& interval : merged) { result.push_back(interval.First); result.push_back(interval.Last); }
    for (double contact : contacts)
    {
        if (std::abs(contact - first) <= epsilon) contact = first;
        if (std::abs(contact - last) <= epsilon) contact = last;
        // An already-on-skin native endpoint can differ from an approximate
        // source curve's formal intersection by a tiny tangent excursion.
        // Collapse only that support-tolerance contact region, never use it
        // to create a positive-length OnSkin span (NativeLaw still rejects it).
        for (const double anchor : { first, last })
            if (std::abs(contact - anchor) > epsilon
                && SkinDistance(projected.Source->Value(anchor), section) <= SupportTolerance
                && Covered(projected, section, std::min(contact, anchor), std::max(contact, anchor)))
            {
                contact = anchor;
                break;
            }
        bool inSupport = false;
        for (const auto& interval : merged)
            inSupport = inSupport || (contact >= interval.First - epsilon && contact <= interval.Last + epsilon);
        if (!inSupport) result.push_back(contact);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end(), [&](const double a, const double b) { return std::abs(a - b) <= epsilon; }), result.end());
    return result;
}
