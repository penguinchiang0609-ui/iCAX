#include "alma_study.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
static void require(bool ok, const char *operation) {
    if (!ok)
        throw std::runtime_error(std::string(operation) + ": " + PN2S_LastErrorDetail());
}
int main() {
    auto s = PN2_CreateSession();
    try {
        require(!PN2_IsObjectError(s), "Create session");
        PN2_Point outline[] = {{0, 0}, {10, 0}, {10, 9}, {0, 9}};
        PN2_Point hole[] = {{1, 1}, {8.7, 1}, {8.7, 7.5}, {1, 7.5}};
        PN2_Point insert[] = {{0, 0}, {8, 0}, {8, 2}, {0, 2}};
        auto frame = PN2_AddShape(s, outline, 4, {0, 0}), small = PN2_AddShape(s, insert, 4, {0, 0});
        require(!PN2_IsObjectError(frame) && !PN2_IsObjectError(small), "Add shapes");
        require(PN2_ShapeAddHole(s, frame, hole, 4) == 0, "Add material hole");
        auto fixed = PN2_CreateOrientation(0), continuous = PN2_CreateRangeOrientation(40, 40);
        PN2_Part parts[4];
        require(PN2_AddSeveralParts(s, frame, &fixed, 1, 2, parts) == 0, "Create frames");
        require(PN2_AddSeveralParts(s, small, &continuous, 1, 2, parts + 2) == 0,
                "Create continuous-angle inserts");
        auto sheet = PN2_AddSheet(s, 9, 10);
        int stockQuantity = 2;
        auto multi = PN2_MultiNest(s, &sheet, 1, &stockQuantity, parts, 4, 30);
        require(!PN2_IsObjectError(multi), "MultiNest");
        int boards = 0, left = 0;
        require(PN2_GetMultiResultInfos(s, multi, &boards) == 0 && boards == 2, "Board count");
        require(PN2S_GetMultiUnplacedCount(s, multi, &left) == 0 && left == 0, "Unplaced count");
        std::cout << std::fixed << std::setprecision(6) << "2 sheets, 4 parts placed, 0 unplaced\n";
        std::set<PN2_Part> seen;
        int rotated = 0;
        for (int b = 0; b < boards; b++) {
            PN2_Sheet stock;
            auto result = PN2_GetResult(s, multi, b, &stock);
            int n;
            double length;
            require(!PN2_IsObjectError(result) && stock == sheet, "Get board");
            require(PN2_GetResultInfos(s, result, &length, &n) == 0 && n == 2, "Board placements");
            for (int i = 0; i < n; i++) {
                PN2_Part p;
                PN2_Point pos;
                PN2_Orientation o;
                require(PN2_GetKthNestedPart(s, result, i, &p, &pos, &o) == 0, "Read placement");
                require(seen.insert(p).second, "Unique instance");
                if (p == parts[2] || p == parts[3]) {
                    require(o.min_angle >= 0 && o.min_angle <= 80 &&
                                std::abs(o.min_angle - std::round(o.min_angle)) > 1e-4,
                            "Continuous angle");
                    rotated++;
                }
                std::cout << "sheet=" << b + 1 << " x=" << pos.x << " y=" << pos.y
                          << " angle=" << o.min_angle << "\n";
            }
        }
        require(seen.size() == 4 && rotated == 2, "Instance conservation");
        PN2_DeleteSession(s);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        PN2_DeleteSession(s);
        return 1;
    }
}
