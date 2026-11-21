#pragma once
#include "recovered_escher_boundary_map.hpp"
namespace study::recovered {
// Parameter order follows 7a9c00: step first, horizontal period second.
EscherBoundaryTiledPolygon escherBoundaryPeriodicMap(EscherIntegerPoint step,EscherIntegerPoint horizontal,
    const ConvolutionIntegerPolygon &); //7a9c00
}
