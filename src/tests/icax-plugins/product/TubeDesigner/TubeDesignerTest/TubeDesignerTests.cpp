#include "pch.h"

#include <TubeDesigner/SecurityWindow1Template.h>

#include <limits>

namespace
{
    using namespace iCAX::TubeDesigner;

    const SGeneratedPart& PartAt(const SGeneratedProduct& Product_, std::size_t Index_)
    {
        EXPECT_LT(Index_, Product_.Parts.size());
        return Product_.Parts.at(Index_);
    }
}

TEST(TubeDesignerSecurityWindow1Test, DefaultParametersGenerateStableSevenPartOrder)
{
    const auto _Product = GenerateSecurityWindow1(SSecurityWindow1Parameters{});

    EXPECT_EQ("TD-001", _Product.ProductCode);
    EXPECT_EQ("security-window-1", _Product.TemplateID);
    EXPECT_EQ("1.0.0", _Product.TemplateVersion);
    ASSERT_EQ(7u, _Product.Parts.size());

    EXPECT_EQ("TD-001-001", PartAt(_Product, 0).PartNumber);
    EXPECT_EQ("left-frame", PartAt(_Product, 0).Role);
    EXPECT_NEAR(1800.0, PartAt(_Product, 0).Length, 1.0e-9);

    EXPECT_EQ("TD-001-002", PartAt(_Product, 1).PartNumber);
    EXPECT_EQ("right-frame", PartAt(_Product, 1).Role);
    EXPECT_NEAR(1800.0, PartAt(_Product, 1).Length, 1.0e-9);

    EXPECT_EQ("TD-001-003", PartAt(_Product, 2).PartNumber);
    EXPECT_EQ("middle-vertical", PartAt(_Product, 2).Role);
    EXPECT_NEAR(1750.0, PartAt(_Product, 2).Length, 1.0e-9);

    for (std::size_t _Index = 3; _Index < _Product.Parts.size(); ++_Index)
    {
        EXPECT_EQ("horizontal", PartAt(_Product, _Index).Role);
        EXPECT_NEAR(1150.0, PartAt(_Product, _Index).Length, 1.0e-9);
    }
    EXPECT_EQ("TD-001-007", PartAt(_Product, 6).PartNumber);
}

TEST(TubeDesignerSecurityWindow1Test, DefaultConnectionsKeepExplicitTargetAndInsertedRoles)
{
    const auto _Product = GenerateSecurityWindow1(SSecurityWindow1Parameters{});

    ASSERT_EQ(12u, _Product.Joints.size());
    std::size_t _FrameCuts = 0;
    std::size_t _HorizontalCuts = 0;
    for (const auto& _Joint : _Product.Joints)
    {
        EXPECT_EQ("through", _Joint.Mode);
        EXPECT_NEAR(0.1, _Joint.Clearance, 1.0e-9);
        if (_Joint.TargetPartIndex == 1 || _Joint.TargetPartIndex == 2)
        {
            EXPECT_GE(_Joint.InsertedPartIndex, 4u);
            EXPECT_LE(_Joint.InsertedPartIndex, 7u);
            ++_FrameCuts;
        }
        else
        {
            EXPECT_GE(_Joint.TargetPartIndex, 4u);
            EXPECT_LE(_Joint.TargetPartIndex, 7u);
            EXPECT_EQ(3u, _Joint.InsertedPartIndex);
            ++_HorizontalCuts;
        }
    }
    EXPECT_EQ(8u, _FrameCuts);
    EXPECT_EQ(4u, _HorizontalCuts);
}

TEST(TubeDesignerSecurityWindow1Test, CountsDrivePartAndJointTotals)
{
    SSecurityWindow1Parameters _Parameters;
    _Parameters.ProductCode = "ORDER-42";
    _Parameters.HorizontalCount = 3;
    _Parameters.MiddleVerticalCount = 2;

    const auto _Product = GenerateSecurityWindow1(_Parameters);

    ASSERT_EQ(7u, _Product.Parts.size());
    EXPECT_EQ(12u, _Product.Joints.size());
    EXPECT_EQ("ORDER-42-007", _Product.Parts.back().PartNumber);
}

TEST(TubeDesignerSecurityWindow1Test, RejectsUnsupportedOrUnsafeParameters)
{
    SSecurityWindow1Parameters _Parameters;
    _Parameters.MiddleVerticalCount = 11;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.Frame.Type = "round";
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.HorizontalCount = 2;
    _Parameters.FirstHorizontalTopOffset = 1700.0;
    _Parameters.LastHorizontalBottomOffset = 200.0;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.HorizontalCount = 101;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.HorizontalBranchReserve = -2.0;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.Width = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.Height = 60.0;
    _Parameters.VerticalBranchReserve = 0.0;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);
}
