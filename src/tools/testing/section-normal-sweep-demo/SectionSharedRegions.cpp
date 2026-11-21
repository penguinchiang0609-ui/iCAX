#define SECTION_REGION_QUASI_SUPPORT 1
#define main sectionRegionToolMain
#include "../../../../output/tests/section-regions-native-20261113/SectionRegions.cpp"
#undef main

namespace {

struct PatchRule {
    int curveId;
    std::vector<int> owners;
};

struct Plan {
    std::vector<std::vector<sr::P>> polygons;
    std::vector<PatchRule> patches;
};

struct MaterialPatch {
    TopoDS_Shape shape;
    std::vector<int> curves;
    std::vector<int> owners;
};

int readCount(std::istream& input, int minimum, int maximum, const char* label) {
    int count;
    if (!(input >> count) || count < minimum || count > maximum) {
        throw std::runtime_error(std::string("Invalid coverage plan ") + label);
    }
    return count;
}

Plan readPlan(const std::string& filename, const sr::Domain& domain) {
    std::ifstream input(filename);
    std::string signature;
    if (!(input >> signature) || signature != "ICAX_SHARED_COVERAGE_V1") {
        throw std::runtime_error("Unsupported coverage plan format");
    }

    Plan plan;
    const int count = readCount(input, 1, 10000, "region count");
    for (int region = 0; region < count; ++region) {
        const int vertexCount = readCount(input, 0, 100000, "vertex count");
        if ((vertexCount == 0 && count != 1) || vertexCount == 1 || vertexCount == 2) {
            throw std::runtime_error("Coverage polygons require at least three vertices");
        }
        auto& points = plan.polygons.emplace_back();
        for (int v = 0; v < vertexCount; ++v) {
            double y, z;
            if (!(input >> y >> z) || !std::isfinite(y) || !std::isfinite(z)) {
                throw std::runtime_error("Invalid coverage plan vertex");
            }
            points.emplace_back(y, z);
        }
    }

    std::set<int> uniqueCurves;
    const auto curveCount = static_cast<int>(domain.sources.size());
    const int ruleCount = readCount(input, 0, curveCount, "patch rule count");
    for (int rule = 0; rule < ruleCount; ++rule) {
        const int curveId = readCount(input, 0, curveCount - 1, "patch curve ID");
        const int ownerCount = readCount(input, 2, count, "patch owner count");
        if (!uniqueCurves.insert(curveId).second) {
            throw std::runtime_error("Duplicate patch curve ID in coverage plan");
        }
        std::vector<int> owners;
        for (int owner = 0; owner < ownerCount; ++owner) {
            owners.push_back(readCount(input, 1, count, "patch owner ID"));
        }
        std::sort(owners.begin(), owners.end());
        if (std::adjacent_find(owners.begin(), owners.end()) != owners.end()) {
            throw std::runtime_error("Duplicate patch owner ID in coverage plan");
        }
        plan.patches.push_back({curveId, std::move(owners)});
    }
    std::string trailing;
    if (input >> trailing) throw std::runtime_error("Unexpected data after coverage plan");
    return plan;
}

struct SectionTransform {
    gp_Trsf planToLocal;
    gp_Trsf localToAligned;
};

SectionTransform sectionTransform(const sr::Domain& domain, bool fixture) {
    if (!fixture) return {gp_Trsf{}, domain.toSource};
    const auto normal = gp_Vec(0, 0, 1).Transformed(domain.toSource);
    if (std::hypot(normal.X(), normal.Y()) > 1.e-8) {
        throw std::runtime_error("Fixture is not in the exported world XY plane");
    }
    const auto sourceOrigin = gp_Pnt(0, 0, 0).Transformed(domain.toSource);
    gp_Trsf planToSource;
    planToSource.SetTranslation(gp_Vec(0, 0, sourceOrigin.Z()));
    gp_Trsf sourceToAligned;
    sourceToAligned.SetValues(0, 0, 1, -sourceOrigin.Z(), 1, 0, 0, 0, 0, 1, 0, 0);
    return {domain.toSource.Inverted() * planToSource, sourceToAligned * domain.toSource};
}

TopoDS_Face polygonFace(const std::vector<sr::P>& points, const gp_Trsf& planToLocal) {
    BRepBuilderAPI_MakeWire wire;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        BRepBuilderAPI_MakeEdge edge(gp_Pnt(a.X(), a.Y(), 0).Transformed(planToLocal),
                                     gp_Pnt(b.X(), b.Y(), 0).Transformed(planToLocal));
        if (!edge.IsDone()) throw std::runtime_error("Invalid edge in coverage polygon");
        wire.Add(edge.Edge());
    }
    if (!wire.IsDone()) throw std::runtime_error("Coverage polygon does not form a wire");
    BRepBuilderAPI_MakeFace face(sr::XY, wire.Wire(), true);
    if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid()) {
        throw std::runtime_error("Invalid or self-intersecting coverage polygon");
    }
    return face.Face();
}

