namespace multiple_inner_rings
{
Point operator+(Point a, Point b) { return {a[0] + b[0], a[1] + b[1]}; }
Point operator-(Point a, Point b) { return {a[0] - b[0], a[1] - b[1]}; }
Point operator*(Point a, double scale) { return {a[0] * scale, a[1] * scale}; }
double Dot(Point a, Point b) { return a[0] * b[0] + a[1] * b[1]; }
double Cross(Point a, Point b) { return a[0] * b[1] - a[1] * b[0]; }
double Length(Point a) { return std::hypot(a[0], a[1]); }

struct Trim
{
    Curve Shape;
    std::vector<int> SourceIds;
};

struct Boundary
{
    bool Inner;
    double MaterialSide;
    std::vector<Trim> Trims;
};

Point Axis(const Trim& trim)
{
    const Point chord = trim.Shape.End - trim.Shape.Start;
    return chord * (1 / Length(chord));
}

Point Inward(const Boundary& boundary, const Trim& trim)
{
    const auto axis = Axis(trim);
    return Point{-axis[1], axis[0]} * boundary.MaterialSide;
}

Point Tangent(const Trim& trim, bool atEnd)
{
    const auto& curve = trim.Shape;
    if (!curve.IsArc)
        return Axis(trim);
    const auto radial = (atEnd ? curve.End : curve.Start) - curve.Center;
    return Point{-radial[1], radial[0]} * (std::copysign(1.0, curve.Sweep) / Length(radial));
}

bool Parallel(Point a, Point b, double span, double tolerance)
{
    return std::abs(Cross(a, b)) * span <= tolerance;
}

bool Join(Trim& first, const Trim& last, double tolerance)
{
    auto& a = first.Shape;
    const auto& b = last.Shape;
    if (a.IsArc != b.IsArc || Length(a.End - b.Start) > tolerance)
        return false;
    if (a.IsArc)
    {
        if (a.Sweep * b.Sweep <= 0 || Length(a.Center - b.Center) > tolerance ||
            std::abs(a.Radius - b.Radius) > tolerance)
            return false;
        a.Sweep += b.Sweep;
    }
    else
    {
        const auto direction = Axis(first);
        if (Dot(direction, Axis(last)) <= 0 ||
            std::abs(Cross(direction, b.End - a.Start)) > tolerance)
            return false;
    }
    a.End = b.End;
    first.SourceIds.insert(first.SourceIds.end(), last.SourceIds.begin(), last.SourceIds.end());
    return true;
}

std::optional<Boundary> MakeBoundary(const Ring& ring, double tolerance)
{
    if (ring.Curves.empty())
        return std::nullopt;
    const Point origin = ring.Curves.front().Start;
    double twiceArea = 0;
    Boundary boundary{ring.IsInner, 0, {}};
    for (const auto& curve : ring.Curves)
    {
        if ((curve.IsArc ? curve.Radius * std::abs(curve.Sweep) : Length(curve.End - curve.Start)) <= tolerance)
            return std::nullopt;
        twiceArea += curve.IsArc
            ? Cross(curve.Center - origin, curve.End - curve.Start) + curve.Radius * curve.Radius * curve.Sweep
            : Cross(curve.Start - origin, curve.End - origin);
        Trim trim{curve, {curve.Id}};
        if (boundary.Trims.empty() || !Join(boundary.Trims.back(), trim, tolerance))
            boundary.Trims.push_back(std::move(trim));
    }
    if (!std::isfinite(twiceArea) || std::abs(twiceArea) <= 2 * tolerance * tolerance)
        return std::nullopt;
    if (boundary.Trims.size() > 1 && Join(boundary.Trims.back(), boundary.Trims.front(), tolerance))
        boundary.Trims.erase(boundary.Trims.begin());
    for (std::size_t i = 0; i < boundary.Trims.size(); ++i)
        if (Length(boundary.Trims[i].Shape.End - boundary.Trims[(i + 1) % boundary.Trims.size()].Shape.Start) > tolerance)
            return std::nullopt;
    boundary.MaterialSide = std::copysign(1.0, twiceArea) * (ring.IsInner ? -1 : 1);
    return boundary;
}

bool CoversSweep(const TopoDS_Face& material, const std::vector<Point>& polygon, double tolerance)
{
    double perimeter = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i)
        perimeter += Length(polygon[i] - polygon[(i + 1) % polygon.size()]);
    const auto band = MakeCoverageFace(polygon);
    return Area(band) > tolerance * perimeter &&
        Area(Boolean(band, material, BooleanOperation::Cut)) <= tolerance * perimeter;
}

struct Rail
{
    const Boundary* Owner;
    std::size_t Index;

    const Trim& FiniteTrim() const { return Owner->Trims[Index]; }
};

struct WebEnd
{
    Point FirstRoot, LastRoot;
    Point IntoWeb;
    Rail FirstSide, LastSide;
};

