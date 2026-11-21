#pragma once
#include "polygon_geometry.hpp"

namespace icax::iplane::batch {
struct Stock {
    geometry::Board geometry;
    int quantity = 0;
};
struct BoardLayout {
    size_t stock = 0, stockOrdinal = 0;
    study::Output output;
    std::string backend = "material-dll";
};
struct Score {
    size_t placed = 0, boards = 0;
    double materialArea = 0, netStockArea = 0, envelopeArea = 0;
};
struct Statistics {
    size_t constructionTrials = 0, stockComparisons = 0;
    size_t replacementTrials = 0, consolidationTrials = 0, repackTrials = 0;
    size_t acceptedImprovements = 0;
    std::string bestSource = "incumbent";
};
struct Result {
    std::vector<BoardLayout> boards;
    std::vector<std::pair<size_t, size_t>> unplaced;
    Score score;
    Statistics statistics;
    bool stopped = false;
    std::vector<std::string> warnings;
};

// fullDemand carries original group/ordinal identities and finite or bounded
// Pose intervals. Board coordinates and returned placement translations retain
// their original origins. An incumbent may be empty; every retained board is
// strictly validated before publication. parent is the original final batch
// deadline, including time already spent by the caller; it is never reset.
Result optimize(const study::Input &fullDemand, const std::vector<Stock> &inventory,
                const std::vector<BoardLayout> &verifiedIncumbent, study::Budget &parent);
} // namespace icax::iplane::batch
