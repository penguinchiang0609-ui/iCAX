#include "alma_study.h"
#include <stdio.h>
int main(void) {
    PN2_Session s = PN2_CreateSession();
    PN2_Point p[] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}}, pivot = {0, 0};
    PN2_Shape shape = PN2_AddShape(s, p, 4, pivot);
    PN2_Orientation pose = PN2_CreateOrientation(0);
    PN2_Part parts[3];
    PN2_Sheet sheet;
    PN2_Result result;
    double length = 0;
    int n = 0;
    if (PN2_IsObjectError(s) || PN2_IsObjectError(shape) ||
        PN2_AddSeveralParts(s, shape, &pose, 1, 3, parts))
        return 1;
    if (PN2S_SetStrategy(s, PN2S_RECTANGLE_RECURSIVE))
        return 2;
    sheet = PN2_AddSheet(s, 4, 12);
    result = PN2_Nest(s, sheet, parts, 3, 10);
    if (PN2_IsObjectError(result) || PN2_GetResultInfos(s, result, &length, &n))
        return 3;
    printf("C client: %d rectangles, length %.3f\n", n, length);
    PN2_DeleteSession(s);
    return n == 3 ? 0 : 4;
}
