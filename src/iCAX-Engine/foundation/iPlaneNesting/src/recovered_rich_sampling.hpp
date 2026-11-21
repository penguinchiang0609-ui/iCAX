#pragma once
#include "recovered_rich_traversal.hpp"

namespace study::recovered {
// 8ac9c0. Angles are in radians. Returned points lie at the midpoints of
// circumscribed chords. The enclosing visitor appends the original endpoint.
std::vector<ShakerPoint> richCircumscribedArcPoints(const RichCircle &, double beginAngle,
                                                  double endAngle, double error);
// 8acc50. Parameters use the circle's native length units. The enclosing
// visitor appends the original endpoint after these interior samples.
std::vector<ShakerPoint> richInscribedArcPoints(const RichCircle &,
                                              std::array<double, 2> parameters, double error);
// b74ea0 with [0,pi/2], preferUpper=false, tolerance=1e-6.
double richSamplingQuadrantRemainder(double angle);
// 8ac680 including its optional quadrant-alignment branch.
std::vector<ShakerPoint> richCircumscribedArc(const RichCircle &,
    std::array<double, 2> parameters, double error, bool alignQuadrants = false);
// 8aceb0. The SDK's default quadrant-alignment flag is disabled.
std::vector<ShakerPoint> richDiscretizeArc(const RichCircle &,
    std::array<double, 2> parameters, double error, bool alignQuadrants = false);
}
