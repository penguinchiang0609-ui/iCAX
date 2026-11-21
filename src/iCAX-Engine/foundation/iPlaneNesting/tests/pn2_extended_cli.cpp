#include "alma_study.h"
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>
// Protocol: strategy gap seconds stock_count group_count; then stocks W H qty;
// groups qty vertices holes poses pivotX pivotY, outer points, holes(n, points),
// followed by poses(min_angle max_angle mirror). Calls only the public DLL ABI.
int main(int argc, char **argv) {
    std::vector<std::string> operations;
    bool protectionOffsets = false;
    for (int argument = 1; argument < argc; ++argument) {
        const std::string operation = argv[argument];
        if (operation == "--protection-offsets") {
            protectionOffsets = true;
            continue;
        }
        if (operation != "--shake" && operation != "--compact-bottom" && operation != "--compact-top")
            return 2;
        operations.push_back(operation);
    }
    int strategy, ns, ng;
    double gap, seconds;
    if (!(std::cin >> strategy >> gap >> seconds >> ns >> ng) || ns < 1 || ns > 64 || ng < 0 || ng > 128)
        return 2;
    auto s = PN2_CreateSession();
    auto fail = [&](int e) {
        std::cout << "{\"error\":" << e << ",\"detail\":" << std::quoted(PN2S_LastErrorDetail()) << "}\n";
        PN2_DeleteSession(s);
        return 0;
    };
    if (PN2_IsObjectError(s))
        return fail(PN2_ErrorCodeFromObject(s));
    int error = PN2S_SetStrategy(s, strategy);
    if (!error)
        error = PN2S_SetRectangleGap(s, gap);
    if (error)
        return fail(error);
    std::vector<PN2_Sheet> stocks;
    std::vector<int> quantities;
    std::map<PN2_Sheet, int> stockIds;
    for (int i = 0; i < ns; i++) {
        double w, h;
        int qty;
        if (!(std::cin >> w >> h >> qty))
            return fail(PN2S_INVALID);
        auto sh = PN2_AddSheet(s, h, w);
        if (PN2_IsObjectError(sh))
            return fail(PN2_ErrorCodeFromObject(sh));
        stocks.push_back(sh);
        quantities.push_back(qty);
        stockIds[sh] = i;
    }
    std::vector<PN2_Part> all;
    std::map<PN2_Part, std::pair<int, int>> ids;
    for (int i = 0; i < ng; i++) {
        int qty, nv, nh, np;
        PN2_Point pivot;
        if (!(std::cin >> qty >> nv >> nh >> np >> pivot.x >> pivot.y) || qty < 1 || qty > 10000 ||
            nv < 3 || nv > 8192 || nh < 0 || nh > 512 || np < 1 || np > 32)
            return fail(PN2S_INVALID);
        std::vector<PN2_Point> points(nv);
        for (auto &p : points)
            if (!(std::cin >> p.x >> p.y))
                return fail(PN2S_INVALID);
        auto shape = PN2_AddShape(s, points.data(), nv, pivot);
        if (PN2_IsObjectError(shape))
            return fail(PN2_ErrorCodeFromObject(shape));
        if (protectionOffsets) {
            double protection;
            if (!(std::cin >> protection))
                return fail(PN2S_INVALID);
            error = PN2_ShapeSetProtectionOffset(s, shape, protection);
            if (error)
                return fail(error);
        }
        for (int hole = 0; hole < nh; hole++) {
            int n;
            if (!(std::cin >> n) || n < 3 || n > 8192)
                return fail(PN2S_INVALID);
            std::vector<PN2_Point> p(n);
            for (auto &q : p)
                if (!(std::cin >> q.x >> q.y))
                    return fail(PN2S_INVALID);
            error = PN2_ShapeAddHole(s, shape, p.data(), n);
            if (error)
                return fail(error);
        }
        std::vector<PN2_Orientation> poses(np);
        for (auto &p : poses)
            if (!(std::cin >> p.min_angle >> p.max_angle >> p.horizontal_flip))
                return fail(PN2S_INVALID);
        std::vector<PN2_Part> parts(qty);
        error = PN2_AddSeveralParts(s, shape, poses.data(), np, qty, parts.data());
        if (error)
            return fail(error);
        for (int j = 0; j < qty; j++) {
            all.push_back(parts[j]);
            ids[parts[j]] = {i, j};
        }
    }
    auto multi = PN2_MultiNest(s, stocks.data(), ns, quantities.data(), all.data(),
                               static_cast<int>(all.size()), seconds);
    if (PN2_IsObjectError(multi))
        return fail(PN2_ErrorCodeFromObject(multi));
    int count = 0;
    if (PN2_GetMultiResultInfos(s, multi, &count))
        return fail(PN2S_INTERNAL);
    std::vector<PN2_Result> optimized;
    std::vector<PN2_Sheet> resultSheets;
    for (int b = 0; b < count; ++b) {
        PN2_Sheet sheet;
        auto result = PN2_GetResult(s, multi, b, &sheet);
        if (PN2_IsObjectError(result))
            return fail(PN2_ErrorCodeFromObject(result));
        for (const auto &operation : operations) {
            result = operation == "--shake" ? PN2_Shake(s, result, seconds)
                : PN2_Compact(s, result, operation == "--compact-bottom" ? 1 : 2, seconds);
            if (PN2_IsObjectError(result))
                return fail(PN2_ErrorCodeFromObject(result));
        }
        optimized.push_back(result);
        resultSheets.push_back(sheet);
    }
    std::cout << std::setprecision(17) << "{\"error\":0,\"boards\":[";
    for (int b = 0; b < count; b++) {
        const auto sh = resultSheets[b];
        const auto result = optimized[b];
        int n;
        double length;
        if (PN2_GetResultInfos(s, result, &length, &n))
            return 3;
        if (b)
            std::cout << ',';
        std::cout << "{\"stock\":" << stockIds.at(sh) << ",\"length\":" << length << ",\"placements\":[";
        for (int i = 0; i < n; i++) {
            PN2_Part p;
            PN2_Point pos;
            PN2_Orientation angle;
            if (PN2_GetKthNestedPart(s, result, i, &p, &pos, &angle))
                return 3;
            auto [g, o] = ids.at(p);
            if (i)
                std::cout << ',';
            std::cout << "{\"group\":" << g << ",\"ordinal\":" << o << ",\"x\":" << pos.x
                      << ",\"y\":" << pos.y << ",\"angle\":" << angle.min_angle
                      << ",\"flip\":" << angle.horizontal_flip << '}';
        }
        std::cout << "]}";
    }
    int n = 0;
    if (PN2S_GetMultiUnplacedCount(s, multi, &n))
        return 3;
    std::cout << "],\"unplaced\":[";
    for (int i = 0; i < n; i++) {
        PN2_Part p;
        if (PN2S_GetMultiUnplacedPart(s, multi, i, &p))
            return 3;
        auto [g, o] = ids.at(p);
        if (i)
            std::cout << ',';
        std::cout << "{\"group\":" << g << ",\"ordinal\":" << o << '}';
    }
    std::cout << "]}\n";
    PN2_DeleteSession(s);
    return 0;
}
