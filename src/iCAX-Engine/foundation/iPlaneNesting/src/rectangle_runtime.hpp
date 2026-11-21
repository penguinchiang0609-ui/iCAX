#pragma once
#include "recovered_rectangle.hpp"
namespace study {
struct RectangleTilingModel {
    std::vector<Placement> members;
    Point u{}, v{};
    int32_t maximumX = 0, maximumY = 0, maximumTotal = 0, style = 1;
};
Box rectangleModelBounds(const Input &, const RectangleTilingModel &);
struct RectangleCoreOptions {
    std::vector<double> groupPrices, modulePrices;
    std::vector<int> moduleOrder;
    std::vector<RectangleTilingModel> modules;
    double targetWidth = 0;
};
struct RectangleCoreResult {
    Output output;
    std::array<double, 3> dimensions{};
    std::vector<int> counts;
    bool hasTree = false, complete = false;
};
RectangleCoreResult rectangleCore(const Input &, Budget &, const RectangleCoreOptions &);
struct RectangleProgram {
    recovered::RectangleConfig config;
    std::vector<double> groupPrices;
    std::vector<RectangleTilingModel> modules;
    double stockPrice = 0;
    RectangleProgram() { config.iterationLimit = 0; }
};
struct OrdinaryRectangleTrace {
    recovered::RectangleTrace search;
    size_t recursiveTrials = 0;
    size_t participatingGroups = 0;
    std::vector<std::vector<double>> modulePrices;
};
bool ordinaryRectangle(const Input &, Budget &, Output &, const RectangleProgram & = {},
                       OrdinaryRectangleTrace * = nullptr);
}
