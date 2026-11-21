        struct BranchRail
        {
            Handle(Geom2d_Curve) Curve;
            double First, Last;
            std::size_t NativeIndex;
            bool IsInner, Reversed;
            std::vector<NativeBezier> Beziers;
        };

        struct BranchContact
        {
            const BranchRail* Rail;
            double Parameter;
            gp_Pnt2d Point;
            bool Vertex;
        };

        using BranchContacts = std::array<BranchContact, 3>;

        std::vector<BranchRail> ReadBranchRails(const SSectionWiresResult& section)
        {
            std::vector<BranchRail> rails;
            for (const auto& wire : section.Wires)
                for (const auto& edge : wire.Edges)
                {
                    const auto curve = occ::BuildOpenCascadeCurve2(edge.Curve);
                    rails.push_back({curve, edge.Range.First, edge.Range.Last, rails.size(), wire.bInner, edge.bReversed,
                        NativeBezierSpans(curve, edge.Range.First, edge.Range.Last)});
                }
            return rails;
        }

        double DistanceToControlHull(const NativeBezier& span, const gp_Pnt2d& point)
        {
            std::vector<gp_Pnt2d> poles;
            for (int i = 1; i <= span->NbPoles(); ++i)
                poles.push_back(span->Pole(i));
            const auto hull = ConvexSkinHull(std::move(poles));
            double distance = std::numeric_limits<double>::infinity();
            bool inside = hull.size() > 2;
            for (std::size_t i = 0; i < hull.size(); ++i)
            {
                const auto& a = hull[i], b = hull[(i + 1) % hull.size()];
                inside &= gp_Vec2d(a, b).Crossed(gp_Vec2d(a, point)) >= 0;
                distance = std::min(distance, DistanceToSegment(point, a, b));
            }
            return inside ? 0 : distance;
        }

        bool ClearsContactDisk(const NativeBezier& span, const gp_Pnt2d& center,
            double radius, int depth = 0)
        {
            if (DistanceToControlHull(span, center) >= radius)
                return true;
            if (depth >= 28 || span->StartPoint().Distance(center) < radius ||
                span->EndPoint().Distance(center) < radius)
                return false;
            const auto halves = BisectNativeBezier(span);
            return ClearsContactDisk(halves[0], center, radius, depth + 1) &&
                ClearsContactDisk(halves[1], center, radius, depth + 1);
        }

        BranchContact BranchSite(const BranchRail& rail, const gp_Pnt2d& hint,
            bool pointSite, double tolerance)
        {
            for (double parameter : {rail.First, rail.Last})
            {
                const auto end = rail.Curve->Value(parameter);
                if (pointSite && hint.Distance(end) <= tolerance)
                    return {&rail, parameter, end, true};
            }
            Geom2dAPI_ProjectPointOnCurve projection(hint, rail.Curve, rail.First, rail.Last);
            double parameter = rail.First;
            if (hint.Distance(rail.Curve->Value(rail.Last)) < hint.Distance(rail.Curve->Value(parameter)))
                parameter = rail.Last;
            if (projection.NbPoints() > 0 && projection.LowerDistance() < hint.Distance(rail.Curve->Value(parameter)))
                parameter = projection.LowerDistanceParameter();
            return {&rail, parameter, rail.Curve->Value(parameter), false};
        }

        using ContactVariables = std::array<double, 6>;
        using ContactSystem = std::array<std::array<double, 7>, 6>;

        bool SolveContactSystem(ContactSystem& system)
        {
            for (std::size_t col = 0; col < system.size(); ++col)
            {
                auto pivot = col;
                for (std::size_t row = col + 1; row < system.size(); ++row)
                    if (std::abs(system[row][col]) > std::abs(system[pivot][col]))
                        pivot = row;
                if (std::abs(system[pivot][col]) <= std::numeric_limits<double>::epsilon())
                    return false;
                std::swap(system[pivot], system[col]);
                const double scale = system[col][col];
                for (std::size_t i = col; i <= system.size(); ++i)
                    system[col][i] /= scale;
                for (std::size_t row = 0; row < system.size(); ++row)
                {
                    if (row == col)
                        continue;
                    const double factor = system[row][col];
                    for (std::size_t i = col; i <= system.size(); ++i)
                        system[row][i] -= factor * system[col][i];
                }
            }
            return true;
        }

        double EvaluateContactSystem(const BranchContacts& contacts, const gp_Pnt2d& origin,
            double scale, const ContactVariables& variables, ContactSystem& system)
        {
            system = {};
            for (std::size_t i = 0; i < contacts.size(); ++i)
            {
                const auto& rail = *contacts[i].Rail;
                const auto parameter = std::lerp(rail.First, rail.Last, variables[3 + i]);
                gp_Pnt2d p;
                gp_Vec2d tangent, second;
                if (contacts[i].Vertex)
                    p = contacts[i].Point;
                else
                    rail.Curve->D2(parameter, p, tangent, second);
                const double range = rail.Last - rail.First;
                const gp_Vec2d offset((p.X() - origin.X()) / scale - variables[0],
                    (p.Y() - origin.Y()) / scale - variables[1]);
                tangent *= range / scale;
                second *= range * range / scale;
                const double along = offset.Dot(tangent);
                system[i][0] = -2 * offset.X();
                system[i][1] = -2 * offset.Y();
                system[i][2] = -2 * variables[2];
                system[i][3 + i] = 2 * along;
                system[i][6] = variables[2] * variables[2] - offset.SquareMagnitude();
                auto& orthogonality = system[3 + i];
                if (contacts[i].Vertex)
                    orthogonality[3 + i] = 1;
                else
                {
                    orthogonality[0] = -tangent.X();
                    orthogonality[1] = -tangent.Y();
                    orthogonality[3 + i] = tangent.SquareMagnitude() + offset.Dot(second);
                    orthogonality[6] = -along;
                }
            }
            double residual = 0;
            for (auto& row : system)
            {
                double norm = 0;
                for (std::size_t j = 0; j < system.size(); ++j)
                    norm = std::max(norm, std::abs(row[j]));
                if (norm <= std::numeric_limits<double>::min())
                    return std::numeric_limits<double>::infinity();
                for (auto& x : row)
                    x /= norm;
                residual = std::max(residual, std::abs(row.back()));
            }
            return residual;
        }

        std::optional<BranchContacts> RefineBranchContacts(BranchContacts contacts,
            const std::vector<BranchRail>& rails, const TopoDS_Face& material,
            const gp_Pnt2d& seed, double scale, double guideError, double tolerance)
        {
            ContactVariables variables{};
            for (std::size_t i = 0; i < contacts.size(); ++i)
            {
                const auto& site = contacts[i];
                variables[2] += seed.Distance(site.Point) / (3 * scale);
                variables[3 + i] = (site.Parameter - site.Rail->First) / (site.Rail->Last - site.Rail->First);
            }
            bool converged = false;
            for (int iteration = 0; iteration < 45; ++iteration)
            {
                ContactSystem system;
                const double residual = EvaluateContactSystem(contacts, seed, scale, variables, system);
                if (residual < 0.05 * tolerance / scale)
                {
                    converged = true;
                    break;
                }
                if (!SolveContactSystem(system))
                    return std::nullopt;
                bool improved = false;
                for (double damping = 1; damping >= 1.0 / 65536; damping *= 0.5)
                {
                    auto next = variables;
                    for (std::size_t i = 0; i < variables.size(); ++i)
                        next[i] += damping * system[i][6];
                    if (next[2] <= 0 || std::any_of(next.begin() + 3, next.end(),
                            [](double u) { return u < 0 || u > 1; }))
                        continue;
                    ContactSystem nextSystem;
                    if (EvaluateContactSystem(contacts, seed, scale, next, nextSystem) < residual)
                    {
                        variables = next;
                        improved = true;
                        break;
                    }
                }
                if (!improved)
                    return std::nullopt;
            }
            const gp_Pnt2d center(seed.X() + variables[0] * scale, seed.Y() + variables[1] * scale);
            const double radius = variables[2] * scale;
            if (!converged || center.Distance(seed) > 20 * guideError || radius <= 20 * tolerance ||
                BRepClass_FaceClassifier(material, gp_Pnt(0, center.X(), center.Y()), tolerance).State() != TopAbs_IN)
                return std::nullopt;
            for (std::size_t i = 0; i < contacts.size(); ++i)
            {
                auto& site = contacts[i];
                if (!site.Vertex)
                {
                    site.Parameter = std::lerp(site.Rail->First, site.Rail->Last, variables[i + 3]);
                    gp_Vec2d tangent;
                    site.Rail->Curve->D1(site.Parameter, site.Point, tangent);
                    if (tangent.SquareMagnitude() <= std::numeric_limits<double>::min() ||
                        std::abs(tangent.Normalized().Dot(gp_Vec2d(center, site.Point))) > 10 * tolerance)
                        return std::nullopt;
                }
                if (std::abs(site.Point.Distance(center) - radius) > 10 * tolerance)
                    return std::nullopt;
                for (std::size_t j = 0; j < i; ++j)
                    if (site.Point.Distance(contacts[j].Point) <= 20 * tolerance)
                        return std::nullopt;
            }
            for (const auto& rail : rails)
                for (const auto& span : rail.Beziers)
                    if (!ClearsContactDisk(span, center, radius - 10 * tolerance))
                        return std::nullopt;
            return contacts;
        }

        bool SameContacts(const BranchContacts& a, const BranchContacts& b, double tolerance)
        {
            return std::ranges::all_of(a, [&](const auto& site) {
                return std::ranges::any_of(b, [&](const auto& other) {
                    return site.Rail->IsInner == other.Rail->IsInner && site.Point.Distance(other.Point) <= tolerance;
                });
            });
        }

        std::vector<BranchContacts> FindBranchContacts(const std::vector<BranchRail>& rails,
            const TopoDS_Face& material, const gp_Pnt2d& origin, double scale,
            double error, double tolerance)
        {
            struct Guide { gp_Pnt2d Start, End; const BranchRail* Rail; };
            std::vector<Guide> guides;
            using SitePoint = boost::polygon::point_data<int>;
            using SiteSegment = boost::polygon::segment_data<int>;
            using Diagram = boost::polygon::voronoi_diagram<double>;
            std::vector<SiteSegment> segments;
            const double numericScale = 1e8 / scale;
            const auto quantify = [&](const gp_Pnt2d& point) {
                return SitePoint(static_cast<int>(std::llround((point.X() - origin.X()) * numericScale)),
                    static_cast<int>(std::llround((point.Y() - origin.Y()) * numericScale)));
            };
            for (const auto& rail : rails)
                for (const auto& bezier : rail.Beziers)
                {
                    std::vector<NativeBezier> spans;
                    if (!BoundSkinWithChords(bezier, error, spans))
                        return {};
                    for (const auto& span : spans)
                    {
                        const auto first = span->StartPoint(), last = span->EndPoint();
                        const auto a = quantify(first), b = quantify(last);
                        if (a == b)
                            continue;
                        segments.emplace_back(a, b);
                        guides.push_back({first, last, &rail});
                        if (segments.size() > 100000)
                            return {};
                    }
                }
            Diagram diagram;
            boost::polygon::construct_voronoi(segments.begin(), segments.end(), &diagram);
            const auto location = [&](const Diagram::vertex_type* vertex) {
                return gp_Pnt2d(origin.X() + vertex->x() / numericScale, origin.Y() + vertex->y() / numericScale);
            };
            const auto hint = [&](const Diagram::cell_type* cell, const gp_Pnt2d& point) {
                const auto& guide = guides[cell->source_index()];
                if (cell->contains_point())
                    return cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT
                        ? guide.Start : guide.End;
                const gp_Vec2d step(guide.Start, guide.End);
                const double u = std::clamp(gp_Vec2d(guide.Start, point).Dot(step) / step.SquareMagnitude(), 0.0, 1.0);
                return guide.Start.Translated(step * u);
            };
            const auto radius = [&](const Diagram::vertex_type* vertex) {
                const auto point = location(vertex);
                return point.Distance(hint(vertex->incident_edge()->cell(), point));
            };
            std::vector<BranchContacts> result;
            for (const auto& vertex : diagram.vertices())
            {
                const auto point = location(&vertex);
                if (radius(&vertex) <= 2 * error)
                    continue;
                const auto* first = vertex.incident_edge();
                auto* edge = first;
                int paths = 0;
                bool touchesInner = false, touchesOuter = false;
                do
                {
                    if (edge->is_primary() && edge->vertex1() && radius(edge->vertex1()) > 2 * error)
                        ++paths;
                    const auto* rail = guides[edge->cell()->source_index()].Rail;
                    touchesInner |= rail->IsInner;
                    touchesOuter |= !rail->IsInner;
                    edge = edge->rot_next();
                } while (edge != first);
                if (paths < 3 || !touchesInner || !touchesOuter ||
                    BRepClass_FaceClassifier(material, gp_Pnt(0, point.X(), point.Y()), tolerance).State() != TopAbs_IN)
                    continue;
                std::vector<BranchContact> outer, inner;
                do
                {
                    const auto* rail = guides[edge->cell()->source_index()].Rail;
                    const auto site = BranchSite(*rail, hint(edge->cell(), point), edge->cell()->contains_point(), tolerance);
                    auto& group = rail->IsInner ? inner : outer;
                    if (std::ranges::none_of(group, [&](const auto& other) {
                            return other.Point.Distance(site.Point) <= 20 * tolerance;
                        }))
                        group.push_back(site);
                    edge = edge->rot_next();
                } while (edge != first);
                for (const auto& inside : inner)
                    for (std::size_t i = 0; i < outer.size(); ++i)
                        for (std::size_t j = i + 1; j < outer.size(); ++j)
                        {
                            try
                            {
                                const auto contacts = RefineBranchContacts({outer[i], outer[j], inside},
                                    rails, material, point, scale, error, tolerance);
                                if (contacts && std::ranges::none_of(result, [&](const auto& group) {
                                        return SameContacts(group, *contacts, 20 * tolerance);
                                    }))
                                    result.push_back(*contacts);
                            }
                            catch (const Standard_Failure&)
                            {
                                // A proposal at a singular native trim cannot
                                // establish a normal-contact branch.
                            }
                        }
            }
            return result;
        }

        int AppendBranchSkin(const BranchContact& begin, const BranchContact& end,
            const std::vector<BranchRail>& rails, const std::vector<detail::NativeContourEdge>& native,
            const gp_Pln& plane, std::vector<TopoDS_Shape>& sourceCurves,
            std::vector<std::vector<std::uint64_t>>& sourceIds, double tolerance)
        {
            const auto firstOuter = static_cast<std::size_t>(std::ranges::find_if(rails, [](const auto& rail) {
                return !rail.IsInner;
            }) - rails.begin());
            BRep_Builder builder;
            TopoDS_Compound support;
            builder.MakeCompound(support);
            std::vector<std::uint64_t> ids;
            const auto add = [&](const BranchRail& rail, double a, double b)
            {
                if (std::abs(a - b) <= Precision::PConfusion() ||
                    rail.Curve->Value(a).Distance(rail.Curve->Value(b)) <= tolerance)
                    return;
                if (a > b)
                    std::swap(a, b);
                BRepBuilderAPI_MakeEdge edge(GeomAPI::To3d(rail.Curve, plane), a, b);
                if (!edge.IsDone())
                    throw std::runtime_error("Could not reconstruct the original curved branch span");
                builder.Add(support, edge.Edge());
                ids.push_back(native[rail.NativeIndex].SourceEdgeId);
            };
            auto index = begin.Rail->NativeIndex;
            double parameter = begin.Parameter;
            for (std::size_t seen = 0; seen <= rails.size(); ++seen)
            {
                const auto& rail = rails[index];
                if (&rail == end.Rail && ((parameter < end.Parameter) != rail.Reversed))
                {
                    add(rail, parameter, end.Parameter);
                    break;
                }
                add(rail, parameter, rail.Reversed ? rail.First : rail.Last);
                index = index + 1 < rails.size() && !rails[index + 1].IsInner ? index + 1 : firstOuter;
                parameter = rails[index].Reversed ? rails[index].Last : rails[index].First;
            }
            if (ids.empty())
                return -1;
            const auto id = static_cast<int>(sourceCurves.size());
            sourceCurves.push_back(support);
            sourceIds.push_back(std::move(ids));
            return id;
        }

        std::optional<detail::Plan> PlanCurvedBranch(const SSectionWiresResult& section,
            const TopoDS_Face& material, const std::vector<detail::NativeContourEdge>& native,
            std::vector<TopoDS_Shape>& sourceCurves, std::vector<std::vector<std::uint64_t>>& sourceIds,
            const gp_Pln& plane, double tolerance)
        {
            if (section.Wires.size() != 2)
                return std::nullopt;
            const auto rails = ReadBranchRails(section);
            double minY = std::numeric_limits<double>::infinity(), minZ = minY, maxY = -minY, maxZ = -minY;
            for (const auto& rail : rails)
                for (const auto& curve : rail.Beziers)
                    for (int i = 1; i <= curve->NbPoles(); ++i)
                    {
                        const auto p = curve->Pole(i);
                        minY = std::min(minY, p.X()); minZ = std::min(minZ, p.Y());
                        maxY = std::max(maxY, p.X()); maxZ = std::max(maxZ, p.Y());
                    }
            const gp_Pnt2d origin(std::midpoint(minY, maxY), std::midpoint(minZ, maxZ));
            const double scale = std::hypot(maxY - minY, maxZ - minZ);
            if (!std::isfinite(scale) || scale <= 20 * tolerance)
                return std::nullopt;
            std::vector<BranchContacts> previous;
            std::optional<BranchContacts> proven;
            // The diagram proposes contact neighborhoods. Refine them on
            // the original curves, check the empty disk, then require the
            // same native contacts from two successive guide refinements.
            for (double error = scale / 10000; error >= scale / 80000; error *= 0.5)
            {
                const auto contacts = FindBranchContacts(rails, material, origin, scale, error, tolerance);
                if (contacts.size() == 1 && previous.size() == 1 &&
                    SameContacts(contacts[0], previous[0], 20 * tolerance))
                {
                    proven = contacts[0];
                    break;
                }
                previous = contacts;
            }
            if (!proven)
                return std::nullopt;
            const auto& [a, b, inner] = *proven;
            const gp_Vec2d cut(a.Point, b.Point);
            gp_Pnt2d p;
            gp_Vec2d forward;
            a.Rail->Curve->D1(a.Parameter, p, forward);
            if (a.Rail->Reversed)
                forward.Reverse();
            const double side = cut.Crossed(gp_Vec2d(a.Point, inner.Point));
            if (std::abs(side) <= tolerance * cut.Magnitude())
                return std::nullopt;
            const bool forwardIsFin = cut.Crossed(forward) * side < 0;
            const int fin = AppendBranchSkin(forwardIsFin ? a : b, forwardIsFin ? b : a, rails,
                native, plane, sourceCurves, sourceIds, tolerance);
            if (fin < 0)
                return std::nullopt;
            const auto loop = static_cast<int>(sourceCurves.size());
            sourceCurves.push_back(native[inner.Rail->NativeIndex].Edge);
            sourceIds.push_back({native[inner.Rail->NativeIndex].SourceEdgeId});
            detail::Plan plan;
            plan.Crosscuts.push_back({ToPoint(a.Point), ToPoint(b.Point)});
            plan.Walls.push_back({"闭合壁区", {loop}, {loop}, {}});
            plan.Walls.push_back({"曲壁分支", {fin}, {fin}, {}});
            return plan;
        }
