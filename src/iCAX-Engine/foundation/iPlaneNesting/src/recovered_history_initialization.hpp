#pragma once
#include "recovered_history.hpp"
namespace study::recovered {
struct HistoryReferenceRectangle {
    double width = 0, height = 0;
    int32_t identifier = 0;
    Polygon polygon;
};
// 64ad70: value10 is the complete height argument; the contour uses +/-height/2.
HistoryReferenceRectangle historyReferenceRectangle(double width, double height, int32_t identifier);
// 550550 ordinary 78 reference. Mean comes from 5502e0 or its context218/220 fallback.
HistoryReferenceRectangle fixedHistoryReference(Point effectiveMean, int32_t identifier);
// 551670. referenceHeight is context78.value10, rather than the sheet height.
// Identifiers start at the current context16e0 after its earlier reference objects.
std::vector<HistoryReferenceRectangle> dynamicHistoryReferences(double referenceHeight,
    double originalSheetHeight, int32_t count, int32_t firstIdentifier);
// 3a0060: empty sample vectors, DBL_MAX/2 estimates, zero attempts, unit weights.
HistoryStatistics initialHistoryStatistics(size_t count);
}
