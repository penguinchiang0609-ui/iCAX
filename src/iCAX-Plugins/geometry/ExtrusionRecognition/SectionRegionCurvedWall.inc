        using NativeBezier = Handle(Geom2d_BezierCurve);

        std::vector<NativeBezier> NativeBezierSpans(const Handle(Geom2d_Curve)& curve,
            double first, double last, bool reverse = false)
        {
            const auto spline = Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(curve, first, last));
            Geom2dConvert_BSplineCurveToBezierCurve converter(spline);
            std::vector<NativeBezier> spans;
            for (int i = 1; i <= converter.NbArcs(); ++i)
            {
                auto span = converter.Arc(reverse ? converter.NbArcs() + 1 - i : i);
                if (reverse)
                    span->Reverse();
                spans.push_back(std::move(span));
            }
            return spans;
        }

        struct ConvexSkin
        {
            std::vector<NativeBezier> Spans;
            std::vector<gp_Pnt2d> Hull;
            double Orientation = 0;
        };

        std::array<NativeBezier, 2> BisectNativeBezier(const NativeBezier& source)
        {
            std::array<NativeBezier, 2> halves{
                NativeBezier::DownCast(source->Copy()), NativeBezier::DownCast(source->Copy())};
            halves[0]->Segment(0, 0.5);
            halves[1]->Segment(0.5, 1);
            return halves;
        }

        double DistanceToSegment(const gp_Pnt2d& point, const gp_Pnt2d& first, const gp_Pnt2d& last)
        {
            const gp_Vec2d chord(first, last);
            const gp_Vec2d offset(first, point);
            const double squareLength = chord.SquareMagnitude();
            if (squareLength == 0)
                return offset.Magnitude();
            return (offset - chord * std::clamp(offset.Dot(chord) / squareLength, 0.0, 1.0)).Magnitude();
        }

        bool BoundSkinWithChords(const NativeBezier& span, double error,
            std::vector<NativeBezier>& result, int depth = 0)
        {
            bool bounded = true;
            for (int i = 1; i <= span->NbPoles(); ++i)
            {
                if (span->Weight(i) <= 0)
                    return false;
                bounded &= DistanceToSegment(span->Pole(i), span->StartPoint(), span->EndPoint()) <= error;
            }
            if (bounded)
            {
                result.push_back(span);
                return true;
            }
            if (depth >= 20)
                return false;
            const auto halves = BisectNativeBezier(span);
            return BoundSkinWithChords(halves[0], error, result, depth + 1) &&
                BoundSkinWithChords(halves[1], error, result, depth + 1);
        }

        std::vector<gp_Pnt2d> ConvexSkinHull(std::vector<gp_Pnt2d> points)
        {
            std::ranges::sort(points, [](const gp_Pnt2d& a, const gp_Pnt2d& b) {
                return a.X() < b.X() || (a.X() == b.X() && a.Y() < b.Y());
            });
            const auto duplicate = std::ranges::unique(points, [](const auto& a, const auto& b) {
                return a.IsEqual(b, 0);
            });
            points.erase(duplicate.begin(), duplicate.end());
            if (points.size() < 3)
                return points;
            std::vector<gp_Pnt2d> hull;
            const auto append = [&](const auto& range, std::size_t floor)
            {
                for (const gp_Pnt2d& p : range)
                {
                    while (hull.size() > floor &&
                        gp_Vec2d(hull[hull.size() - 2], hull.back()).Crossed(
                            gp_Vec2d(hull.back(), p)) <= 0)
                        hull.pop_back();
                    hull.push_back(p);
                }
                hull.pop_back();
            };
            append(points, 1);
            append(points | std::views::reverse, hull.size() + 1);
            return hull;
        }

        std::optional<ConvexSkin> ReadConvexSkin(const SSectionWire& wire, double error)
        {
            ConvexSkin skin;
            const double chordError = error / 8;
            for (const auto& edge : wire.Edges)
            {
                const auto curve = occ::BuildOpenCascadeCurve2(edge.Curve);
                for (const auto& span : NativeBezierSpans(curve, edge.Range.First, edge.Range.Last, edge.bReversed))
                    if (!BoundSkinWithChords(span, chordError, skin.Spans))
                        return std::nullopt;
            }
            if (skin.Spans.size() < 3)
                return std::nullopt;
            std::vector<gp_Pnt2d> vertices;
            double signedArea = 0;
            const auto start = skin.Spans.front()->StartPoint();
            for (const auto& span : skin.Spans)
            {
                vertices.push_back(span->StartPoint());
                vertices.push_back(span->EndPoint());
                signedArea += gp_Vec2d(start, span->StartPoint()).Crossed(gp_Vec2d(start, span->EndPoint()));
            }
            skin.Hull = ConvexSkinHull(std::move(vertices));
            if (skin.Hull.size() < 3 || std::abs(signedArea) <= error * error)
                return std::nullopt;
            skin.Orientation = std::copysign(1.0, signedArea);
            // Every native span stays inside a convex capsule of a hull side by
            // the positive-weight Bezier bound. This rejects a real concavity.
            const double capsule = error - chordError;
            std::size_t side = 0;
            for (const auto& span : skin.Spans)
            {
                std::size_t checked = 0;
                for (; checked < skin.Hull.size(); ++checked)
                {
                    const auto& a = skin.Hull[side], b = skin.Hull[(side + 1) % skin.Hull.size()];
                    if (DistanceToSegment(span->StartPoint(), a, b) <= capsule &&
                        DistanceToSegment(span->EndPoint(), a, b) <= capsule)
                        break;
                    side = (side + (skin.Orientation > 0 ? 1 : skin.Hull.size() - 1)) % skin.Hull.size();
                }
                if (checked == skin.Hull.size())
                    return std::nullopt;
            }
            return skin;
        }

        struct SkinHalfPlane
        {
            gp_Pnt2d Point;
            gp_Vec2d Inward;
        };

        std::vector<SkinHalfPlane> SkinHalfPlanes(const ConvexSkin& skin)
        {
            std::vector<SkinHalfPlane> planes;
            for (std::size_t i = 0; i < skin.Hull.size(); ++i)
            {
                const gp_Vec2d edge(skin.Hull[i], skin.Hull[(i + 1) % skin.Hull.size()]);
                const double length = edge.Magnitude();
                if (length > Precision::Confusion())
                    planes.push_back({skin.Hull[i], gp_Vec2d(-edge.Y(), edge.X()) / length});
            }
            return planes;
        }

        bool ProvesNormalReach(const NativeBezier& span, double orientation,
            const std::vector<SkinHalfPlane>& target, double error, int depth = 0)
        {
            gp_Pnt2d middle;
            gp_Vec2d tangent;
            span->D1(0.5, middle, tangent);
            if (tangent.SquareMagnitude() <= std::numeric_limits<double>::min())
                return false;
            tangent /= tangent.Magnitude();
            const gp_Vec2d normal(-orientation * tangent.Y(), orientation * tangent.X());
            double lo = 0, hi = std::numeric_limits<double>::infinity();
            for (const auto& plane : target)
            {
                const double offset = 2 * error - gp_Vec2d(plane.Point, middle).Dot(plane.Inward);
                const double slope = normal.Dot(plane.Inward);
                if (std::abs(slope) <= Precision::Angular())
                {
                    if (offset >= 0)
                        return false;
                }
                else if (slope > 0)
                    lo = std::max(lo, offset / slope);
                else
                    hi = std::min(hi, offset / slope);
            }
            if (lo >= hi || !std::isfinite(hi))
                return false;
            const double reach = std::midpoint(lo, hi);
            double normalDeviation = 0, positionDeviation = 0;
            for (int i = 1; i <= span->NbPoles(); ++i)
            {
                positionDeviation = std::max(positionDeviation, middle.Distance(span->Pole(i)));
                if (i == 1)
                    continue;
                gp_Vec2d step(span->Pole(i - 1), span->Pole(i));
                if (step.SquareMagnitude() <= std::numeric_limits<double>::min())
                    continue;
                step /= step.Magnitude();
                // A positive-weight rational Bezier tangent belongs to the
                // positive cone of its forward control polygon increments.
                if (step.Dot(tangent) <= 0)
                {
                    normalDeviation = 2;
                    break;
                }
                normalDeviation = std::max(normalDeviation, (step - tangent).Magnitude());
            }
            const auto witness = middle.Translated(normal * reach);
            const double uncertainty = positionDeviation + reach * normalDeviation;
            if (std::ranges::all_of(target, [&](const auto& plane) {
                    return gp_Vec2d(plane.Point, witness).Dot(plane.Inward) > uncertainty + error;
                }))
                return true;
            if (depth >= 12)
                return false;
            const auto halves = BisectNativeBezier(span);
            return ProvesNormalReach(halves[0], orientation, target, error, depth + 1) &&
                ProvesNormalReach(halves[1], orientation, target, error, depth + 1);
        }

        std::optional<detail::Plan> PlanCurvedWall(const SSectionWiresResult& section, double tolerance)
        {
            if (section.Wires.size() != 2)
                return std::nullopt;
            const auto outerIndex = section.Wires.front().bInner ? 1 : 0;
            const double error = std::min(SectionContourFitToleranceMm, tolerance);
            const auto outer = ReadConvexSkin(section.Wires[outerIndex], error);
            const auto inner = ReadConvexSkin(section.Wires[1 - outerIndex], error);
            if (!outer || !inner)
                return std::nullopt;
            const auto target = SkinHalfPlanes(*inner);
            if (target.size() < 3 || !std::ranges::all_of(outer->Spans, [&](const auto& span) {
                    return ProvesNormalReach(span, outer->Orientation, target, error);
                }))
                return std::nullopt;
            detail::Plan plan;
            plan.Walls.push_back({"连续壁区", {}, {}, {}});
            return plan;
        }
