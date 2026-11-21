        struct BoreRimEdge
        {
            TopoDS_Edge Edge;
            std::uint64_t FaceId;
        };

        struct BoreRim
        {
            gp_Pnt Center;
            std::vector<BoreRimEdge> Edges;
        };

        bool IsCoaxial(const gp_Ax1& reference, const gp_Ax1& axis)
        {
            return reference.Direction().IsParallel(axis.Direction(), Precision::Angular()) &&
                gp_Lin(reference).Distance(axis.Location()) <= GeometryTolerance;
        }

        bool CoversCircle(const BoreRim& rim, const gp_Cylinder& cylinder)
        {
            constexpr double fullTurn = 2 * std::numbers::pi;
            const double tolerance = GeometryTolerance / cylinder.Radius();
            const gp_Ax2 frame(rim.Center, cylinder.Axis().Direction());
            std::vector<std::pair<double, double>> intervals;
            for (const auto& item : rim.Edges)
            {
                const BRepAdaptor_Curve curve(item.Edge);
                const double first = curve.FirstParameter(), last = curve.LastParameter();
                const double extent = last - first;
                if (extent <= 0 || extent > fullTurn + tolerance)
                    return false;
                const auto circle = curve.Circle();
                const bool sameSense = circle.Axis().Direction().Dot(frame.Direction()) > 0;
                const gp_Vec radial(circle.Location(), curve.Value(sameSense ? first : last));
                double start = std::atan2(radial.Dot(gp_Vec(frame.YDirection())), radial.Dot(gp_Vec(frame.XDirection())));
                if (start < 0)
                    start += fullTurn;
                const double end = start + std::min(extent, fullTurn);
                intervals.emplace_back(start, std::min(end, fullTurn));
                if (end > fullTurn)
                    intervals.emplace_back(0, end - fullTurn);
            }
            std::ranges::sort(intervals);
            double covered = 0;
            for (const auto& [start, end] : intervals)
            {
                if (std::abs(start - covered) > tolerance)
                    return false;
                covered = std::max(covered, end);
            }
            return std::abs(covered - fullTurn) <= tolerance;
        }

        std::vector<BoreRim> FindBoreRims(const O::SOpenCascadeBRepBuildResult& built,
            std::uint64_t sourceFaceId, const gp_Cylinder& cylinder)
        {
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> identities;
            std::vector<std::vector<std::uint64_t>> owners;
            std::map<std::uint64_t, std::vector<int>> faceEdges;
            for (const auto& [id, face] : built.Faces)
            {
                const BRepAdaptor_Surface surface(face);
                if (surface.GetType() != GeomAbs_Cylinder)
                    continue;
                const auto support = surface.Cylinder();
                if (!IsCoaxial(cylinder.Axis(), support.Axis()) ||
                    std::abs(cylinder.Radius() - support.Radius()) > GeometryTolerance)
                    continue;
                auto& indices = faceEdges[id];
                for (const auto& edge : PhysicalEdges(face))
                {
                    const int index = identities.Add(edge);
                    if (static_cast<std::size_t>(index) > owners.size())
                        owners.emplace_back();
                    owners[index - 1].push_back(id);
                    indices.push_back(index);
                }
            }
            if (!faceEdges.contains(sourceFaceId))
                return {};

            // Shared edges inside this connected cylinder are strip seams.
            // Only its free circular arcs can be part of an opening or floor.
            std::vector<std::uint64_t> connected{sourceFaceId};
            std::set<std::uint64_t> seenFaces{sourceFaceId};
            std::set<int> wallEdges;
            for (std::size_t i = 0; i < connected.size(); ++i)
                for (const int index : faceEdges.at(connected[i]))
                {
                    wallEdges.insert(index);
                    for (const auto id : owners[index - 1])
                        if (seenFaces.insert(id).second)
                            connected.push_back(id);
                }

            std::vector<BoreRim> rims;
            for (const int index : wallEdges)
            {
                const auto& faceIds = owners[index - 1];
                if (faceIds.size() == 2)
                    continue;
                if (faceIds.size() != 1)
                    return {};
                const auto edge = TopoDS::Edge(identities.FindKey(index));
                const BRepAdaptor_Curve curve(edge);
                if (curve.GetType() != GeomAbs_Circle)
                    return {};
                const auto circle = curve.Circle();
                if (!IsCoaxial(cylinder.Axis(), circle.Axis()) ||
                    std::abs(cylinder.Radius() - circle.Radius()) > GeometryTolerance)
                    return {};
                auto rim = std::ranges::find_if(rims, [&](const BoreRim& candidate)
                {
                    return candidate.Center.Distance(circle.Location()) <= GeometryTolerance;
                });
                if (rim == rims.end())
                {
                    if (rims.size() == 2)
                        return {};
                    rims.push_back({circle.Location(), {{edge, faceIds.front()}}});
                }
                else
                    rim->Edges.push_back({edge, faceIds.front()});
            }
            if (rims.size() != 2 || !CoversCircle(rims[0], cylinder) || !CoversCircle(rims[1], cylinder))
                return {};
            return rims;
        }
