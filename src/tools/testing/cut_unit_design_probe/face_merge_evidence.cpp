// Whole-support plane certificates for stage-one cut-unit merging.
// No source topology, surface, edge, tolerance or triangulation is modified.
#include <STEPControl_Reader.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;
constexpr double planeTolerance = 1e-7;
constexpr double parameterTolerance = 1e-10;

std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
std::string quoted(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        result += c < 32 ? ' ' : c;
    }
    return result + '"';
}
void point(std::ostream& output, const gp_Pnt& p) {
    output << '[' << p.X() << ',' << p.Y() << ',' << p.Z() << ']';
}
gp_Dir canonicalNormal(const gp_Dir& input) {
    std::array<double, 3> v{input.X(), input.Y(), input.Z()};
    const auto index = static_cast<size_t>(std::max_element(v.begin(), v.end(),
        [](double a, double b) { return std::abs(a) < std::abs(b); }) - v.begin());
    return v[index] < 0 ? input.Reversed() : input;
}

struct Certificate {
    bool planar = false, positiveWeights = true, parameterDomainCovered = false;
    std::string method, reason;
    gp_Pnt origin;
    gp_Dir normal{0, 0, 1};
    double maximumDeviation = 0;
    int controlPointCount = 0;
    std::vector<gp_Pnt> points;
};

bool coveredDomain(const TopoDS_Face& face, const Handle(Geom_Surface)& surface) {
    double u0, u1, v0, v1, a, b, c, d;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    surface->Bounds(a, b, c, d);
    if (!std::isfinite(u0) || !std::isfinite(u1) || !std::isfinite(v0) || !std::isfinite(v1)) return false;
    const bool uCovered = surface->IsUPeriodic() || (u0 >= a - parameterTolerance && u1 <= b + parameterTolerance);
    const bool vCovered = surface->IsVPeriodic() || (v0 >= c - parameterTolerance && v1 <= d + parameterTolerance);
    return uCovered && vCovered;
}

Certificate certificate(const TopoDS_Face& face) {
    Certificate result;
    BRepAdaptor_Surface adaptor(face, true);
    if (adaptor.GetType() == GeomAbs_Plane) {
        result.method = "AnalyticPlaneFiniteUVEnvelope";
        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        if (!std::isfinite(u0) || !std::isfinite(u1) || !std::isfinite(v0) || !std::isfinite(v1)) {
            result.reason = "unbounded-face-parameter-envelope";
            return result;
        }
        const auto plane = adaptor.Plane();
        result.origin = plane.Location();
        result.normal = canonicalNormal(plane.Axis().Direction());
        for (double u : {u0, u1}) for (double v : {v0, v1}) result.points.push_back(adaptor.Value(u, v));
        for (const auto& p : result.points) result.maximumDeviation = std::max(result.maximumDeviation, plane.Distance(p));
        result.parameterDomainCovered = true;
        result.planar = result.maximumDeviation <= planeTolerance;
        if (!result.planar) result.reason = "analytic-plane-envelope-outside-tolerance";
        return result;
    }

    TopLoc_Location location;
    Handle(Geom_Surface) surface = BRep_Tool::Surface(face, location);
    while (!surface.IsNull()) {
        const auto trimmed = Handle(Geom_RectangularTrimmedSurface)::DownCast(surface);
        if (trimmed.IsNull()) break;
        surface = trimmed->BasisSurface();
    }
    if (surface.IsNull()) { result.reason = "missing-source-surface"; return result; }
    const auto spline = Handle(Geom_BSplineSurface)::DownCast(surface);
    const auto bezier = Handle(Geom_BezierSurface)::DownCast(surface);
    if (spline.IsNull() && bezier.IsNull()) {
        result.method = "UnsupportedNonplanarSurfaceType";
        result.reason = "whole-surface-plane-not-certified";
        return result;
    }
    result.method = spline.IsNull() ? "PositiveWeightBezierControlHull" : "PositiveWeightBSplineControlHull";
    result.parameterDomainCovered = coveredDomain(face, surface);
    if (!result.parameterDomainCovered) {
        result.reason = "trim-domain-outside-certified-control-hull-domain";
        return result;
    }
    const int nu = spline.IsNull() ? bezier->NbUPoles() : spline->NbUPoles();
    const int nv = spline.IsNull() ? bezier->NbVPoles() : spline->NbVPoles();
    for (int u = 1; u <= nu; ++u) for (int v = 1; v <= nv; ++v) {
        const double weight = spline.IsNull() ? bezier->Weight(u, v) : spline->Weight(u, v);
        if (!std::isfinite(weight) || weight <= 0) result.positiveWeights = false;
        const auto p = spline.IsNull() ? bezier->Pole(u, v) : spline->Pole(u, v);
        result.points.push_back(p.Transformed(location.Transformation()));
    }
    result.controlPointCount = static_cast<int>(result.points.size());
    if (!result.positiveWeights) { result.reason = "control-hull-requires-positive-weights"; return result; }
    if (result.points.size() < 3) { result.reason = "insufficient-control-points"; return result; }
    result.origin = result.points.front();
    auto farthest = std::max_element(result.points.begin(), result.points.end(), [&](const gp_Pnt& a, const gp_Pnt& b) {
        return result.origin.SquareDistance(a) < result.origin.SquareDistance(b);
    });
    gp_Vec axis(result.origin, *farthest);
    if (axis.SquareMagnitude() <= 1e-24) { result.reason = "degenerate-control-net"; return result; }
    axis.Normalize();
    gp_Vec normal; double maximumCross = 0;
    for (const auto& p : result.points) {
        const auto candidate = axis.Crossed(gp_Vec(result.origin, p));
        if (candidate.SquareMagnitude() > maximumCross) { normal = candidate; maximumCross = candidate.SquareMagnitude(); }
    }
    if (maximumCross <= 1e-20) { result.reason = "collinear-control-net-no-unique-plane"; return result; }
    result.normal = canonicalNormal(gp_Dir(normal));
    const gp_Pln plane(result.origin, result.normal);
    for (const auto& p : result.points) result.maximumDeviation = std::max(result.maximumDeviation, plane.Distance(p));
    result.planar = result.maximumDeviation <= planeTolerance;
    if (!result.planar) result.reason = "control-hull-is-not-planar";
    return result;
}

