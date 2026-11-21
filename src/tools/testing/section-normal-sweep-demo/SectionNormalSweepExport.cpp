#define SECTION_REGION_QUASI_SUPPORT 1
#define main sectionRegionToolMain
#include "../../../../output/tests/section-regions-native-20261113/SectionRegions.cpp"
#undef main

namespace {

sr::P coordinates(const sr::Domain& domain, const sr::P& point, bool worldXY) {
    if (!worldXY) return point;
    const auto world = gp_Pnt(point.X(), point.Y(), 0).Transformed(domain.toSource);
    return {world.X(), world.Y()};
}

gp_Vec2d direction(const sr::Domain& domain, gp_Vec2d vector, bool worldXY) {
    if (!worldXY) return vector;
    const auto world = gp_Vec(vector.X(), vector.Y(), 0).Transformed(domain.toSource);
    return {world.X(), world.Y()};
}

void writeCurve(std::ostream& out, const sr::Domain& domain, const sr::Source& source,
                bool worldXY) {
    const double start = source.reverse ? source.last : source.first;
    const double end = source.reverse ? source.first : source.last;
    const sr::P a = coordinates(domain, source.curve->Value(start), worldXY);
    const sr::P b = coordinates(domain, source.curve->Value(end), worldXY);
    const Geom2dAdaptor_Curve curve(source.curve, source.first, source.last);
    out << "{\"id\":" << source.index << ",\"fitBoundMm\":" << source.fitBound;
    if (curve.GetType() == GeomAbs_Line) {
        out << ",\"kind\":\"line\",\"a\":";
        sr::point(out, a);
        out << ",\"b\":";
        sr::point(out, b);
    } else if (curve.GetType() == GeomAbs_Circle) {
        const gp_Circ2d circle = curve.Circle();
        const sr::P center = coordinates(domain, circle.Location(), worldXY);
        const gp_Vec2d radial(center, a);
        sr::P local;
        gp_Vec2d tangent;
        source.curve->D1(start, local, tangent);
        if (source.reverse) tangent.Reverse();
        tangent = direction(domain, tangent, worldXY);
        const double sweep = std::copysign(source.last - source.first, radial.Crossed(tangent));
        const double a0 = std::atan2(radial.Y(), radial.X());
        const sr::P expected(center.X() + circle.Radius() * std::cos(a0 + sweep),
                             center.Y() + circle.Radius() * std::sin(a0 + sweep));
        if (expected.Distance(b) > 1e-7) throw std::runtime_error("Arc sign or trim mismatch");
        out << ",\"kind\":\"arc\",\"center\":";
        sr::point(out, center);
        out << ",\"radius\":" << circle.Radius() << ",\"a0\":" << a0
            << ",\"sweep\":" << sweep;
    } else {
        throw std::runtime_error("Demo supports only native analytic line and circle fits");
    }
    out << '}';
}

void writeRings(std::ostream& out, const sr::Domain& domain, bool worldXY) {
    out << std::setprecision(17) << "{\"nativeToleranceMm\":" << domain.tol
        << ",\"nativeMaterialAreaMm2\":" << sr::area(domain.input) << ",\"rings\":[";
    int wire = 0;
    for (const auto& face : domain.faces) {
        const auto outer = BRepTools::OuterWire(face);
        for (TopExp_Explorer it(face, TopAbs_WIRE); it.More(); it.Next(), ++wire) {
            if (wire) out << ',';
            out << "{\"id\":" << wire << ",\"role\":"
                << std::quoted(it.Current().IsSame(outer) ? "outer" : "inner") << ",\"curves\":[";
            bool first = true;
            for (const auto& source : domain.sources) {
                if (source.wire != wire) continue;
                if (!first) out << ',';
                first = false;
                writeCurve(out, domain, source, worldXY);
            }
            if (first) throw std::runtime_error("Section wire has no exportable curves");
            out << "]}";
        }
    }
    out << "]}\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 6) {
        std::cerr << "SectionNormalSweepExport input output [endX declaredGap edgeIdentities]\n";
        return 1;
    }
    try {
        const bool fixture = argc == 3;
        const auto domain = sr::read(argv[1], fixture ? std::nullopt : std::optional{std::stod(argv[3])},
                                     fixture ? 0 : std::stod(argv[4]), fixture ? "" : argv[5]);
        std::ofstream out(argv[2]);
        writeRings(out, domain, fixture);
        if (!out) throw std::runtime_error("Could not write analytic section");
        return 0;
    } catch (const Standard_Failure& error) {
        std::cerr << error.GetMessageString() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return 2;
}
