namespace section_partition_audit {

struct SectionFixture final {
    std::string Name;
    R::SSectionWiresResult Section;
    double Area;
    GeomAbs_CurveType BoundaryType;
};

R::SSectionWire MakeEllipse(double major, double minor, bool inner) {
    G::Ellipse2 ellipse;
    ellipse.Placement.Location = {1.5, -2.25};
    ellipse.Placement.XDirection = {0.6, 0.8};
    ellipse.Placement.YDirection = {-0.8, 0.6};
    ellipse.MajorRadius = major;
    ellipse.MinorRadius = minor;
    R::SSectionWireEdge edge;
    edge.Curve = ellipse;
    edge.bReversed = inner;
    edge.Range = {0, 2 * machining_path_audit::Pi};
    const auto curve = O::BuildOpenCascadeCurve2(edge.Curve);
    const auto start = curve->Value(edge.bReversed ? edge.Range.Last : edge.Range.First);
    const auto end = curve->Value(edge.bReversed ? edge.Range.First : edge.Range.Last);
    edge.Start = {start.X(), start.Y()};
    edge.End = {end.X(), end.Y()};
    R::SSectionWire wire;
    wire.bInner = inner;
    wire.bClosed = true;
    wire.dSignedArea = (inner ? -1 : 1) * machining_path_audit::Pi * major * minor;
    wire.Edges.push_back(std::move(edge));
    return wire;
}

SectionFixture EllipseAnnulus() {
    R::SSectionWiresResult section;
    section.bSuccess = true;
    section.Wires = {MakeEllipse(12, 8, false), MakeEllipse(9, 6, true)};
    return {"elliptical-annulus", std::move(section), 42 * machining_path_audit::Pi, GeomAbs_Ellipse};
}

TopoDS_Shape MakeStock(R::SSectionWiresResult& section) {
    const gp_Pln plane(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0)));
    std::vector<TopoDS_Wire> holes;
    TopoDS_Wire outer;
    std::uint64_t nextEdgeId = 1;
    for (auto& wire : section.Wires) {
        BRepBuilderAPI_MakeWire boundary;
        for (auto& edge : wire.Edges) {
            const auto curve = GeomAPI::To3d(O::BuildOpenCascadeCurve2(edge.Curve), plane);
            BRepBuilderAPI_MakeEdge maker(curve, edge.Range.First, edge.Range.Last);
            if (!maker.IsDone()) throw std::runtime_error("Could not construct fixture section edge");
            auto trimmed = maker.Edge();
            if (edge.bReversed) trimmed.Reverse();
            boundary.Add(trimmed);
            edge.SourceEdgeId = nextEdgeId++;
        }
        if (!boundary.IsDone()) throw std::runtime_error("Could not construct fixture section wire");
        if (wire.bInner) holes.push_back(boundary.Wire());
        else outer = boundary.Wire();
    }
    BRepBuilderAPI_MakeFace material(plane, outer, true);
    for (const auto& hole : holes) material.Add(hole);
    if (!material.IsDone() || !BRepCheck_Analyzer(material.Face()).IsValid())
        throw std::runtime_error("Invalid native fixture section material");
    return BRepPrimAPI_MakePrism(material.Face(), gp_Vec(25, 0, 0)).Shape();
}

ObjectMap CheckObliqueT(const fs::path& output, double tolerance) {
    auto section = machining_path_audit::PolygonSection({
        {-50, 0}, {-5, 0}, {-5, -40}, {5, -65}, {5, 0}, {50, 0}, {50, 10}, {-50, 10}});
    const auto stock = MakeStock(section);
    const auto classification = machining_path_audit::AllMachining(stock);
    const auto before = G::Persistence::Serialize(classification.SplitGeometry);
    const auto result = R::SectionRegionPartitionWork::Run(section, classification, tolerance);
    const bool unchanged = before == G::Persistence::Serialize(classification.SplitGeometry);
    const auto report = section_region_export::Export(result, output / "oblique-tee", {}, "oblique-tee",
        "part-1", {}, {}, section, classification, tolerance, unchanged);
    const auto cellForArea = [&](double area) {
        return std::ranges::find_if(result.Cells, [&](const auto& cell) {
            return Relative(area, cell.Area) <= 1e-8;
        });
    };
    const auto flange = cellForArea(1000), web = cellForArea(525);
    bool reciprocal = false;
    bool separated = false;
    if (flange != result.Cells.end() && web != result.Cells.end() &&
        flange->WallIds.size() == 1 && web->WallIds.size() == 1 && flange->WallIds != web->WallIds) {
        const auto shapes = O::ReconstructOpenCascadeShape(result.Geometry);
        constexpr double spatialAccuracy = 1e-6;
        Bnd_Box flangeBounds, webBounds;
        BRepBndLib::AddOptimal(shapes.Faces.at(flange->FaceId), flangeBounds, false, false);
        BRepBndLib::AddOptimal(shapes.Faces.at(web->FaceId), webBounds, false, false);
        separated = std::abs(flangeBounds.CornerMin().Z()) <= spatialAccuracy &&
            std::abs(webBounds.CornerMax().Z()) <= spatialAccuracy;
        std::set<std::uint64_t> owners;
        for (const auto& span : result.BoundarySpans) {
            if (span.AdjacentCellFaceIds.empty()) continue;
            const auto previous = span.CellFaceId == flange->FaceId ? flange : web;
            const auto next = span.CellFaceId == flange->FaceId ? web : flange;
            if (span.CellFaceId != previous->FaceId ||
                span.AdjacentCellFaceIds != std::vector<std::uint64_t>{next->FaceId})
                continue;
            const BRepAdaptor_Curve curve(shapes.Edges.at(span.EdgeId));
            auto a = curve.Value(span.FirstParameter), b = curve.Value(span.LastParameter);
            if (b.Y() < a.Y()) std::swap(a, b);
            if (curve.GetType() == GeomAbs_Line && a.Distance(gp_Pnt(0, -5, 0)) <= spatialAccuracy &&
                b.Distance(gp_Pnt(0, 5, 0)) <= spatialAccuracy)
                owners.insert(span.CellFaceId);
        }
        reciprocal = owners.size() == 2;
    }
    const bool passed = result.Status == R::ESectionRegionPartitionStatus::Complete &&
        result.Walls.size() == 2 && result.Cells.size() == 2 && result.Junctions.empty() &&
        separated && reciprocal && unchanged && result.bBoundaryProvenanceComplete &&
        report.at("brepRoundTripPassed").To<bool>();
    return {{"name", std::string("oblique-tee")}, {"passed", passed}, {"status", report.at("resultStatus")},
        {"reason", result.Reason}, {"flangeArea", flange == result.Cells.end() ? 0 : flange->Area},
        {"webArea", web == result.Cells.end() ? 0 : web->Area}, {"wallSeparation", separated},
        {"reciprocalInterface", reciprocal}, {"sourceUnchanged", unchanged},
        {"completeBoundaryProvenance", result.bBoundaryProvenanceComplete}};
}

