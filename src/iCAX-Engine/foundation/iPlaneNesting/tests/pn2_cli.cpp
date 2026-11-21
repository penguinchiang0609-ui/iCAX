#include "alma_study.h"
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>
#include <charconv>
#include <string_view>
int main(int argc, char **argv) {
    int strategy, groups;
    double width, height, pitch, gap, seconds;
    if (!(std::cin >> strategy >> width >> height >> pitch >> gap >> seconds >> groups) || groups < 0 ||
        groups > 128)
        return 2;
    auto session = PN2_CreateSession();
    auto fail = [&](int code) {
        std::cerr << PN2S_LastErrorDetail() << '\n';
        std::cout << "{\"error\":" << code << "}\n";
        PN2_DeleteSession(session);
        return 0;
    };
    // -1 exercises the new-session default without changing its strategy.
    int error = strategy == -1 ? 0 : PN2S_SetStrategy(session, strategy);
    if (argc > 1) {
        unsigned int seed = 0;
        std::string_view text(argv[1]);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), seed);
        if (argc != 2 || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            return fail(PN2S_INVALID);
        if (!error) error = PN2S_SetRandomSeed(session, seed);
    }
    if (!error)
        error = PN2S_SetPitch(session, pitch);
    if (!error)
        error = PN2S_SetRectangleGap(session, gap);
    if (error)
        return fail(error);
    std::vector<PN2_Part> all;
    std::map<PN2_Part, std::pair<int, int>> ids;
    for (int g = 0; g < groups; g++) {
        int qty, nv, np;
        PN2_Point pivot;
        if (!(std::cin >> qty >> nv >> np >> pivot.x >> pivot.y) || qty < 1 || qty > 1000 || nv < 3 ||
            nv > 256 || np < 1 || np > 32)
            return fail(PN2S_INVALID);
        std::vector<PN2_Point> polygon(nv);
        for (auto &p : polygon)
            if (!(std::cin >> p.x >> p.y))
                return fail(PN2S_INVALID);
        std::vector<PN2_Orientation> poses(np);
        for (auto &p : poses) {
            if (!(std::cin >> p.min_angle >> p.horizontal_flip))
                return fail(PN2S_INVALID);
            p.max_angle = p.min_angle;
        }
        auto shape = PN2_AddShape(session, polygon.data(), nv, pivot);
        if (PN2_IsObjectError(shape))
            return fail(PN2_ErrorCodeFromObject(shape));
        std::vector<PN2_Part> parts(qty);
        error = PN2_AddSeveralParts(session, shape, poses.data(), np, qty, parts.data());
        if (error)
            return fail(error);
        for (int i = 0; i < qty; i++) {
            ids[parts[i]] = {g, i};
            all.push_back(parts[i]);
        }
    }
    auto sheet = PN2_AddSheet(session, height, width);
    if (PN2_IsObjectError(sheet))
        return fail(PN2_ErrorCodeFromObject(sheet));
    auto result = PN2_Nest(session, sheet, all.data(), static_cast<int>(all.size()), seconds);
    if (PN2_IsObjectError(result))
        return fail(PN2_ErrorCodeFromObject(result));
    double length = 0;
    int count = 0;
    if (PN2_GetResultInfos(session, result, &length, &count))
        return fail(PN2S_INTERNAL);
    std::cout << std::setprecision(17) << "{\"error\":0,\"length\":" << length << ",\"placements\":[";
    for (int i = 0; i < count; i++) {
        PN2_Part p;
        PN2_Point pos;
        PN2_Orientation pose;
        if (PN2_GetKthNestedPart(session, result, i, &p, &pos, &pose))
            return 3;
        auto [g, n] = ids.at(p);
        if (i)
            std::cout << ',';
        std::cout << "{\"group\":" << g << ",\"ordinal\":" << n << ",\"x\":" << pos.x
                  << ",\"y\":" << pos.y << ",\"angle\":" << pose.min_angle
                  << ",\"flip\":" << pose.horizontal_flip << '}';
    }
    std::cout << "]}\n";
    PN2_DeleteSession(session);
    return 0;
}
