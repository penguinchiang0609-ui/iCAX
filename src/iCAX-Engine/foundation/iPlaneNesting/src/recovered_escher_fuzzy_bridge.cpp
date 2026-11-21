#include "recovered_escher_fuzzy_bridge.hpp"
#include "recovered_fuzzy_polygon_construct.hpp"
namespace study::recovered {
EscherFuzzySelectionResult escherFuzzySelect(const ConvolutionIntegerPolygon &polygons,
    std::span<const double> weights,EscherIntegerPoint primary,EscherIntegerPoint secondary){
    const auto selected=fuzzyPolygonBestPoint(fuzzyPreparePolygon(polygons,weights),primary,secondary);
    return {selected.selection,selected.success};
}
void escherBindBoundaryFuzzy(EscherBoundaryLengthServices &services){services.fuzzy=&escherFuzzySelect;}
void escherBindBoundaryGeometry(EscherBoundaryLengthServices &services,EscherPlacementMapCache &cache){
    escherBindBoundaryPlacementCache(services,cache);escherBindBoundaryFuzzy(services);
}
}
