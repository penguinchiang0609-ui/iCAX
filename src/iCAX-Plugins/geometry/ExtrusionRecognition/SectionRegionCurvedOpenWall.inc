        struct OpenWallUse
        {
            gp_Pnt2d Start, End;
            gp_Vec2d Tangent;
            bool IsStraight;
            std::vector<std::size_t> Edges;
        };

        std::vector<OpenWallUse> ReadOpenWallUses(const SSectionWire& wire, double tolerance)
        {
            std::vector<OpenWallUse> uses;
            const auto extend = [&](OpenWallUse& a, const OpenWallUse& b)
            {
                if (!a.IsStraight || !b.IsStraight ||
                    a.Tangent.Dot(b.Tangent) <= 0 ||
                    std::abs(a.Tangent.Crossed(gp_Vec2d(a.Start, b.End))) > tolerance)
                    return false;
                a.End = b.End;
                a.Edges.insert(a.Edges.end(), b.Edges.begin(), b.Edges.end());
                return true;
            };
            for (std::size_t i = 0; i < wire.Edges.size(); ++i)
            {
                const auto& edge = wire.Edges[i];
                const auto geometry = occ::BuildOpenCascadeCurve2(edge.Curve);
                const Geom2dAdaptor_Curve trim(geometry, edge.Range.First, edge.Range.Last);
                const auto first = geometry->Value(edge.bReversed ? edge.Range.Last : edge.Range.First);
                const auto last = geometry->Value(edge.bReversed ? edge.Range.First : edge.Range.Last);
                gp_Vec2d tangent(first, last);
                const bool line = trim.GetType() == GeomAbs_Line && tangent.Magnitude() > tolerance;
                if (line)
                    tangent.Normalize();
                OpenWallUse use{first, last, tangent, line, {i}};
                if (uses.empty() || !extend(uses.back(), use))
                    uses.push_back(std::move(use));
            }
            if (uses.size() > 1 && extend(uses.back(), uses.front()))
                uses.erase(uses.begin());
            return uses;
        }

        gp_Vec2d WallInward(const OpenWallUse& rail, double orientation)
        {
            return gp_Vec2d(-rail.Tangent.Y(), rail.Tangent.X()) * orientation;
        }

        std::optional<std::array<gp_Pnt2d, 4>> FiniteNormalStrip(const OpenWallUse& guide,
            const OpenWallUse& target, double orientation, double tolerance, bool requireFullTarget)
        {
            if (guide.Tangent.Dot(target.Tangent) >= 0)
                return std::nullopt;
            const double length = guide.Start.Distance(guide.End);
            const double first = gp_Vec2d(guide.Start, target.End).Dot(guide.Tangent);
            const double last = gp_Vec2d(guide.Start, target.Start).Dot(guide.Tangent);
            if (requireFullTarget && (first < -tolerance || last > length + tolerance))
                return std::nullopt;
            const double lo = std::clamp(first, 0.0, length);
            const double hi = std::clamp(last, 0.0, length);
            if (hi - lo <= 3 * tolerance)
                return std::nullopt;
            const auto a = guide.Start.Translated(guide.Tangent * lo);
            const auto b = guide.Start.Translated(guide.Tangent * hi);
            const auto c = target.End.Translated(gp_Vec2d(target.End, target.Start) * ((lo - first) / (last - first)));
            const auto d = target.End.Translated(gp_Vec2d(target.End, target.Start) * ((hi - first) / (last - first)));
            const gp_Vec2d inward = WallInward(guide, orientation);
            if (gp_Vec2d(a, c).Dot(inward) <= tolerance || gp_Vec2d(b, d).Dot(inward) <= tolerance)
                return std::nullopt;
            return std::array{a, b, d, c};
        }

        bool IsContainedStrip(const TopoDS_Face& material, const gp_Pln& plane,
            const std::array<gp_Pnt2d, 4>& strip, double tolerance)
        {
            const auto point = [](const gp_Pnt2d& p) { return gp_Pnt(0, p.X(), p.Y()); };
            BRepBuilderAPI_MakeWire wire;
            double perimeter = 0;
            for (std::size_t i = 0; i < strip.size(); ++i)
            {
                const auto& a = strip[i], b = strip[(i + 1) % strip.size()];
                wire.Add(BRepBuilderAPI_MakeEdge(point(a), point(b)).Edge());
                perimeter += a.Distance(b);
            }
            if (!wire.IsDone())
                return false;
            BRepBuilderAPI_MakeFace face(plane, wire.Wire(), true);
            if (!face.IsDone())
                return false;
            NCollection_List<TopoDS_Shape> arguments, tools;
            arguments.Append(face.Face());
            tools.Append(material);
            BRepAlgoAPI_Cut difference;
            difference.SetArguments(arguments);
            difference.SetTools(tools);
            difference.SetNonDestructive(true);
            difference.Build();
            if (!difference.IsDone() || difference.HasErrors())
                return false;
            GProp_GProps props;
            BRepGProp::SurfaceProperties(difference.Shape(), props);
            // Every sweep segment lies in the finite ruled strip. Native
            // subtraction bounds any uncovered material by its edge accuracy.
            return std::abs(props.Mass()) <= tolerance * perimeter;
        }

        bool BoundsTip(const SSectionWire& wire, const std::vector<OpenWallUse>& uses,
            std::size_t first, std::size_t last, const OpenWallUse& guide, const OpenWallUse& opposite,
            bool atEnd, double orientation, double tolerance)
        {
            if (first == last)
                return false;
            const auto tip = atEnd ? opposite.Start : opposite.End;
            const auto limit = guide.Tangent * (atEnd ? 1 : -1);
            const auto outsideNormal = WallInward(guide, orientation);
            const auto insideNormal = WallInward(opposite, orientation);
            for (std::size_t i = first; i != last; i = (i + 1) % uses.size())
            {
                for (auto id : uses[i].Edges)
                {
                    const auto& edge = wire.Edges[id];
                    const auto curve = occ::BuildOpenCascadeCurve2(edge.Curve);
                    for (const auto& bezier : NativeBezierSpans(curve, edge.Range.First, edge.Range.Last))
                    {
                        for (int k = 1; k <= bezier->NbPoles(); ++k)
                        {
                            const auto p = bezier->Pole(k);
                            if (bezier->Weight(k) <= 0 || gp_Vec2d(tip, p).Dot(limit) < -tolerance ||
                                gp_Vec2d(guide.Start, p).Dot(outsideNormal) < -tolerance ||
                                gp_Vec2d(opposite.Start, p).Dot(insideNormal) < -tolerance)
                                return false;
                        }
                    }
                }
            }
            return true;
        }

        std::optional<detail::Plan> PlanCurvedOpenWall(const SSectionWiresResult& section,
            const TopoDS_Face& material, const std::vector<detail::NativeContourEdge>& native,
            std::vector<TopoDS_Shape>& sourceCurves, std::vector<std::vector<std::uint64_t>>& sourceIds,
            const gp_Pln& plane, double tolerance)
        {
            if (section.Wires.size() != 1)
                return std::nullopt;
            const auto& wire = section.Wires.front();
            const auto uses = ReadOpenWallUses(wire, tolerance);
            if (uses.size() < 6)
                return std::nullopt;
            const auto next = [&](std::size_t index) { return (index + 1) % uses.size(); };
            double signedArea = 0;
            for (const auto& use : uses)
            {
                signedArea += gp_Vec2d(uses.front().Start, use.Start).Crossed(
                    gp_Vec2d(uses.front().Start, use.End));
            }
            if (std::abs(signedArea) <= tolerance * tolerance)
                return std::nullopt;
            const double orientation = std::copysign(1.0, signedArea);
            const auto turn = [&](std::size_t a, std::size_t b) {
                return orientation * uses[a].Tangent.Crossed(uses[b].Tangent);
            };
            const auto firstSource = static_cast<int>(sourceCurves.size());
            const auto id = [&](std::size_t index) { return firstSource + static_cast<int>(index); };
            std::optional<detail::Plan> result;
            for (std::size_t start = 0; start < uses.size(); ++start)
            {
                if (!uses[start].IsStraight)
                    continue;
                std::vector<std::size_t> outside{start};
                while (outside.size() < uses.size() / 2)
                {
                    const auto last = outside.back(), after = next(last);
                    if (!uses[after].IsStraight || turn(last, after) <= Precision::Angular())
                        break;
                    outside.push_back(after);
                    for (auto innerStart = next(after); innerStart != start; innerStart = next(innerStart))
                    {
                        if (!uses[innerStart].IsStraight || !BoundsTip(wire, uses, next(after),
                                innerStart, uses[after], uses[innerStart], true, orientation, tolerance))
                            continue;
                        detail::Plan plan;
                        std::vector<std::array<gp_Pnt2d, 4>> strips;
                        std::vector<std::vector<int>> bends;
                        auto inner = innerStart;
                        for (std::size_t j = outside.size(); j-- > 0;)
                        {
                            const auto outer = outside[j];
                            const auto strip = FiniteNormalStrip(uses[outer], uses[inner], orientation, tolerance, true);
                            const auto reverse = FiniteNormalStrip(uses[inner], uses[outer], orientation, tolerance, false);
                            if (!strip || !reverse || !IsContainedStrip(material, plane, *strip, tolerance) ||
                                !IsContainedStrip(material, plane, *reverse, tolerance))
                                break;
                            const int wallId = static_cast<int>(plan.Walls.size() + 1);
                            plan.Walls.push_back({"直壁 " + std::to_string(wallId), {id(inner)}, {id(outer), id(inner)}, {}});
                            strips.push_back(*strip);
                            if (j == 0)
                                break;
                            auto following = next(inner);
                            std::vector<int> bend;
                            while (following != start && !uses[following].IsStraight)
                            {
                                bend.push_back(id(following));
                                following = next(following);
                            }
                            if (bend.empty() || following == start || turn(inner, following) >= -Precision::Angular())
                                break;
                            bends.push_back(std::move(bend));
                            inner = following;
                        }
                        if (strips.size() != outside.size() || bends.size() + 1 != strips.size() ||
                            !BoundsTip(wire, uses, next(inner), start, uses[start], uses[inner],
                                false, orientation, tolerance))
                            continue;
                        for (std::size_t j = 0; j < bends.size(); ++j)
                        {
                            const auto& a = strips[j];
                            const auto& b = strips[j + 1];
                            plan.Crosscuts.push_back({ToPoint(a[0]), ToPoint(a[3])});
                            plan.Crosscuts.push_back({ToPoint(b[1]), ToPoint(b[2])});
                            plan.Walls.push_back({"转角区 " + std::to_string(j + 1), bends[j], bends[j], {}});
                        }
                        if (result)
                            return std::nullopt;
                        result = std::move(plan);
                    }
                }
            }
            if (!result)
                return std::nullopt;
            // A classifier guide is attached to the native trim(s), not the
            // ruled strip. This preserves each original contour identity.
            for (const auto& use : uses)
            {
                BRep_Builder builder;
                TopoDS_Compound original;
                builder.MakeCompound(original);
                std::vector<std::uint64_t> ids;
                for (auto index : use.Edges)
                {
                    builder.Add(original, native.at(index).Edge);
                    ids.push_back(native.at(index).SourceEdgeId);
                }
                sourceCurves.push_back(original);
                sourceIds.push_back(std::move(ids));
            }
            return result;
        }
