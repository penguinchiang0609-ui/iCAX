#pragma once
#include "core.hpp"
#include <functional>
#include <optional>

// Address-specific ports, not a claim that the surrounding product is recovered.
namespace study::recovered {
struct Random {
    // Original layout: cursor, two 624-word banks, temper mask, auxiliary word.
    std::array<uint32_t, 1251> words{};
    explicit Random(uint32_t seed);
    uint32_t next(); // 0x180367a90
    double unit();   // 0x180367a20; consumes exactly two words
    int32_t integer(int32_t first,int32_t last); // 0x18039e660, unsigned wrapped inclusive range
};
struct ValuePart {
    double area = 0, bboxArea = 0, maxWidth = 0, maxHeight = 0, perimeterField = 0;
    double overrideValue = -1;
    int quantity = 0;
    bool optional = false;
    std::vector<Point> variantSizes;
};
double requiredValue(const ValuePart &);
struct Complexity {
    double alpha = 0, gamma = 0, perimeter = 0;
    bool useBBox = false;
};
Complexity drawComplexity(const std::function<double()> &unit);
std::vector<double> values(const std::vector<ValuePart> &, double totalArea, Complexity);
enum class ReferenceKind { Normal, Wide, Triangle };
ReferenceKind selectReference(double unit);
Point effectiveAverage(const std::vector<ValuePart> &, double totalArea, Point fallback);
Polygon referenceShape(Point average, double sheetHeight, ReferenceKind);

struct ScoreState {
    bool frontEnabled = false, goalEnabled = false, narrowEnabled = false;
    int64_t limitX = 0;
    double narrowWidth = 0, bestValue = 0, bestArea = 0, mix = 0;
};
struct ScoreInput {
    double primaryCost = 0, secondaryCost = 0, value = 1;
    int multiplicity = 1, constraintStrength = 0;
    double sheetHeight = 0, candidateWidth = 0, requiredValue = 0, placedValue = 0;
    int64_t candidateRightX = 0, maxPlacedX = 0, leftTrimX = 0;
    bool staircase = false;
    int64_t pitch230 = 1, staircaseLoss = 0;
};
double score(const ScoreInput &, ScoreState &); // ordinary path in 0x180402670
struct FirstPosition { int64_t strip, quantizedWidth; };
struct Initialization {
    bool auxReference = false, hasLastRecord = false, hasLastPart = false, goalValid = false;
    int64_t targetX = -1, maxPlacedX = 0, pitchX = 1;
    double bestValue = 0, bestArea = 0, leftTrim = 0, lastMaxWidth = 0, lastMinWidth = 0;
    double targetEstimate = 0, argA = .2, argB = .1;
    std::vector<double> groupWidths;
    std::vector<std::optional<FirstPosition>> firstPositions;
};
void initialize(const Initialization &, ScoreState &); // 0x180402e50
struct FrontInput {
    double placedArea = 0, sheetWidth = 0, sheetHeight = 0, defectMeasure = 0;
    double auxMetadataHeight = 0, targetField14b8 = 0, queueAggregate20 = 0, auxWidth = 0;
    int64_t initialCount = 0, currentCount = 0, pitchX = 1;
    bool auxIsSpecial = false;
    std::vector<ValuePart> available;
};
double currentFront(const FrontInput &); // 0x1803d7340
int64_t closedCount(const Rows &);
} // namespace study::recovered
