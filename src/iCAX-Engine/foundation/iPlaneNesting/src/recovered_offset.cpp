#include "recovered_offset.hpp"
#include <bit>
namespace study::recovered {
Random::Random(uint32_t seed) {
    // 0x1803c5f30; caller supplies the already-converted Config[0x178] seed.
    words[1] = seed;
    for (uint32_t i = 1; i < 624; ++i) {
        seed = ((seed >> 30) ^ seed) * 0x6c078965U + i;
        words[i + 1] = seed;
    }
    words[0] = 624;
    words[1249] = 0xffffffffU;
}
uint32_t Random::next() {
    auto mix = [&](size_t previous, size_t following, size_t other) {
        uint32_t y = ((words[previous] ^ words[following]) & 0x7fffffffU) ^ words[previous];
        return (0U - (y & 1U)) & 0x9908b0dfU ^ words[other] ^ (y >> 1);
    };
    uint32_t cursor = words[0];
    if (cursor == 624) {
        for (size_t i = 2; i < 626; ++i) words[i + 623] = mix(i - 1, i, i + 396);
    } else if (cursor >= 1248) {
        for (size_t i = 626; i < 853; ++i) words[i - 625] = mix(i - 1, i, i + 396);
        for (size_t i = 853; i < 1249; ++i) words[i - 625] = mix(i - 1, i, i - 852);
        words[624] = mix(1248, 1, 397);
        cursor = 0;
    }
    uint32_t y = words[cursor + 1];
    words[0] = cursor + 1;
    y ^= (y >> 11) & words[1249];
    y ^= (y & 0xff3a58adU) << 7;
    y ^= (y & 0xffffdf8cU) << 15;
    return (y >> 18) ^ y;
}
double Random::unit() {
    const uint32_t first = next(), second = next();
    return static_cast<double>((static_cast<uint64_t>(second) << 21) + (first >> 11)) * 0x1p-53;
}
int32_t Random::integer(int32_t first,int32_t last) {
    const uint32_t lower=std::bit_cast<uint32_t>(first)^0x80000000U;
    const uint32_t range=(std::bit_cast<uint32_t>(last)^0x80000000U)-lower;
    if(range==UINT32_MAX)return std::bit_cast<int32_t>((next()+lower)^0x80000000U);
    const uint32_t bound=range+1;
    uint64_t product=uint64_t(next())*bound;
    if(uint32_t(product)<bound) {
        const uint32_t threshold=(uint32_t(0)-bound)%bound;
        while(uint32_t(product)<threshold)product=uint64_t(next())*bound;
    }
    return std::bit_cast<int32_t>((uint32_t(product>>32)+lower)^0x80000000U);
}
double requiredValue(const ValuePart &p) {
    return p.optional ? 0 : p.overrideValue < 0 ? p.area : p.overrideValue;
}
Complexity drawComplexity(const std::function<double()> &unit) {
    Complexity c;
    // 36616e..366219: gate, optional q, alpha, gamma, bbox. Do not reorder.
    if (unit() < .5) c.perimeter = unit() * .05;
    c.alpha = unit() * .3;
    c.gamma = unit() * .3;
    c.useBBox = unit() < .2;
    return c;
}
std::vector<double> values(const std::vector<ValuePart> &parts, double totalArea, Complexity c) {
    std::vector<double> result;
    double z = 0;
    for (const auto &p : parts) {
        double base = requiredValue(p), v = 0;
        if (base != 0) {
            const double perimeterContribution = std::pow(p.perimeterField, 2.) * c.perimeter + base;
            const double complexity = c.useBBox ? std::pow(p.bboxArea, c.gamma)
                : std::pow(p.maxHeight, c.gamma) + std::pow(p.maxWidth, c.gamma);
            v = std::pow(p.bboxArea / base, c.alpha) * complexity * perimeterContribution;
            z += static_cast<double>(p.optional ? 0 : p.quantity) * v;
        }
        result.push_back(v);
    }
    if (z != 0) for (double &v : result) v = (totalArea / z) * v;
    return result;
}
ReferenceKind selectReference(double u) {
    return u < .15 ? ReferenceKind::Wide : u < .30 ? ReferenceKind::Triangle : ReferenceKind::Normal;
}
Point effectiveAverage(const std::vector<ValuePart> &parts, double totalArea, Point fallback) {
    double w = 0, h = 0;
    int64_t n = 0;
    for (const auto &p : parts) {
        double pw = 0, ph = 0;
        for (auto s : p.variantSizes) { pw += s.x; ph += s.y; }
        if (!p.variantSizes.empty()) { pw /= p.variantSizes.size(); ph /= p.variantSizes.size(); }
        int q = p.optional ? 0 : p.quantity;
        if (pw > 0 || ph > 0) n += q;
        w += q * pw;
        h += q * ph * p.area;
    }
    if (n > 0) { w /= static_cast<double>(n); h /= totalArea; }
    return w <= 0 || h <= 0 ? fallback : Point{w, h};
}
Polygon referenceShape(Point mean, double height, ReferenceKind kind) {
    const double w = (mean.x * .25) * 1.5, h = (mean.y * .25) * .5;
    if (kind == ReferenceKind::Triangle) return {{0, 0}, {w, -h}, {w, h}};
    const double rh = kind == ReferenceKind::Wide ? std::min(height * .5, 4 * h) : h;
    return {{0, -rh / 2}, {w, -rh / 2}, {w, rh / 2}, {0, rh / 2}};
}
double score(const ScoreInput &v, ScoreState &s) {
    double cost = v.primaryCost;
    if (s.mix > .0001) cost = v.secondaryCost * s.mix + (1 - s.mix) * cost;
    if(v.staircase) cost=(double(v.pitch230)/1000000.)*(double(v.staircaseLoss)/1000.)*.5+cost*.5;
    if (s.frontEnabled && !v.staircase) {
        const int64_t excess = std::max<int64_t>(v.candidateRightX - v.maxPlacedX, 0)
            - s.limitX - v.leftTrimX + v.maxPlacedX;
        cost += std::max(static_cast<double>(excess) / 1e6, 0.) * v.sheetHeight;
    }
    if (s.narrowEnabled) cost += std::max(s.narrowWidth - v.candidateWidth, 0.) * v.sheetHeight;
    const double weightedValue = v.value * v.multiplicity;
    double result = cost / ((v.constraintStrength / 100. + 1.) * weightedValue);
    if (s.goalEnabled) {
        const double newValue = v.placedValue + v.requiredValue;
        const double newArea = std::max(static_cast<double>(v.candidateRightX) / 1e6,
            static_cast<double>(v.maxPlacedX - v.leftTrimX) / 1e6) * v.sheetHeight;
        if (s.bestValue * 1.001 < newValue || (s.bestValue * .9999999 < newValue && newArea < s.bestArea)) {
            s.bestValue = newValue; s.bestArea = newArea; result /= 144.;
        }
    }
    return result;
}
void initialize(const Initialization &v, ScoreState &s) {
    if (!(v.auxReference || v.targetX >= 0) || !v.hasLastRecord || !v.hasLastPart) {
        s.frontEnabled = false; s.narrowEnabled = false; return;
    }
    s.goalEnabled = v.goalValid;
    if (v.goalValid) { s.bestValue = v.bestValue; s.bestArea = v.bestArea; }
    const int64_t target = v.targetX >= 0 ? v.targetX : rounded(v.targetEstimate * 1e6);
    const int64_t front = v.maxPlacedX - rounded(v.leftTrim * 1e6);
    double width = v.lastMaxWidth;
    for (double w : v.groupWidths) width = std::max(width, w);
    const int64_t end = front + rounded(width * 1e6);
    s.frontEnabled = end > target;
    if (end <= target) return; // Old narrow state is intentionally preserved here.
    std::optional<int64_t> minEnd;
    for (const auto &p : v.firstPositions) if (p) {
        const int64_t endpoint = p->strip + (p->quantizedWidth + v.pitchX - 1) / v.pitchX;
        if (!minEnd || endpoint < *minEnd) minEnd = endpoint;
    }
    const int64_t freeEnd = minEnd ? *minEnd * v.pitchX : front;
    s.narrowEnabled = freeEnd > front && freeEnd > target;
    s.narrowWidth = .9 * v.lastMinWidth;
    const int64_t da = rounded(v.lastMinWidth * v.argA * 1e6), db = rounded(v.lastMinWidth * v.argB * 1e6);
    s.limitX = std::max({front + db, freeEnd + db, target + da});
}
int64_t closedCount(const Rows &rows) {
    int64_t total = 0;
    for (const auto &row : rows) for (auto r : row) total += r[1] - r[0] + 1;
    return total;
}
double currentFront(const FrontInput &v) {
    if (std::abs(v.placedArea) < 1e-9) {
        double estimate = 0;
        for (const auto &p : v.available) estimate += p.quantity * .5 * (p.bboxArea + p.area);
        const double den = v.sheetHeight - v.defectMeasure / v.sheetWidth;
        return std::abs(den) >= 1e-9 ? std::min(v.sheetWidth, (estimate + v.defectMeasure) / den) : v.sheetWidth;
    }
    const double lost = static_cast<double>(v.initialCount - v.currentCount) / 1000 * (static_cast<double>(v.pitchX) / 1e6);
    if (std::abs(lost) < 1e-9) return v.sheetWidth;
    const double den = (v.placedArea + v.defectMeasure) * (v.sheetHeight - v.auxMetadataHeight) / lost - v.defectMeasure / v.sheetWidth;
    if (std::abs(den) < 1e-9) return v.sheetWidth;
    double target = (v.targetField14b8 - v.queueAggregate20 + v.placedArea) / den;
    if (v.auxIsSpecial && target > v.auxWidth) target -= v.auxWidth;
    return target;
}
} // namespace study::recovered
