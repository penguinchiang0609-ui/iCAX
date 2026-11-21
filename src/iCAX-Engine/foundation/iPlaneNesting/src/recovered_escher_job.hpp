#pragma once
#include "recovered_escher_contact.hpp"

namespace study::recovered {
// 7f7520 keeps exteriors and holes separate until the job-shape wrapper reverses
// and appends the final hole contours. All arithmetic areas use wrapped int64.
void escherNormalizeIntegerContours(ConvolutionIntegerPolygon &exteriors,
    ConvolutionIntegerPolygon &holes, double hullAreaTolerance = .00001);
ConvolutionIntegerPolygon escherNormalizeIntegerShape(ConvolutionIntegerPolygon exteriors,
    ConvolutionIntegerPolygon holes, int32_t maximumHoles = 20); // 7f8490
}