void write(std::ostream& output, int faceId, const Certificate& value) {
    output << "{\"faceId\":" << faceId << ",\"isPlanar\":" << (value.planar ? "true" : "false")
        << ",\"method\":" << quoted(value.method) << ",\"reason\":" << quoted(value.reason)
        << ",\"plane\":";
    if (value.planar) {
        output << "{\"origin\":"; point(output, value.origin); output << ",\"normal\":["
            << value.normal.X() << ',' << value.normal.Y() << ',' << value.normal.Z() << "]}";
    } else output << "null";
    output << ",\"maximumPlaneDeviationMm\":" << value.maximumDeviation
        << ",\"parameterDomainCovered\":" << (value.parameterDomainCovered ? "true" : "false")
        << ",\"positiveWeights\":" << (value.positiveWeights ? "true" : "false")
        << ",\"controlPointCount\":" << value.controlPointCount << ",\"certificatePoints\":[";
    bool comma = false;
    if (value.planar) for (const auto& p : value.points) { if (comma) output << ','; comma = true; point(output, p); }
    output << "],\"bounds\":";
    if (value.points.empty()) output << "null";
    else {
        std::array<double, 6> bounds{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
        for (const auto& p : value.points) for (int k = 0; k < 3; ++k) { bounds[k] = std::min(bounds[k], p.Coord(k + 1)); bounds[k + 3] = std::max(bounds[k + 3], p.Coord(k + 1)); }
        output << '['; for (size_t k = 0; k < bounds.size(); ++k) { if (k) output << ','; output << bounds[k]; } output << ']';
    }
    output << '}';
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: face_merge_evidence source.step output.json");
        const fs::path input = argv[1], target = argv[2];
        STEPControl_Reader reader;
        if (reader.ReadFile(utf8(input).c_str()) != IFSelect_RetDone) throw std::runtime_error("source-read-failed");
        reader.TransferRoots();
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(reader.OneShape(), TopAbs_FACE, faces);
        if (faces.IsEmpty()) throw std::runtime_error("source-has-no-faces");
        if (!target.parent_path().empty()) fs::create_directories(target.parent_path());
        std::ofstream output(target);
        output << std::setprecision(17) << "{\"version\":1,\"planarityToleranceMm\":" << planeTolerance
            << ",\"scope\":\"WholeSourceFaceSupportCertificate\",\"faceCount\":" << faces.Extent() << ",\"faces\":[";
        int planar = 0;
        for (int i = 1; i <= faces.Extent(); ++i) {
            Certificate result;
            try { result = certificate(TopoDS::Face(faces(i))); }
            catch (const Standard_Failure& error) { result.reason = std::string("surface-certificate-failed: ") + error.GetMessageString(); }
            catch (const std::exception& error) { result.reason = std::string("surface-certificate-failed: ") + error.what(); }
            if (i > 1) output << ',';
            write(output, i, result);
            if (result.planar) ++planar;
        }
        output << "],\"planarFaceCount\":" << planar << '}';
        output.close();
        if (!output) throw std::runtime_error("evidence-write-failed");
        std::cout << "Faces " << faces.Extent() << ", certified planar " << planar << std::endl;
        return 0;
    } catch (const Standard_Failure& error) { std::cerr << error.GetMessageString(); return 1; }
      catch (const std::exception& error) { std::cerr << error.what(); return 1; }
}
