#pragma once

#include "SecurityWindow1Template.h"

namespace iCAX::TubeDesigner
{
    struct SSecurityWindow2Parameters final
    {
        std::string ProductCode = "TD-002";
        double Height = 1800.0;
        double Width = 1200.0;
        double HandleBottom = 700.0;
        double HandleHeight = 360.0;
        double HandleWidth = 300.0;
        std::uint64_t HandleHorizontalCount = 1;
        std::uint64_t TopHorizontalCount = 2;
        std::uint64_t BottomHorizontalCount = 2;
        double HorizontalBranchReserve = -1.0;
        double VerticalBranchReserve = -1.0;
        double AssemblyClearance = 0.1;
        STubeProfile Frame{ "rect", 50.0, 50.0, 2.0 };
        STubeProfile Vertical{ "rect", 25.0, 25.0, 1.5 };
        STubeProfile Horizontal{ "rect", 35.0, 35.0, 1.5 };
    };

    _TUBE_DESIGNER_EXP SGeneratedProduct GenerateSecurityWindow2(
        const SSecurityWindow2Parameters& Parameters_);
}