TopoDS_Shape nativeCurve(const sr::Domain& domain, int curveId) {
    const auto& fitted = domain.sources.at(curveId);
    if (fitted.index != curveId || fitted.sourceIndices.empty()) {
        throw std::runtime_error("Coverage plan patch curve lacks original span identities");
    }

    // The fitted ID selects native references. Intersect those original trims,
    // since a fitted circle alone cannot prove contact with the source skin.
    std::vector<TopoDS_Shape> edges;
    for (int referenceId : fitted.sourceIndices) {
        const auto& reference = domain.referenceSources.at(referenceId);
        const auto curve = GeomAPI::To3d(reference.curve, sr::XY);
        BRepBuilderAPI_MakeEdge edge(curve, reference.first, reference.last);
        if (!edge.IsDone()) throw std::runtime_error("Could not rebuild native patch evidence edge");
        edges.push_back(edge.Edge());
    }
    return sr::compound(edges);
}

TopoDS_Shape unionOf(const std::vector<TopoDS_Shape>& shapes, double tolerance) {
    TopoDS_Shape result = shapes.front();
    for (std::size_t i = 1; i < shapes.size(); ++i) {
        result = sr::binary(result, shapes[i], 2, tolerance);
    }
    return result;
}

double differenceArea(const TopoDS_Shape& from, const TopoDS_Shape& cut, double tolerance) {
    if (from.IsSame(cut)) return 0;
    return sr::area(sr::binary(from, cut, 1, tolerance));
}

std::vector<MaterialPatch> recoverPatches(const sr::Domain& domain, const Plan& plan,
                                           const std::vector<TopoDS_Shape>& cores,
                                           const TopoDS_Shape& coreUnion, double minArea) {
    if (domain.input.IsSame(coreUnion)) return {};
    std::vector<TopoDS_Shape> evidence;
    for (const auto& rule : plan.patches) evidence.push_back(nativeCurve(domain, rule.curveId));

    const auto leftover = sr::binary(domain.input, coreUnion, 1, domain.tol);
    std::vector<MaterialPatch> result;
    for (TopExp_Explorer face(leftover, TopAbs_FACE); face.More(); face.Next()) {
        const auto& shape = face.Current();
        if (sr::area(shape) <= minArea) continue;
        MaterialPatch patch{shape};
        for (std::size_t i = 0; i < cores.size(); ++i) {
            if (sr::sharesBoundaryInterval(shape, cores[i], domain.tol)) {
                patch.owners.push_back(static_cast<int>(i + 1));
            }
        }
        for (std::size_t i = 0; i < evidence.size(); ++i) {
            if (!sr::sharesBoundaryInterval(shape, evidence[i], domain.tol)) continue;
            const auto& rule = plan.patches[i];
            if (patch.owners != rule.owners) {
                throw std::runtime_error("Native patch adjacency does not uniquely prove the declared owners");
            }
            patch.curves.push_back(rule.curveId);
        }
        if (patch.curves.empty()) {
            throw std::runtime_error("Uncovered material has no accepted source interval for a junction patch");
        }
        result.push_back(std::move(patch));
    }
    return result;
}

void writeIds(std::ostream& out, const std::vector<int>& ids) {
    out << '[';
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) out << ',';
        out << ids[i];
    }
    out << ']';
}

