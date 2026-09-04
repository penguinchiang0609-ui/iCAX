#pragma once

#include "SecurityWindow1Template.h"

namespace iCAX::TubeDesigner
{
    // A connection process belongs to a tube-path corner, not to a particular
    // closed-frame template. The same model can be reused by L/U/rectangular paths.
    struct STubeCornerProcessParameters final
    {
        std::string JoinType = "v_groove_90";
        std::string ButtWrapMode = "side_wraps_horizontal";
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
    };

    struct SSingleFaceSecurityWindowParameters final
    {
        std::string FrameLayout = "left_right";
        bool AccessDoorEnabled = false;
        std::string ProductCode = "TD-SF-001";
        double Height = 1800.0;
        double Width = 1200.0;

        std::uint64_t HorizontalCount = 4;
        std::uint64_t MiddleVerticalCount = 1;
        double FirstHorizontalTopOffset = 200.0;
        double LastHorizontalBottomOffset = 200.0;

        double DoorLeft = 450.0;
        double DoorBottom = 700.0;
        double DoorHeight = 360.0;
        double DoorWidth = 300.0;
        double DoorGap = 6.0;
        std::string DoorHingeSide = "left";
        std::uint64_t DoorHingeCount = 2;
        std::uint64_t DoorHorizontalCount = 1;
        std::uint64_t DoorVerticalCount = 1;

        STubeCornerProcessParameters OuterFrameProcess;
        STubeCornerProcessParameters DoorFrameProcess;
        STubeCornerProcessParameters DoorLeafFrameProcess;

        double HorizontalBranchReserve = -1.0;
        double VerticalBranchReserve = -1.0;
        double AssemblyClearance = 0.1;
        STubeProfile Frame{ "rect", 50.0, 50.0, 2.0, 5.0 };
        STubeProfile Vertical{ "round", 25.0, 25.0, 1.5 };
        STubeProfile Horizontal{ "rect", 35.0, 35.0, 1.5, 3.5 };
        STubeProfile DoorFrame{ "rect", 35.0, 35.0, 1.5, 3.5 };
        STubeProfile DoorLeafFrame{ "rect", 25.0, 25.0, 1.5, 2.5 };
        STubeProfile DoorVertical{ "round", 16.0, 16.0, 1.2 };
        STubeProfile DoorHorizontal{ "rect", 20.0, 20.0, 1.2, 2.0 };
    };

    _TUBE_DESIGNER_EXP SGeneratedProduct GenerateSingleFaceSecurityWindow(
        const SSingleFaceSecurityWindowParameters& Parameters_);
}