std::optional<Rail> FollowRoot(const Rail& start, bool towardEnd, Point intoWeb, double tolerance)
{
    const auto size = start.Owner->Trims.size();
    std::size_t previous = start.Index;
    for (std::size_t count = 1; count < size; ++count)
    {
        const auto index = (previous + (towardEnd ? 1 : size - 1)) % size;
        const auto& before = start.Owner->Trims[previous];
        const auto& next = start.Owner->Trims[index];
        if (before.Shape.IsArc || next.Shape.IsArc)
        {
            const auto a = Tangent(before, towardEnd), b = Tangent(next, !towardEnd);
            const auto& arc = next.Shape.IsArc ? next.Shape : before.Shape;
            if (Dot(a, b) <= 0 || !Parallel(a, b, arc.Radius, tolerance) ||
                arc.Sweep * start.Owner->MaterialSide >= 0)
                return std::nullopt;
        }
        if (!next.Shape.IsArc)
        {
            const auto depth = next.Shape.End - next.Shape.Start;
            if (Dot(depth, intoWeb) * (towardEnd ? 1 : -1) <= tolerance ||
                !Parallel(Axis(next), intoWeb, Length(depth), tolerance))
                return std::nullopt;
            return Rail{start.Owner, index};
        }
        previous = index;
    }
    return std::nullopt;
}

struct SkinBand
{
    Wall Region;
    std::vector<WebEnd> Roots;
};

std::optional<SkinBand> SweepSkin(const TopoDS_Face& material, const Boundary& outer, const Trim& guide,
    const std::vector<Boundary>& holes, double tolerance)
{
    const Point origin = guide.Shape.Start;
    const Point tangent = Axis(guide);
    const Point inward = Inward(outer, guide);
    const double length = Length(guide.Shape.End - origin);
    struct Span
    {
        Rail RailTrim;
        double First, Last, Offset;
    };
    std::vector<Span> supports;
    for (const auto& boundary : holes)
        for (std::size_t i = 0; i < boundary.Trims.size(); ++i)
        {
            const auto& trim = boundary.Trims[i];
            if (trim.Shape.IsArc || Dot(Inward(boundary, trim), inward) >= 0 ||
                !Parallel(Axis(trim), tangent, Length(trim.Shape.End - trim.Shape.Start), tolerance))
                continue;
            Point a = trim.Shape.Start, b = trim.Shape.End;
            double first = Dot(a - origin, tangent), last = Dot(b - origin, tangent);
            if (last < first)
            {
                std::swap(a, b);
                std::swap(first, last);
            }
            const double offset = Dot(a - origin, inward);
            if (offset > tolerance && last > tolerance && first < length - tolerance)
                supports.push_back({{&boundary, i}, first, last, offset});
        }
    std::ranges::sort(supports, {}, &Span::Offset);
    for (std::size_t begin = 0, end = 0; begin < supports.size(); begin = end)
    {
        const double offset = supports[begin].Offset;
        end = begin + 1;
        while (end < supports.size() && supports[end].Offset - offset <= tolerance)
            ++end;
        std::vector<Span> rails(supports.begin() + begin, supports.begin() + end);
        std::ranges::sort(rails, {}, &Span::First);
        if (rails.front().First > tolerance || rails.back().Last < length - tolerance)
            continue;
        SkinBand proof{{"外侧壁", guide.SourceIds, guide.SourceIds,
            {origin, guide.Shape.End, guide.Shape.End + inward * offset, origin + inward * offset}}, {}};
        bool covers = true;
        for (std::size_t i = 0; i < rails.size(); ++i)
        {
            const auto& rail = rails[i];
            const auto& trim = rail.RailTrim.FiniteTrim();
            proof.Region.SupportIds.insert(proof.Region.SupportIds.end(), trim.SourceIds.begin(), trim.SourceIds.end());
            if (i == 0)
                continue;
            const auto& previous = rails[i - 1];
            const double gap = rail.First - previous.Last;
            if (gap <= tolerance || previous.RailTrim.Owner == rail.RailTrim.Owner)
            {
                covers = false;
                break;
            }
            const auto first = FollowRoot(previous.RailTrim,
                Dot(Axis(previous.RailTrim.FiniteTrim()), tangent) > 0, inward, tolerance);
            const auto last = FollowRoot(rail.RailTrim, Dot(Axis(trim), tangent) < 0, inward, tolerance);
            if (!first || !last ||
                Dot(Inward(*first->Owner, first->FiniteTrim()), tangent) <= 0 ||
                Dot(Inward(*last->Owner, last->FiniteTrim()), tangent) >= 0)
            {
                covers = false;
                break;
            }
            proof.Roots.push_back({origin + tangent * previous.Last + inward * offset,
                origin + tangent * rail.First + inward * offset, inward, *first, *last});
        }
        // The band's end is supported by native exit trims. Only the
        // bracketed roots use their shared line between these trims.
        if (covers && CoversSweep(material, proof.Region.Polygon, tolerance))
            return proof;
    }
    return std::nullopt;
}