void run(const sr::Domain& domain, const Plan& plan, const std::string& prefix, bool fixture) {
    const auto before = sr::serialize(domain.input);
    const auto transform = sectionTransform(domain, fixture);
    const double materialArea = sr::area(domain.input);
    const double minArea = std::max(4 * domain.tol * domain.tol,
        128 * std::numeric_limits<double>::epsilon() * materialArea);
    const double areaTolerance = std::max(16 * minArea, 20 * domain.tol * domain.diag);

    std::vector<TopoDS_Shape> cores;
    for (const auto& polygon : plan.polygons) {
        auto core = polygon.empty() ? domain.input :
            sr::binary(domain.input, polygonFace(polygon, transform.planToLocal), 0, domain.tol);
        if (sr::area(core) <= minArea || !BRepCheck_Analyzer(core).IsValid()) {
            throw std::runtime_error("Coverage polygon has no valid positive-area intersection with original material");
        }
        cores.push_back(std::move(core));
    }

    const auto coreUnion = unionOf(cores, domain.tol);
    const auto patches = recoverPatches(domain, plan, cores, coreUnion, minArea);
    auto regions = cores;
    for (const auto& patch : patches) {
        for (int owner : patch.owners) {
            auto& region = regions.at(owner - 1);
            region = sr::binary(region, patch.shape, 2, domain.tol);
        }
    }
    for (const auto& region : regions) {
        if (!BRepCheck_Analyzer(region).IsValid() || sr::connectedFaceParts(region).size() != 1) {
            throw std::runtime_error("Coverage region must be a connected valid native face set");
        }
    }

    const auto regionUnion = unionOf(regions, domain.tol);
    const double missing = differenceArea(domain.input, regionUnion, domain.tol);
    const double excess = differenceArea(regionUnion, domain.input, domain.tol);
    if (missing > areaTolerance || excess > areaTolerance) {
        std::ostringstream message;
        message << std::setprecision(17) << "Section material coverage failed: missing=" << missing
                << " excess=" << excess << " tolerance=" << areaTolerance;
        throw std::runtime_error(message.str());
    }
    if (sr::serialize(domain.input) != before) throw std::runtime_error("Candidate builder mutated original material");

    const auto originalOrder = sr::compound(regions);
    BRepBuilderAPI_Transform aligned(originalOrder, transform.localToAligned, true);
    if (!aligned.IsDone() || !BRepCheck_Analyzer(aligned.Shape()).IsValid() ||
        !BRepTools::Write(aligned.Shape(), (prefix + ".brep").c_str())) {
        throw std::runtime_error("Could not save valid aligned coverage BREP");
    }

    std::ofstream out(prefix + ".json");
    out << std::setprecision(17) << "{\"materialArea\":" << materialArea
        << ",\"missingArea\":" << missing << ",\"excessArea\":" << excess
        << ",\"nativeTolerance\":" << domain.tol << ",\"areaTolerance\":" << areaTolerance
        << ",\"regionAreas\":[";
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (i) out << ',';
        out << sr::area(regions[i]);
    }
    out << "],\"patches\":[";
    for (std::size_t i = 0; i < patches.size(); ++i) {
        if (i) out << ',';
        const auto& patch = patches[i];
        out << "{\"area\":" << sr::area(patch.shape) << ",\"curveIds\":";
        writeIds(out, patch.curves);
        out << ",\"ownerWallIds\":";
        writeIds(out, patch.owners);
        out << '}';
    }
    out << "]}\n";
    if (!out) throw std::runtime_error("Could not save native coverage evidence");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 7) {
        std::cerr << "SectionSharedRegions originalInput planFile outputPrefix [endX declaredGap edgeIdentities]\n";
        return 1;
    }
    try {
        const bool fixture = argc == 4;
        const auto domain = sr::read(argv[1], fixture ? std::nullopt : std::optional{std::stod(argv[4])},
                                     fixture ? 0 : std::stod(argv[5]), fixture ? "" : argv[6]);
        run(domain, readPlan(argv[2], domain), argv[3], fixture);
        return 0;
    } catch (const Standard_Failure& error) {
        std::cerr << error.GetMessageString() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return 2;
}
