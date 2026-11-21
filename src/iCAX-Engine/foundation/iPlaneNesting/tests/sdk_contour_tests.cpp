#include "alma_study.h"
#include "recovered_sdk_contour.hpp"
#include "recovered_sdk_sampling.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
size_t checks = 0;
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
double area(const std::vector<study::recovered::ShakerPoint> &points) {
    double result = 0.;
    for (size_t index = 0; index < points.size(); ++index) {
        const auto a = points[index], b = points[(index + 1) % points.size()];
        result += a[0] * b[1] - a[1] * b[0];
    }
    return result * .5;
}
void sampleCircle(double radius, double x, double y) {
    using namespace study::recovered;
    const std::array<ShakerPoint, 2> points{{{x - radius, y}, {x + radius, y}}};
    const std::array<double, 2> bulges{1., 1.};
    SdkContourInput input;
    input.points = points; input.values = bulges; input.kind = SdkContourKind::Bulges;
    RichGraph graph;
    const auto &curve = sdkBuildContour(graph, input);
    const auto outer = sdkSampleContour(curve, true), inner = sdkSampleContour(curve, false);
    const double exactArea = 3.14159265358979323846 * radius * radius;
    check(area(outer) >= exactArea * (1. - 1e-12), "Outer arc sampling encloses material");
    check(area(inner) <= exactArea * (1. + 1e-12), "Hole sampling retains clearance");
    for (const auto &sample : {outer, inner}) {
        check(sample.size() >= 100 && sample.size() < 1000, "SDK relative sampling precision");
        for (const auto point : sample) {
            const double r = std::hypot(point[0] - x, point[1] - y);
            check(r >= radius * (1. - 1e-10) && r <= radius * 1.00021,
                  "Circle sample radial error bound");
        }
    }
}
void apiContours() {
    const auto session = PN2_CreateSession(), other = PN2_CreateSession();
    check(!PN2_IsObjectError(session) && !PN2_IsObjectError(other), "Contour test sessions");
    const PN2_Point vertices[]{{-2, 0}, {2, 0}};
    const PN2_Point centers[]{{0, 0}, {0, 0}};
    const double bulges[]{1, 1}, sagittas[]{2, 2};
    const PN2_Bool directions[]{1, 1};
    const PN2_Contour contours[]{
        PN2_CreateContourFromArcsBulges(session, vertices, bulges, 2),
        PN2_CreateContourFromArcsSagittas(session, vertices, sagittas, 2),
        PN2_CreateContourFromArcsCenters(session, vertices, centers, directions, 2)};
    const auto orientation = PN2_CreateOrientation(0);
    const auto sheet = PN2_AddSheet(session, 5., 5.);
    for (const auto contour : contours) {
        check(!PN2_IsObjectError(contour), "Public circular contour construction");
        const auto shape = PN2_AddShapeFromContour(session, contour, {0, 0});
        check(!PN2_IsObjectError(shape), "Public curved shape construction");
        const auto part = PN2_AddPart(session, shape, &orientation, 1);
        const auto result = PN2_Nest(session, sheet, &part, 1, 10.);
        check(!PN2_IsObjectError(result), "Public circle nesting");
        int count = 0;
        double length = 0;
        check(PN2_GetResultInfos(session, result, &length, &count) == PN2S_OK && count == 1,
              "Circle part is placed");
        check(length >= 4. - 1e-6 && length <= 4.002, "Circle layout keeps true diameter");
        PN2_Part placed; PN2_Point position; PN2_Orientation pose;
        check(PN2_GetKthNestedPart(session, result, 0, &placed, &position, &pose) == PN2S_OK && placed == part,
              "Circle placement instance identity");
        check(position.x >= 2. - 1e-6 && position.y >= 2. - 1e-6 &&
              position.x + 2. <= 5. + 1e-6 && position.y + 2. <= 5. + 1e-6,
              "Analytic circle stays inside the sheet");
        check(PN2_ErrorCodeFromObject(PN2_AddShapeFromContour(other, contour, {0, 0})) == PN2S_INVALID,
              "Contour session ownership");
    }
    const PN2_Point rectangle[]{{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}};
    PN2_Point copiedPoints[]{{0, 0}, {3, 0}, {3, 4}, {0, 4}};
    const auto copied = PN2_CreateContourFromPoints(session, copiedPoints, 4);
    check(!PN2_IsObjectError(copied), "Mutable input contour is created");
    for (auto &point : copiedPoints) point = {NAN, NAN};
    check(!PN2_IsObjectError(PN2_AddShapeFromContour(session, copied, {0, 0})),
          "Contour owns its copied input after caller mutation");
    const auto outline = PN2_CreateContourFromPoints(session, rectangle, 5);
    check(!PN2_IsObjectError(outline), "Repeated closure point is normalized");
    const auto frame = PN2_AddShapeFromContour(session, outline, {0, 0});
    const PN2_Point holePoints[]{{3, 5}, {7, 5}};
    const auto hole = PN2_CreateContourFromArcsBulges(session, holePoints, bulges, 2);
    check(!PN2_IsObjectError(frame) && !PN2_IsObjectError(hole), "Frame and circular hole");
    check(PN2_ShapeAddHoleFromContour(session, frame, hole) == PN2S_OK, "Curved hole attaches to material");
    check(PN2_ShapeAddHoleFromContour(session, frame, hole) == PN2S_INVALID, "Overlapping curved holes rejected");
    const PN2_Point diskPoints[]{{-.75, 0}, {.75, 0}};
    const auto diskContour = PN2_CreateContourFromArcsBulges(session, diskPoints, bulges, 2);
    const auto diskShape = PN2_AddShapeFromContour(session, diskContour, {0, 0});
    check(!PN2_IsObjectError(diskShape), "Inner circular part construction");
    const PN2_Part circularHoleOrder[]{PN2_AddPart(session, frame, &orientation, 1),
                                     PN2_AddPart(session, diskShape, &orientation, 1)};
    const auto frameSheet = PN2_AddSheet(session, 10., 10.);
    const auto holeResult = PN2_Nest(session, frameSheet, circularHoleOrder, 2, 10.);
    check(!PN2_IsObjectError(holeResult), "Circle-in-circular-hole public nesting");
    int holeCount = 0; double holeLength = 0.;
    check(PN2_GetResultInfos(session, holeResult, &holeLength, &holeCount) == PN2S_OK && holeCount == 2,
          "Both frame and inner circle placed on the same sheet");
    PN2_Point framePosition{}, diskPosition{};
    for (int index = 0; index < holeCount; ++index) {
        PN2_Part part; PN2_Point position; PN2_Orientation pose;
        check(PN2_GetKthNestedPart(session, holeResult, index, &part, &position, &pose) == PN2S_OK,
              "Circular hole placement query");
        if (part == circularHoleOrder[0]) framePosition = position;
        else { check(part == circularHoleOrder[1], "Inner circle instance"); diskPosition = position; }
    }
    check(std::hypot(diskPosition.x - framePosition.x - 5., diskPosition.y - framePosition.y - 5.) + .75 <= 2. + 1e-6,
          "Analytic circle-in-hole material separation");
    check(PN2_ErrorCodeFromObject(PN2_AddShapeFromContour(session, reinterpret_cast<PN2_Contour>(frame), {0, 0})) == PN2S_INVALID,
          "Contour wrong-type handle rejected");
    check(PN2_ErrorCodeFromObject(PN2_CreateContourFromPoints(session, nullptr, 3)) == PN2S_INVALID,
          "Null contour coordinates rejected");
    const double invalidValues[]{NAN, 1.};
    check(PN2_ErrorCodeFromObject(PN2_CreateContourFromArcsBulges(session, vertices, invalidValues, 2)) == PN2S_INVALID,
          "Nonfinite arc rejected");
    check(PN2_DeleteSession(session) == 1, "Contour session destruction");
    check(PN2_ErrorCodeFromObject(PN2_AddShapeFromContour(session, outline, {0, 0})) == PN2S_INVALID,
          "Contour handles expire with their session");
    check(PN2_DeleteSession(other) == 1, "Other contour test session destruction");
}
}
int main() {
    try {
        sampleCircle(1., 0., 0.);
        sampleCircle(17.3, -31., 48.);
        sampleCircle(.05, 30., -20.);
        apiContours();
        std::cout << "SDK contours, arc material bounds, public nesting and lifetime: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
