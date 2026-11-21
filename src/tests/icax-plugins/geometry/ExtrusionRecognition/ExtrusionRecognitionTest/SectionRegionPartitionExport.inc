namespace section_region_export {

const char* ResultStatus(R::ESectionRegionPartitionStatus status) {
    switch (status) {
    case R::ESectionRegionPartitionStatus::NotRun: return "not-run";
    case R::ESectionRegionPartitionStatus::Complete: return "complete";
    case R::ESectionRegionPartitionStatus::Unresolved: return "unresolved";
    case R::ESectionRegionPartitionStatus::Unsupported: return "unsupported";
    case R::ESectionRegionPartitionStatus::Failed: return "failed";
    }
    return "failed";
}

VariantArray PreviewWires(const TopoDS_Face& face) {
    VariantArray wires;
    for (TopExp_Explorer wire(face, TopAbs_WIRE); wire.More(); wire.Next()) {
        VariantArray points;
        for (BRepTools_WireExplorer item(TopoDS::Wire(wire.Current()), face); item.More(); item.Next()) {
            const auto edge = TopoDS::Edge(item.Current());
            if (BRep_Tool::Degenerated(edge)) continue;
            const BRepAdaptor_Curve curve(edge);
            const bool reversed = edge.Orientation() == TopAbs_REVERSED;
            const auto append = [&](const gp_Pnt& point) {
                points.emplace_back(VariantArray{point.Y(), point.Z()});
            };
            if (curve.GetType() != GeomAbs_Line) {
                const GCPnts_QuasiUniformDeflection preview(curve, 0.001);
                if (preview.IsDone() && preview.NbPoints() >= 2) {
                    for (int i = 1; i < preview.NbPoints(); ++i)
                        append(preview.Value(reversed ? preview.NbPoints() + 1 - i : i));
                    continue;
                }
            }
            append(curve.Value(reversed ? curve.LastParameter() : curve.FirstParameter()));
        }
        if (points.size() > 1) wires.emplace_back(std::move(points));
    }
    return wires;
}

void WriteCheckpointContourPreview(const R::SSectionWiresResult& section, const fs::path& file) {
    const gp_Pln plane(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)));
    VariantArray wires;
    for (const auto& wire : section.Wires) {
        VariantArray edges;
        for (const auto& edge : wire.Edges) {
            const auto curve = O::BuildOpenCascadeCurve2(edge.Curve);
            BRepBuilderAPI_MakeEdge maker(GeomAPI::To3d(curve, plane), edge.Range.First, edge.Range.Last);
            if (!maker.IsDone()) throw std::runtime_error("Cannot display a checkpoint contour edge");
            const BRepAdaptor_Curve adaptor(maker.Edge());
            GCPnts_QuasiUniformDeflection display(adaptor, 0.001);
            if (!display.IsDone() || display.NbPoints() < 2)
                throw std::runtime_error("Cannot display the complete checkpoint contour");
            VariantArray points;
            for (int i = 1; i <= display.NbPoints(); ++i) {
                const auto point = display.Value(edge.bReversed ? display.NbPoints() + 1 - i : i);
                points.emplace_back(VariantArray{point.Y(), point.Z()});
            }
            edges.emplace_back(ObjectMap{{"sourceEdgeId", std::to_string(edge.SourceEdgeId)},
                {"sourceFaceId", std::to_string(edge.SourceFaceId)}, {"previewSamples", std::move(points)}});
        }
        wires.emplace_back(ObjectMap{{"inner", wire.bInner}, {"edges", std::move(edges)}});
    }
    Save(file, {{"coordinatePlane", std::string("aligned YZ")}, {"previewOnlySamples", true}, {"wires", std::move(wires)}});
}