int Run(const fs::path& output) {
    const double tolerance = R::SSectionWireOptions{}.dConnectionTolerance;
    VariantArray rows;
    bool passed = true;
    for (auto fixture : {SectionFixture{"rectangular-solid", machining_path_audit::RectangleSection(),
                             100, GeomAbs_Line}, EllipseAnnulus()}) {
        const auto stock = MakeStock(fixture.Section);
        const auto classification = machining_path_audit::AllMachining(stock);
        const auto before = G::Persistence::Serialize(classification.SplitGeometry);
        const auto result = R::SectionRegionPartitionWork::Run(fixture.Section, classification, tolerance);
        const bool unchanged = before == G::Persistence::Serialize(classification.SplitGeometry);
        const bool complete = result.Status == R::ESectionRegionPartitionStatus::Complete;
        const auto prefix = output / fixture.Name;
        const auto report = section_region_export::Export(result, prefix, {}, fixture.Name,
            "part-1", {}, {}, fixture.Section, classification, tolerance, unchanged);

        std::set<std::uint64_t> expectedSources, actualSources;
        for (const auto& wire : fixture.Section.Wires)
            for (const auto& edge : wire.Edges) expectedSources.insert(edge.SourceEdgeId);
        bool onlyPhysical = true;
        for (const auto& span : result.BoundarySpans) {
            onlyPhysical &= span.AdjacentCellFaceIds.empty() && !span.ContourSpans.empty();
            for (const auto& original : span.ContourSpans) actualSources.insert(original.SourceEdgeId);
        }
        bool nativeCurves = false;
        if (complete) {
            const auto shapes = O::ReconstructOpenCascadeShape(result.Geometry);
            nativeCurves = shapes.bOK && !shapes.Edges.empty() &&
                std::ranges::all_of(shapes.Edges, [&](const auto& item) {
                return BRepAdaptor_Curve(item.second).GetType() == fixture.BoundaryType;
            });
        }
        const double area = result.Cells.size() == 1 ? result.Cells.front().Area : 0;
        const bool valid = complete && result.Walls.size() == 1 && result.Cells.size() == 1 &&
            result.Cells.front().WallIds == std::vector<int>{result.Walls.front().Id} &&
            result.Junctions.empty() && Relative(fixture.Area, area) <= 1e-8 && unchanged &&
            nativeCurves && onlyPhysical && actualSources == expectedSources &&
            result.bBoundaryProvenanceComplete && report.at("brepRoundTripPassed").To<bool>();
        passed &= valid;
        rows.emplace_back(ObjectMap{{"name", fixture.Name}, {"passed", valid},
            {"status", report.at("resultStatus")}, {"reason", result.Reason},
            {"expectedArea", fixture.Area}, {"area", area}, {"nativeCurves", nativeCurves},
            {"sourceUnchanged", unchanged}, {"completeBoundaryProvenance", result.bBoundaryProvenanceComplete},
            {"sourceEdgeCount", static_cast<int>(actualSources.size())}});
    }
    auto tee = CheckObliqueT(output, tolerance);
    passed &= tee.at("passed").To<bool>();
    rows.emplace_back(std::move(tee));
    Save(output / "summary.json", {{"passed", passed}, {"fixtures", std::move(rows)}});
    return passed ? 0 : 1;
}

}
