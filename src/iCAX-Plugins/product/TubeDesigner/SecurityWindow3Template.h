#pragma once

#include "SecurityWindow1Template.h"

namespace iCAX::TubeDesigner
{
    struct SSecurityWindow3Parameters final
    {
        std::string ProductCode = "TD-003";
        double Height = 1800.0;
        double Width = 1200.0;
        std::string FrameJoinType = "v_groove_90";
        std::string FrameButtWrapMode = "side_wraps_horizontal";
        std::string VGrooveStyle = "sharp_v";
        double VGrooveBottomDistance = 2.0;
        double VGrooveRadius = 18.0;
        double VGrooveKFactor = 0.62;
        bool VGrooveMaleFemale = false;
        bool VGrooveBottomCut = false;
        bool VGrooveReliefHole = false;
        bool VGrooveWallOvercut = false;
        double VGrooveReliefDiameter = 10.0;
        bool VGrooveReliefNoThrough = false;
        std::uint64_t HorizontalCount = 4;
        std::uint64_t MiddleVerticalCount = 1;
        double FirstHorizontalTopOffset = 200.0;
        double LastHorizontalBottomOffset = 200.0;
        double HorizontalBranchReserve = -1.0;
        double VerticalBranchReserve = -1.0;
        double AssemblyClearance = 0.1;
        STubeProfile Frame{ "rect", 50.0, 50.0, 2.0 };
        STubeProfile Vertical{ "rect", 25.0, 25.0, 1.5 };
        STubeProfile Horizontal{ "rect", 35.0, 35.0, 1.5 };
    };

    _TUBE_DESIGNER_EXP SGeneratedProduct GenerateSecurityWindow3(
        const SSecurityWindow3Parameters& Parameters_);
}
