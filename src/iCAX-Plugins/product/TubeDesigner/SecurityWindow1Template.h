#pragma once

#include "TubeDesignerExport.h"

#include <cstdint>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace iCAX::TubeDesigner
{
    struct STubeProfile final
    {
        std::string Type = "rect";
        double Width = 0.0;
        double Depth = 0.0;
        double WallThickness = 0.0;
        double CornerRadius = 0.0;
    };

    struct SSecurityWindow1Parameters final
    {
        std::string ProductCode = "TD-001";
        double Height = 1800.0;
        double Width = 1200.0;
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

    struct SGeneratedPart final
    {
        struct SPoint2D final
        {
            double X = 0.0;
            double Y = 0.0;
        };

        struct SManufacturingEdge final
        {
            std::string Kind = "line";
            SPoint2D Start;
            SPoint2D Middle;
            SPoint2D End;
        };

        struct SManufacturingCutter final
        {
            std::string Kind;
            std::string Label;
            double StartZ = 0.0;
            double EndZ = 0.0;
            std::vector<SPoint2D> Points;
            std::vector<SManufacturingEdge> Edges;
            SPoint2D Center;
            double Radius = 0.0;
        };

        struct SManufacturingGeometry final
        {
            std::string Kind = "tube-with-cutters";
            double BaseLength = 0.0;
            double BendAllowance = 0.0;
            std::uint64_t BendCount = 0;
            std::vector<SManufacturingCutter> Cutters;
        };

        struct SPreviewGeometry final
        {
            std::string Kind = "folded-rect-frame";
            double OuterMinX = 0.0;
            double OuterMinY = 0.0;
            double OuterMaxX = 0.0;
            double OuterMaxY = 0.0;
            double OpeningMinX = 0.0;
            double OpeningMinY = 0.0;
            double OpeningMaxX = 0.0;
            double OpeningMaxY = 0.0;
            double HalfDepth = 0.0;
            double WallThickness = 0.0;
            double RoundedOuterCornerRadius = 0.0;
            double RoundedInnerCornerRadius = 0.0;
        };

        std::uint64_t Index = 0;
        std::string PartNumber;
        std::string Role;
        std::string StartCut = "square";
        std::string EndCut = "square";
        STubeProfile Profile;
        double X1 = 0.0;
        double Y1 = 0.0;
        double X2 = 0.0;
        double Y2 = 0.0;
        double Length = 0.0;
        std::vector<SPoint2D> PathPoints;
        std::optional<SManufacturingGeometry> ManufacturingGeometry;
        std::optional<SPreviewGeometry> PreviewGeometry;
        // Empty means this manufacturing part is also an independent preview member.
        // Parts sharing a non-empty key are rendered and selected as one assembly unit,
        // while disassembly still expands them into their individual manufacturing parts.
        std::string PreviewAssemblyKey;
        std::string PreviewAssemblyRole;
        std::string PreviewAssemblyName;

        bool IsVertical() const noexcept
        {
            return std::abs(Y2 - Y1) > std::abs(X2 - X1);
        }
    };

    struct SGeneratedJoint final
    {
        std::uint64_t TargetPartIndex = 0;
        std::uint64_t InsertedPartIndex = 0;
        std::string Mode = "through";
        double Clearance = 0.0;
        double X = 0.0;
        double Y = 0.0;
        std::string StableKey;
    };

    struct SGeneratedProduct final
    {
        std::string ProductCode;
        std::string TemplateID = "security-window-1";
        std::string TemplateVersion = "1.0.0";
        double Width = 0.0;
        double Height = 0.0;
        std::vector<SGeneratedPart> Parts;
        std::vector<SGeneratedJoint> Joints;
    };

    _TUBE_DESIGNER_EXP SGeneratedProduct GenerateSecurityWindow1(
        const SSecurityWindow1Parameters& Parameters_);
}