std::array<int, 2> WebKey(const WebEnd& root)
{
    std::array ids{root.FirstSide.FiniteTrim().SourceIds.front(), root.LastSide.FiniteTrim().SourceIds.front()};
    std::ranges::sort(ids);
    return ids;
}

std::optional<Wall> SweepWeb(const TopoDS_Face& material, const WebEnd& first, WebEnd last, double tolerance)
{
    const auto& one = first.FirstSide.FiniteTrim();
    const auto& two = first.LastSide.FiniteTrim();
    const Point along = first.IntoWeb;
    const Point across = Inward(*first.FirstSide.Owner, one);
    const Point origin = one.Shape.Start;
    const double width = Dot(two.Shape.Start - origin, across);
    const double depth = Dot(last.FirstRoot - first.FirstRoot, along);
    if (width <= tolerance || depth <= tolerance ||
        Dot(along, last.IntoWeb) >= 0 || !Parallel(along, last.IntoWeb, depth, tolerance))
        return std::nullopt;
    const auto interval = [&](const Trim& trim)
    {
        const double a = Dot(trim.Shape.Start - origin, along), b = Dot(trim.Shape.End - origin, along);
        return std::array{std::min(a, b), std::max(a, b)};
    };
    const auto oneRange = interval(one), twoRange = interval(two);
    const double lo = std::max(oneRange[0], twoRange[0]), hi = std::min(oneRange[1], twoRange[1]);
    if (hi - lo <= tolerance || lo < Dot(first.FirstRoot - origin, along) - tolerance ||
        hi > Dot(last.FirstRoot - origin, along) + tolerance ||
        !CoversSweep(material, {origin + along * lo, origin + along * hi,
            origin + along * hi + across * width, origin + along * lo + across * width}, tolerance))
        return std::nullopt;

    // The opposed web skins bound the core. The paired root intervals
    // bound where this web owns its material at both flange joins.
    if (Dot(last.LastRoot - last.FirstRoot, across) < 0)
        std::swap(last.FirstRoot, last.LastRoot);
    auto supports = one.SourceIds;
    supports.insert(supports.end(), two.SourceIds.begin(), two.SourceIds.end());
    return Wall{"内隔壁", supports, supports, {first.FirstRoot, first.LastRoot, last.LastRoot, last.FirstRoot}};
}

std::optional<Plan> BuildPlan(const TopoDS_Face& material, const std::vector<Ring>& rings, double tolerance)
{
    if (material.IsNull() || rings.size() < 3 || !std::isfinite(tolerance) || tolerance <= 0)
        return std::nullopt;
    std::optional<Boundary> outside;
    std::vector<Boundary> holes;
    for (const auto& ring : rings)
    {
        auto boundary = MakeBoundary(ring, tolerance);
        if (!boundary || (!ring.IsInner && outside))
            return std::nullopt;
        if (ring.IsInner)
            holes.push_back(std::move(*boundary));
        else
            outside = std::move(*boundary);
    }
    if (!outside || holes.size() < 2)
        return std::nullopt;

    Plan plan;
    std::map<std::array<int, 2>, std::vector<WebEnd>> roots;
    const auto& contour = outside->Trims;
    std::vector<int> wallIds(contour.size());
    for (std::size_t i = 0; i < contour.size(); ++i)
    {
        const auto& trim = contour[i];
        if (trim.Shape.IsArc)
            continue;
        auto proof = SweepSkin(material, *outside, trim, holes, tolerance);
        if (!proof)
            return std::nullopt;
        for (const auto& root : proof->Roots)
            roots[WebKey(root)].push_back(root);
        plan.Walls.push_back(std::move(proof->Region));
        wallIds[i] = static_cast<int>(plan.Walls.size());
    }
    if (roots.empty())
        return std::nullopt;
    for (const auto& [key, ends] : roots)
    {
        if (ends.size() != 2)
            return std::nullopt;
        auto web = SweepWeb(material, ends.front(), ends.back(), tolerance);
        if (!web)
            return std::nullopt;
        plan.Walls.push_back(std::move(*web));
    }
    for (std::size_t i = 0; i < contour.size(); ++i)
    {
        const auto& round = contour[i];
        if (!round.Shape.IsArc)
            continue;
        const auto prior = (i + contour.size() - 1) % contour.size(), next = (i + 1) % contour.size();
        if (wallIds[prior] == 0 || wallIds[next] == 0 || round.Shape.Sweep * outside->MaterialSide <= 0)
            return std::nullopt;
        std::vector adjacent{wallIds[prior], wallIds[next]};
        std::ranges::sort(adjacent);
        plan.Patches.push_back({round.SourceIds.front(), std::move(adjacent), plan.Walls.size()});
        plan.Walls.push_back({"外侧转角", round.SourceIds, round.SourceIds, {}});
    }
    return plan;
}
}
