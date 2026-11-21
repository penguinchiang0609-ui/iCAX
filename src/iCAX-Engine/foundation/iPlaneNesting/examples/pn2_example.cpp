#include "alma_study.h"
#include <iostream>
int main() {
    auto s = PN2_CreateSession();
    if (PN2_IsObjectError(s))
        return 1;
    PN2_Point poly[] = {{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}};
    auto shape = PN2_AddShape(s, poly, 6, {0, 0});
    PN2_Orientation poses[] = {PN2_CreateOrientation(0), PN2_CreateOrientation(90)};
    PN2_Part parts[4];
    int e = PN2_AddSeveralParts(s, shape, poses, 2, 4, parts);
    auto sheet = PN2_AddSheet(s, 6, 8);
    PN2S_SetPitch(s, .25);
    auto result = e ? static_cast<PN2_Result>(nullptr) : PN2_Nest(s, sheet, parts, 4, 10);
    if (PN2_IsObjectError(result)) {
        std::cerr << PN2_ErrorMessageFromObject(result) << ": " << PN2S_LastErrorDetail() << '\n';
        PN2_DeleteSession(s);
        return 2;
    }
    double length;
    int n;
    PN2_GetResultInfos(s, result, &length, &n);
    std::cout << PN2S_Version() << "\nPlaced " << n << " of 4; occupied length=" << length << '\n';
    for (int i = 0; i < n; i++) {
        PN2_Part p;
        PN2_Point pos;
        PN2_Orientation o;
        if (PN2_GetKthNestedPart(s, result, i, &p, &pos, &o))
            return 3;
        std::cout << i << ": x=" << pos.x << ", y=" << pos.y << ", angle=" << o.min_angle
                  << ", mirror=" << o.horizontal_flip << '\n';
    }
    PN2_DeleteSession(s);
    return n == 4 ? 0 : 4;
}