ObjectMap Export(const R::SSectionRegionPartitionResult& result, const fs::path& prefix,
    const fs::path& source, const std::string& group, const std::string& partId,
    const fs::path& contourReport, const fs::path& stageReport,
    const R::SSectionWiresResult& section, const R::SSectionFaceClassificationResult& classification,
    double connectionTolerance, bool sourceUnchanged) {
    const std::string status = ResultStatus(result.Status);
    const auto file = [&](const char* ext) { auto path = prefix; path += ext; return path; };
    const auto dataFile = file(".json");
    const auto brepFile = file(".brep");
    const bool complete = result.Status == R::ESectionRegionPartitionStatus::Complete;
    const std::string stageStatus = complete ? "passed" : status == "unresolved" ? "partial" : status;
    fs::create_directories(prefix.parent_path());
    fs::remove(brepFile);
    fs::remove(dataFile);

    ObjectMap document{{"schema", 1}, {"stage", std::string("regionPartition")}, {"status", status},
        {"reason", result.Reason}, {"name", group}, {"partId", partId}, {"source", Utf8(source)},
        {"sourceUnchanged", sourceUnchanged}, {"coordinatePlane", std::string("aligned YZ")},
        {"connectionToleranceMm", connectionTolerance}, {"contourReport", Utf8(contourReport)},
        {"stageReport", Utf8(stageReport)}, {"sourceWireCount", static_cast<int>(section.Wires.size())},
        {"classifyMachiningFaceCount", static_cast<int>(classification.MachiningFaceIds.size())},
        {"classifyNonMachiningFaceCount", static_cast<int>(classification.NonMachiningFaceIds.size())}};
    ObjectMap audit{{"status", stageStatus}, {"resultStatus", status}, {"reason", result.Reason},
        {"dataPath", Utf8(dataFile)}, {"sourceUnchanged", sourceUnchanged}};
    VariantArray walls, cells, junctions, boundarySpans;
    double materialArea = 0;
    int interfaces = 0;
    if (complete) {
        const auto built = O::ReconstructOpenCascadeShape(result.Geometry);
        if (!built.bOK || built.Shape.IsNull() || built.Faces.size() != result.Cells.size() || result.Cells.empty())
            throw std::runtime_error("Region partition output does not reconstruct all material cells");

        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        std::set<std::uint64_t> faceIds;
        std::map<int, VariantArray> wallCells;
        std::map<int, double> wallAreas;
        std::vector<std::vector<int>> junctionOwners;
        std::vector<VariantArray> junctionCells(result.Junctions.size());
        for (const auto& wall : result.Walls) {
            if (!wallCells.emplace(wall.Id, VariantArray{}).second)
                throw std::runtime_error("Region partition output contains duplicate wall IDs");
        }
        for (const auto& junction : result.Junctions) {
            auto owners = junction.WallIds;
            std::sort(owners.begin(), owners.end());
            if (owners.size() < 2 || std::adjacent_find(owners.begin(), owners.end()) != owners.end())
                throw std::runtime_error("Region partition junction has invalid ownership");
            junctionOwners.push_back(std::move(owners));
        }
        for (std::size_t i = 0; i < result.Cells.size(); ++i) {
            const auto& cell = result.Cells[i];
            const auto key = built.Faces.find(cell.FaceId);
            if (key == built.Faces.end() || !faceIds.insert(cell.FaceId).second ||
                cell.WallIds.empty() || !std::isfinite(cell.Area) || cell.Area <= 0)
                throw std::runtime_error("Region partition cell has invalid face, owner, or area data");
            const int id = static_cast<int>(i + 1);
            VariantArray owners;
            auto ownerIds = cell.WallIds;
            std::sort(ownerIds.begin(), ownerIds.end());
            if (std::adjacent_find(ownerIds.begin(), ownerIds.end()) != ownerIds.end())
                throw std::runtime_error("Region partition cell repeats a wall owner");
            for (const int owner : ownerIds) {
                if (!wallCells.contains(owner)) throw std::runtime_error("Region partition cell refers to an unknown wall");
                owners.emplace_back(owner);
                wallCells.at(owner).emplace_back(id);
                wallAreas[owner] += cell.Area;
            }
            ObjectMap record{{"id", id}, {"faceId", std::to_string(cell.FaceId)}, {"area", cell.Area},
                {"ownerWallIds", owners}, {"displayWires", PreviewWires(key->second)}};
            if (ownerIds.size() > 1) {
                if (cell.JunctionId < 1 || static_cast<std::size_t>(cell.JunctionId) > junctionOwners.size() ||
                    junctionOwners[cell.JunctionId - 1] != ownerIds)
                    throw std::runtime_error("Shared cell has no matching native junction");
                record["junctionId"] = cell.JunctionId;
                junctionCells.at(cell.JunctionId - 1).emplace_back(id);
            } else if (cell.JunctionId != 0) {
                throw std::runtime_error("Exclusive cell has a shared junction");
            }
            cells.emplace_back(std::move(record));
            builder.Add(compound, key->second);
            materialArea += cell.Area;
        }
        for (const auto& wall : result.Walls) {
            VariantArray sources;
            for (const auto id : wall.SourceEdgeIds) sources.emplace_back(std::to_string(id));
            walls.emplace_back(ObjectMap{{"id", wall.Id}, {"label", wall.Label}, {"sourceEdgeIds", sources},
                {"area", wallAreas[wall.Id]}, {"cellIds", wallCells.at(wall.Id)}});
        }
        for (std::size_t i = 0; i < result.Junctions.size(); ++i) {
            const auto& junction = result.Junctions[i];
            VariantArray owners;
            for (const int owner : junction.WallIds) owners.emplace_back(owner);
            junctions.emplace_back(ObjectMap{{"id", static_cast<int>(i + 1)}, {"label", junction.Label},
                {"wallIds", owners}, {"cellIds", junctionCells[i]}});
        }
        for (const auto& boundary : result.BoundarySpans) {
            const auto owner = built.Faces.find(boundary.CellFaceId);
            const auto edge = built.Edges.find(boundary.EdgeId);
            if (owner == built.Faces.end() || edge == built.Edges.end() ||
                !std::isfinite(boundary.FirstParameter) || !std::isfinite(boundary.LastParameter) ||
                boundary.FirstParameter >= boundary.LastParameter)
                throw std::runtime_error("Invalid native section boundary span");
            bool onCell = false;
            for (TopExp_Explorer uses(owner->second, TopAbs_EDGE); uses.More(); uses.Next())
                if (uses.Current().IsSame(edge->second)) {
                    onCell = true;
                    break;
                }
            if (!onCell || (boundary.ContourSpans.empty() == boundary.AdjacentCellFaceIds.empty()))
                throw std::runtime_error("Section boundary span has inconsistent cell ownership");
            VariantArray adjacent, contour;
            for (const auto faceId : boundary.AdjacentCellFaceIds) {
                if (faceId == boundary.CellFaceId || !faceIds.contains(faceId))
                    throw std::runtime_error("Unknown adjacent section cell face");
                adjacent.emplace_back(std::to_string(faceId));
            }
            if (!adjacent.empty()) ++interfaces;
            for (const auto& source : boundary.ContourSpans) {
                const auto& original = section.Wires.at(source.WireIndex).Edges.at(source.EdgeIndex);
                if (source.SourceEdgeId != original.SourceEdgeId || source.SourceFaceId != original.SourceFaceId ||
                    !std::isfinite(source.FirstParameter) || !std::isfinite(source.LastParameter) ||
                    source.FirstParameter >= source.LastParameter)
                    throw std::runtime_error("Section boundary span does not preserve original contour identity");
                contour.emplace_back(ObjectMap{{"wireIndex", static_cast<int>(source.WireIndex)},
                    {"edgeIndex", static_cast<int>(source.EdgeIndex)}, {"sourceEdgeId", std::to_string(source.SourceEdgeId)},
                    {"sourceFaceId", std::to_string(source.SourceFaceId)}, {"firstParameter", source.FirstParameter},
                    {"lastParameter", source.LastParameter}});
            }
            boundarySpans.emplace_back(ObjectMap{{"cellFaceId", std::to_string(boundary.CellFaceId)},
                {"edgeId", std::to_string(boundary.EdgeId)}, {"firstParameter", boundary.FirstParameter},
                {"lastParameter", boundary.LastParameter}, {"adjacentCellFaceIds", std::move(adjacent)},
                {"contourSpans", std::move(contour)}});
        }

        std::stringstream serialized;
        BRepTools::Write(compound, serialized);
        TopoDS_Shape reread;
        BRepTools::Read(reread, serialized, builder);
        const int faceCount = Count(reread, TopAbs_FACE);
        const double rereadArea = NativeArea(reread);
        if (reread.IsNull() || !BRepCheck_Analyzer(reread).IsValid() ||
            faceCount != static_cast<int>(result.Cells.size()) || Relative(materialArea, rereadArea) > 1e-7)
            throw std::runtime_error("Region BREP reread did not preserve the validated material cells");
        std::ofstream stream(brepFile, std::ios::binary | std::ios::trunc);
        stream << serialized.str();
        stream.flush();
        if (!stream) throw std::runtime_error("Cannot write region BREP");
        document["brepPath"] = Utf8(brepFile);
        document["reread"] = ObjectMap{{"valid", true}, {"faceCount", faceCount}, {"area", rereadArea}};
        audit["brepPath"] = Utf8(brepFile);
        audit["brepRoundTripPassed"] = true;
    }
    const auto counts = ObjectMap{{"walls", static_cast<int>(walls.size())},
        {"cells", static_cast<int>(cells.size())}, {"junctions", static_cast<int>(junctions.size())},
        {"boundarySpans", static_cast<int>(boundarySpans.size())}, {"interfaceSpans", interfaces}};
    document["counts"] = counts;
    document["walls"] = walls;
    document["cells"] = cells;
    document["junctions"] = junctions;
    document["boundarySpans"] = std::move(boundarySpans);
    document["boundaryProvenanceComplete"] = result.bBoundaryProvenanceComplete;
    document["materialArea"] = materialArea;
    document["sharedArea"] = result.SharedArea;
    document["previewOnlySamples"] = true;
    audit["counts"] = counts;
    audit["materialArea"] = materialArea;
    audit["sharedArea"] = result.SharedArea;
    audit["boundaryProvenanceComplete"] = result.bBoundaryProvenanceComplete;
    Save(dataFile, document);
    return audit;
}

} // namespace section_region_export
