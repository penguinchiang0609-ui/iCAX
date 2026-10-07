#include "pch.h"
#include <TubeDesigner/RecoveredPartDrawingFeatures.h>
#include <TubeDesigner/PunchGeometry.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <limits>
#include <set>

using namespace iCAX::TubeDesigner;
using namespace iCAX::GeometryData;
using namespace iCAX::GeometryData::Tube;
using namespace iCAX::Data;

namespace
{
    CTubeNeutralGeometry PlainTube()
    {
        CTubeNeutralGeometry geometry;
        geometry.BaseNodeID = "base";
        geometry.RootNodeID = "base";
        geometry.RecognitionStatus = ERecognitionStatus::Exact;
        SExtrudedRegionNode extrusion;
        extrusion.Frame.XDirection = { 0, 1, 0 };
        extrusion.Frame.YDirection = { 0, 0, 1 };
        extrusion.Frame.ZDirection = { 1, 0, 0 };
        extrusion.First = 0;
        extrusion.Last = 100;
        SSolidNode base;
        base.ID = "base";
        base.Data = std::move(extrusion);
        geometry.SolidNodes.push_back(std::move(base));
        return geometry;
    }

    SSolidNode TopCircle(double lowerZ = 8)
    {
        SExtrudedRegionNode cylinder;
        cylinder.Frame.Location = { 0, 0, 10 };
        cylinder.Frame.XDirection = { 1, 0, 0 };
        cylinder.Frame.YDirection = { 0, 1, 0 };
        cylinder.Frame.ZDirection = { 0, 0, -1 };
        cylinder.First = 0;
        cylinder.Last = 10 - lowerZ;
        SRegionLoop2 loop;
        loop.ID = "outer";
        SRegionCurve2 curve;
        curve.ID = "circle";
        Circle2 circle;
        circle.Placement.Location = { 30, 2 };
        circle.Radius = 3;
        curve.Segment.Curve = circle;
        curve.Segment.Range = { 0, 2 * std::acos(-1), false };
        loop.Curves.push_back(std::move(curve));
        cylinder.Section.Boundaries.push_back(std::move(loop));
        SSolidNode node;
        node.ID = "hole";
        node.Data = std::move(cylinder);
        SFeatureRelations relations;
        SFeatureMaterialEffect effect;
        effect.Role = EFeatureMaterialRole::Penetration;
        relations.MaterialEffect = effect;
        node.Relations = relations;
        SRemovalSemantics semantics;
        semantics.Extent = ERemovalExtentKind::Through;
        node.RemovalSemantics = semantics;
        return node;
    }

    SMaterialSpan ObservedBaseSpan(double first, double last)
    {
        SMaterialSpan span;
        span.Start.Kind = EMaterialSpanEndpointKind::HostBoundary;
        span.Start.Station.Value = first;
        span.Start.Station.Observability = EParameterObservability::Observed;
        span.Start.Boundary = SMaterialBoundaryReference{};
        span.Start.Boundary->HostNodeID = "base";
        span.End.Kind = EMaterialSpanEndpointKind::HostBoundary;
        span.End.Station.Value = last;
        span.End.Station.Observability = EParameterObservability::Observed;
        span.End.Boundary = SMaterialBoundaryReference{};
        span.End.Boundary->HostNodeID = "base";
        return span;
    }

    void Subtract(CTubeNeutralGeometry& geometry, SSolidNode cut)
    {
        const auto id = cut.ID;
        geometry.SolidNodes.push_back(std::move(cut));
        SBooleanNode difference;
        difference.Operation = EBooleanOperation::Difference;
        difference.Children = { "base", id };
        SSolidNode root;
        root.ID = "result";
        root.Data = std::move(difference);
        geometry.SolidNodes.push_back(std::move(root));
        geometry.RootNodeID = "result";
        geometry.RecognitionStatus = ERecognitionStatus::EquivalentButAmbiguous;
    }

    CTubeNeutralGeometry GroupedOppositeWallCircle()
    {
        auto geometry = PlainTube();
        auto generator = TopCircle(-50);
        generator.ID = "surface-group-cylinder-1/single-generator";
        generator.Metadata["source"] = "occ.part-surface-group";
        auto& extrusion = std::get<SExtrudedRegionNode>(generator.Data);
        extrusion.Frame.Location = { 0, 0, 50 };
        extrusion.Last = 100;
        auto& circle = std::get<Circle2>(
            extrusion.Section.Boundaries.front().Curves.front().Segment.Curve);
        circle.Placement.Location.Y = 0;
        generator.Relations->MaterialSpans.clear();
        for (const auto& [first, last] : {
            std::pair{ -50.0, -42.0 }, std::pair{ 42.0, 50.0 } })
        {
            generator.Relations->MaterialSpans.push_back(ObservedBaseSpan(first, last));
        }
        geometry.SolidNodes.push_back(std::move(generator));

        SAlternativeNode interpretation;
        interpretation.RecommendedCandidateID = "single-cross-material-generator";
        SInterpretationCandidate grouped;
        grouped.ID = interpretation.RecommendedCandidateID;
        grouped.CandidateNodeID = "surface-group-cylinder-1/single-generator";
        interpretation.Candidates.push_back(grouped);
        SInterpretationCandidate independent;
        independent.ID = "independent-coaxial-cuts";
        independent.CandidateNodeID = "surface-group-cylinder-1/independent-spans";
        interpretation.Candidates.push_back(independent);
        SSolidNode alternative;
        alternative.ID = "surface-group-cylinder-1";
        alternative.Data = std::move(interpretation);
        alternative.Metadata["source"] = "occ.part-surface-group-alternative";
        SRemovalSemantics semantics;
        semantics.Extent = ERemovalExtentKind::Through;
        alternative.RemovalSemantics = semantics;
        Subtract(geometry, std::move(alternative));
        return geometry;
    }

    CTubeNeutralGeometry EccentricOppositeWallCircle(bool alongY, double axisSign, double offset)
    {
        auto geometry = GroupedOppositeWallCircle();
        auto& extrusion = std::get<SExtrudedRegionNode>(geometry.SolidNodes[1].Data);
        extrusion.Frame.Location = alongY ? Point3{0, -60 * axisSign, 0}
            : Point3{0, 0, -60 * axisSign};
        extrusion.Frame.YDirection = alongY ? Direction3{0, 0, 1} : Direction3{0, 1, 0};
        extrusion.Frame.ZDirection = alongY ? Direction3{0, axisSign, 0}
            : Direction3{0, 0, axisSign};
        extrusion.Last = 120;
        std::get<Circle2>(extrusion.Section.Boundaries.front().Curves.front().Segment.Curve)
            .Placement.Location.Y = offset;
        return geometry;
    }

    double MaterialVolume(const TopoDS_Shape& shape)
    {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape, properties);
        return std::abs(properties.Mass());
    }
}

TEST(RecoveredPartDrawingFeatures, PlainTubeNeedsNoCutters)
{
    const auto result = MapNeutralTubeFeatures(PlainTube(), 100);
    EXPECT_TRUE(result.Complete);
    EXPECT_TRUE(result.Features.empty());
    EXPECT_TRUE(result.Ends.empty());
}

TEST(RecoveredPartDrawingFeatures, CircularSingleWallCutMapsToSystemCircleTool)
{
    auto geometry = PlainTube();
    Subtract(geometry, TopCircle());
    const auto result = MapNeutralTubeFeatures(geometry, 100);
    ASSERT_TRUE(result.Complete) << ::testing::PrintToString(result.UnsupportedNodeIDs);
    ASSERT_EQ(result.Features.size(), 1u);
    const auto feature = result.Features.front().To<ObjectMap>();
    EXPECT_EQ(feature.at("face").To<std::string>(), "top");
    EXPECT_DOUBLE_EQ(feature.at("station").To<double>(), 30);
    EXPECT_DOUBLE_EQ(feature.at("offset").To<double>(), 2);
    EXPECT_DOUBLE_EQ(feature.at("diameter").To<double>(), 6);
    EXPECT_EQ(feature.at("toolRef").To<ObjectMap>().at("id").To<std::string>(), "circle");
}

TEST(RecoveredPartDrawingFeatures, BlindCircleRequiresObservedSingleMaterialSpan)
{
    auto geometry = PlainTube();
    auto cut = TopCircle(9);
    cut.RemovalSemantics->Extent = ERemovalExtentKind::Blind;
    SMaterialSpan span;
    span.Start.Kind = EMaterialSpanEndpointKind::HostBoundary;
    span.Start.Station.Value = 8;
    span.End.Kind = EMaterialSpanEndpointKind::FeatureTermination;
    span.End.Station.Value = 9;
    cut.Relations->MaterialSpans.push_back(span);
    Subtract(geometry, std::move(cut));
    const auto result = MapNeutralTubeFeatures(geometry, 100);
    ASSERT_TRUE(result.Complete) << ::testing::PrintToString(result.UnsupportedNodeIDs);
    const auto feature = result.Features.front().To<ObjectMap>();
    EXPECT_TRUE(feature.at("blindHole").To<bool>());
    EXPECT_DOUBLE_EQ(feature.at("cutDepth").To<double>(), 1);
}

TEST(RecoveredPartDrawingFeatures, PlanarStartCutMapsToEndMiter)
{
    auto geometry = PlainTube();
    SHalfSpaceNode halfSpace;
    halfSpace.Boundary.Location = { 10, 0, 0 };
    halfSpace.Boundary.Normal = { 0.8944271909999159, -0.4472135954999579, 0 };
    halfSpace.KeepNegativeSide = true;
    SSolidNode cut;
    cut.ID = "end-cut";
    cut.Data = halfSpace;
    SFeatureRelations relations;
    SFeatureMaterialEffect effect;
    effect.Role = EFeatureMaterialRole::Truncation;
    relations.MaterialEffect = effect;
    cut.Relations = relations;
    Subtract(geometry, std::move(cut));
    const auto result = MapNeutralTubeFeatures(geometry, 100);
    ASSERT_TRUE(result.Complete) << ::testing::PrintToString(result.UnsupportedNodeIDs);
    ASSERT_TRUE(result.Ends.contains("start"));
    EXPECT_FALSE(result.Ends.contains("end"));
    const auto start = result.Ends.at("start").To<ObjectMap>();
    EXPECT_EQ(start.at("toolRef").To<ObjectMap>().at("id").To<std::string>(), "end-miter");
    EXPECT_DOUBLE_EQ(start.at("trim").To<double>(), 10);
    EXPECT_NEAR(start.at("toolParameters").To<ObjectMap>().at("angle").To<double>(),
        std::atan(0.5) * 180 / std::acos(-1), 1.0e-9);
}

TEST(RecoveredPartDrawingFeatures, CenteredWebRoundHoleUsesObservedMaterialRatherThanPaddedCylinder)
{
    auto geometry = PlainTube();
    auto cut = TopCircle(-10);
    // One web spans Z=-3..+3. The generator extends well outside it.
    cut.Relations->MaterialSpans.push_back(ObservedBaseSpan(-3, 3));
    Subtract(geometry, std::move(cut));
    const auto result = MapNeutralTubeFeatures(geometry, 100);
    ASSERT_TRUE(result.Complete);
    ASSERT_EQ(result.Features.size(), 1u);
    const auto feature = result.Features.front().To<ObjectMap>();
    EXPECT_EQ(feature.at("face").To<std::string>(), "top");
    EXPECT_FALSE(feature.at("opposite").To<bool>());
    EXPECT_FALSE(feature.at("blindHole").To<bool>());
    EXPECT_DOUBLE_EQ(feature.at("station").To<double>(), 30);
    EXPECT_DOUBLE_EQ(feature.at("offset").To<double>(), 2);

    auto alongY = PlainTube();
    auto lateral = TopCircle(-10);
    auto& cylinder = std::get<SExtrudedRegionNode>(lateral.Data);
    cylinder.Frame.Location = {0, 10, 0};
    cylinder.Frame.YDirection = {0, 0, 1};
    cylinder.Frame.ZDirection = {0, -1, 0};
    lateral.Relations->MaterialSpans.push_back(ObservedBaseSpan(-3, 3));
    Subtract(alongY, std::move(lateral));
    const auto yResult = MapNeutralTubeFeatures(alongY, 100);
    ASSERT_TRUE(yResult.Complete);
    EXPECT_EQ(yResult.Features.front().To<ObjectMap>().at("face").To<std::string>(), "right");
}

TEST(RecoveredPartDrawingFeatures, OpenChannelRoundHoleUsesActualWallSide)
{
    auto geometry = PlainTube();
    auto cut = TopCircle(-30);
    // Axis is -Z, so positive stations 20..25 locate a wall at Z=-25..-20.
    // The padded generator crosses zero, but this is one negative-side wall.
    cut.Relations->MaterialSpans.push_back(ObservedBaseSpan(20, 25));
    Subtract(geometry, std::move(cut));
    const auto result = MapNeutralTubeFeatures(geometry, 100);
    ASSERT_TRUE(result.Complete);
    EXPECT_EQ(result.Features.front().To<ObjectMap>().at("face").To<std::string>(), "bottom");
}

TEST(RecoveredPartDrawingFeatures, CenteredWebBlindHoleUsesItsObservedEntrance)
{
    for (bool entersPositive : {true, false}) {
        auto geometry = PlainTube();
        auto cut = TopCircle(-10);
        cut.RemovalSemantics->Extent = ERemovalExtentKind::Blind;
        auto span = ObservedBaseSpan(-2, 1);
        auto& termination = entersPositive ? span.End : span.Start;
        termination.Kind = EMaterialSpanEndpointKind::FeatureTermination;
        termination.Boundary.reset();
        cut.Relations->MaterialSpans.push_back(span);
        Subtract(geometry, std::move(cut));
        const auto result = MapNeutralTubeFeatures(geometry, 100);
        ASSERT_TRUE(result.Complete);
        const auto feature = result.Features.front().To<ObjectMap>();
        EXPECT_EQ(feature.at("face").To<std::string>(), entersPositive ? "top" : "bottom");
        EXPECT_TRUE(feature.at("blindHole").To<bool>());
        EXPECT_DOUBLE_EQ(feature.at("cutDepth").To<double>(), 3);
    }
}

TEST(RecoveredPartDrawingFeatures, CenteredWebMappingRequiresOneProvenBaseMaterialSpan)
{
    for (int invalidKind : {0, 1, 2, 3}) {
        auto geometry = PlainTube();
        auto cut = TopCircle(-10);
        auto span = ObservedBaseSpan(-3, 3);
        if (invalidKind == 0) span.Start.Boundary.reset();
        if (invalidKind == 1) span.End.Boundary->HostNodeID = "unrelated";
        if (invalidKind == 2) span.End.Kind = EMaterialSpanEndpointKind::FeatureTermination;
        cut.Relations->MaterialSpans.push_back(span);
        if (invalidKind == 3) cut.Relations->MaterialSpans.push_back(span);
        Subtract(geometry, std::move(cut));
        EXPECT_FALSE(MapNeutralTubeFeatures(geometry, 100).Complete) << invalidKind;
    }
}

TEST(RecoveredPartDrawingFeatures, RefusesOpaqueAlternativesAndCrossWallCuts)
{
    auto residual = PlainTube();
    SSolidNode opaque;
    opaque.ID = "opaque";
    opaque.Data = SResidualBRepNode{};
    Subtract(residual, std::move(opaque));
    EXPECT_FALSE(MapNeutralTubeFeatures(residual, 100).Complete);

    auto alternative = PlainTube();
    SSolidNode choice;
    choice.ID = "choice";
    choice.Data = SAlternativeNode{};
    Subtract(alternative, std::move(choice));
    EXPECT_FALSE(MapNeutralTubeFeatures(alternative, 100).Complete);

    auto crossWall = PlainTube();
    Subtract(crossWall, TopCircle(-10));
    EXPECT_FALSE(MapNeutralTubeFeatures(crossWall, 100).Complete);
}

TEST(RecoveredPartDrawingFeatures, GroupedCrossWallCircleMapsToOppositeTool)
{
    const auto geometry = GroupedOppositeWallCircle();
    const auto result = MapNeutralTubeFeatures(geometry, 100);
    ASSERT_TRUE(result.Complete) << ::testing::PrintToString(result.UnsupportedNodeIDs);
    ASSERT_EQ(result.Features.size(), 1u);
    const auto feature = result.Features.front().To<ObjectMap>();
    EXPECT_TRUE(feature.at("opposite").To<bool>());
    EXPECT_FALSE(feature.at("blindHole").To<bool>());
    EXPECT_EQ(feature.at("face").To<std::string>(), "top");
    EXPECT_DOUBLE_EQ(feature.at("offset").To<double>(), 0);
    EXPECT_DOUBLE_EQ(feature.at("station").To<double>(), 30);
    EXPECT_DOUBLE_EQ(feature.at("diameter").To<double>(), 6);
}

TEST(RecoveredPartDrawingFeatures, GroupedCrossWallRejectsOtherInterpretationsAndUnequalSpans)
{
    auto selectedIndependent = GroupedOppositeWallCircle();
    auto& choice = std::get<SAlternativeNode>(selectedIndependent.SolidNodes[2].Data);
    choice.SelectedCandidateID = "independent-coaxial-cuts";
    EXPECT_FALSE(MapNeutralTubeFeatures(selectedIndependent, 100).Complete);

    auto unrelated = GroupedOppositeWallCircle();
    unrelated.SolidNodes[2].Metadata["source"] = "other-alternative";
    EXPECT_FALSE(MapNeutralTubeFeatures(unrelated, 100).Complete);

    auto unequal = GroupedOppositeWallCircle();
    unequal.SolidNodes[1].Relations->MaterialSpans[1].Start.Station.Value = 43;
    EXPECT_FALSE(MapNeutralTubeFeatures(unequal, 100).Complete);

    auto extraSpan = GroupedOppositeWallCircle();
    extraSpan.SolidNodes[1].Relations->MaterialSpans.push_back(
        extraSpan.SolidNodes[1].Relations->MaterialSpans.front());
    EXPECT_FALSE(MapNeutralTubeFeatures(extraSpan, 100).Complete);

}

TEST(RecoveredPartDrawingFeatures, EccentricCrossWallCirclesReplayWithTheSameSignedOffsetOnBothFaces)
{
    const auto outer = BRepPrimAPI_MakeBox(gp_Pnt(0, -50, -50), 100, 100, 100).Shape();
    const auto inner = BRepPrimAPI_MakeBox(gp_Pnt(-1, -42, -42), 102, 84, 84).Shape();
    BRepAlgoAPI_Cut hollow(outer, inner);
    ASSERT_TRUE(hollow.IsDone());
    const auto base = hollow.Shape();
    ASSERT_TRUE(BRepCheck_Analyzer(base).IsValid());

    for (bool alongY : {false, true}) for (double sign : {-1.0, 1.0})
        for (double offset : {-7.0, 7.0})
    {
        SCOPED_TRACE(::testing::Message() << "alongY=" << alongY
            << " axisSign=" << sign << " offset=" << offset);
        auto geometry = EccentricOppositeWallCircle(alongY, sign, offset);
        // Reversed endpoint order must not change the wall or offset meaning.
        if (sign > 0) for (auto& span : geometry.SolidNodes[1].Relations->MaterialSpans)
            std::swap(span.Start, span.End);
        const auto result = MapNeutralTubeFeatures(geometry, 100);
        ASSERT_TRUE(result.Complete) << ::testing::PrintToString(result.UnsupportedNodeIDs);
        ASSERT_EQ(result.Features.size(), 2u);
        std::vector<SPunchFeature> features;
        std::set<std::string> faces;
        for (const auto& value : result.Features)
        {
            const auto mapped = value.To<ObjectMap>();
            EXPECT_FALSE(mapped.at("opposite").To<bool>());
            EXPECT_FALSE(mapped.at("blindHole").To<bool>());
            EXPECT_DOUBLE_EQ(mapped.at("offset").To<double>(), offset);
            EXPECT_DOUBLE_EQ(mapped.at("station").To<double>(), 30);
            EXPECT_DOUBLE_EQ(mapped.at("diameter").To<double>(), 6);
            SPunchFeature feature;
            feature.ID = mapped.at("id").To<std::string>();
            feature.Face = mapped.at("face").To<std::string>();
            feature.Reference = mapped.at("reference").To<std::string>();
            feature.LayoutDatum = mapped.at("layoutDatum").To<std::string>();
            feature.Station = mapped.at("station").To<double>();
            feature.Offset = mapped.at("offset").To<double>();
            feature.Diameter = mapped.at("diameter").To<double>();
            faces.insert(feature.Face);
            features.push_back(std::move(feature));
        }
        EXPECT_EQ(faces, (alongY ? std::set<std::string>{"left", "right"}
            : std::set<std::string>{"bottom", "top"}));
        EXPECT_NE(features[0].ID, features[1].ID);

        // Compare actual editor cuts with one independent full-length cylinder.
        // Mirroring either offset would leave material at one opening and cut
        // an unwanted hole elsewhere, even though the volumes alone agree.
        const auto origin = alongY ? gp_Pnt(30, -60, offset) : gp_Pnt(30, offset, -60);
        const auto axis = alongY ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1);
        const auto cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(origin, axis), 3, 120).Shape();
        BRepAlgoAPI_Cut expected(base, cylinder);
        ASSERT_TRUE(expected.IsDone());
        const auto replay = BuildPunchGeometry(base, features);
        ASSERT_TRUE(BRepCheck_Analyzer(replay).IsValid());
        BRepAlgoAPI_Cut missing(expected.Shape(), replay);
        BRepAlgoAPI_Cut unexpected(replay, expected.Shape());
        ASSERT_TRUE(missing.IsDone());
        ASSERT_TRUE(unexpected.IsDone());
        ASSERT_TRUE(BRepCheck_Analyzer(missing.Shape()).IsValid());
        ASSERT_TRUE(BRepCheck_Analyzer(unexpected.Shape()).IsValid());
        EXPECT_LE((MaterialVolume(missing.Shape()) + MaterialVolume(unexpected.Shape()))
            / MaterialVolume(expected.Shape()), 1.0e-8);
    }
}

TEST(RecoveredPartDrawingFeatures, EccentricCrossWallSplitRejectsUnprovenOrUnrepresentableWallsAtomically)
{
    for (int invalidKind = 0; invalidKind < 11; ++invalidKind)
    {
        SCOPED_TRACE(invalidKind);
        auto geometry = EccentricOppositeWallCircle(false, -1, 7);
        auto& generator = geometry.SolidNodes[1];
        auto& extrusion = std::get<SExtrudedRegionNode>(generator.Data);
        auto& spans = generator.Relations->MaterialSpans;
        auto& curve = extrusion.Section.Boundaries.front().Curves.front();
        if (invalidKind == 0) spans[1].Start.Boundary.reset();
        if (invalidKind == 1) spans[1].End.Boundary->HostNodeID = "other";
        if (invalidKind == 2) spans[1].End.Kind = EMaterialSpanEndpointKind::FeatureTermination;
        if (invalidKind == 3) spans[1].Start.Station.Observability = EParameterObservability::Reconstructed;
        if (invalidKind == 4) spans[1] = ObservedBaseSpan(41, 50); // Unequal wall thickness.
        if (invalidKind == 5) spans[1] = ObservedBaseSpan(-30, -22); // Both walls on one side.
        if (invalidKind == 6) extrusion.Last = 100; // Generator no longer covers the negative wall.
        if (invalidKind == 7) extrusion.Frame.ZDirection = {0, 0.1, -std::sqrt(0.99)};
        if (invalidKind == 8) std::get<Circle2>(curve.Segment.Curve).Placement.Location.Y
            = std::numeric_limits<double>::infinity();
        if (invalidKind == 9) curve.Segment.Range.Last = std::acos(-1); // Only a semicircle.
        if (invalidKind == 10) generator.Metadata["source"] = "unproven-cylinder";
        const auto result = MapNeutralTubeFeatures(geometry, 100);
        EXPECT_FALSE(result.Complete);
        EXPECT_TRUE(result.Features.empty());
        EXPECT_EQ(result.UnsupportedNodeIDs, std::vector<std::string>{"surface-group-cylinder-1"});
    }
}

namespace
{
    CTubeNeutralGeometry ExactCurvePocket(bool sideY, double sign)
    {
        auto geometry = PlainTube();
        auto node = TopCircle();
        node.ID = "curve-pocket";
        node.Metadata["source"] = "occ.planar-cap-extrusion";
        auto& cut = std::get<SExtrudedRegionNode>(node.Data);
        cut.Frame.Location = sideY ? Point3{0,sign*10,0} : Point3{0,0,sign*10};
        cut.Frame.XDirection = {1,0,0};
        cut.Frame.YDirection = sideY ? Direction3{0,0,1} : Direction3{0,1,0};
        cut.Frame.ZDirection = sideY ? Direction3{0,-sign,0} : Direction3{0,0,-sign};
        cut.First=0; cut.Last=0.2;
        cut.Section.Boundaries.clear();
        SRegionLoop2 outer; outer.ID="outer";
        NURBS2 spline;
        spline.Degree=2;
        spline.Poles={{20,0},{25,-2},{30,0}};
        spline.Weights={1,0.8,1};
        spline.Knots={0,1}; spline.Multiplicities={3,3};
        outer.Curves.push_back({"curve",{spline,{0,1,true}}});
        for(const auto& segment:std::vector<Segment2>{{{20,0},{20,5}},{{20,5},{30,5}},{{30,5},{30,0}}})
            outer.Curves.push_back({"line",{segment,{0,1,false}}});
        SRegionLoop2 inner; inner.ID="island";
        for(const auto& segment:std::vector<Segment2>{{{24,1},{26,1}},{{26,1},{26,3}},{{26,3},{24,3}},{{24,3},{24,1}}})
            inner.Curves.push_back({"island-edge",{segment,{0,1,false}}});
        cut.Section.Boundaries={outer,inner};
        node.RemovalSemantics->Extent=ERemovalExtentKind::Blind;
        auto span=ObservedBaseSpan(-10,-9.8);
        span.End.Kind=EMaterialSpanEndpointKind::FeatureTermination;
        span.End.Boundary.reset();
        node.Relations->MaterialSpans={span};
        Subtract(geometry,std::move(node));
        return geometry;
    }
}

TEST(RecoveredPartDrawingFeatures, ExactCurvePocketKeepsSplineIslandDepthAndFourEntryDirections)
{
    for(bool sideY:{false,true}) for(double sign:{-1.,1.}) {
        SCOPED_TRACE(sideY);
        SCOPED_TRACE(sign);
        const auto result=MapNeutralTubeFeatures(ExactCurvePocket(sideY,sign),100);
        ASSERT_TRUE(result.Complete);
        ASSERT_EQ(result.Features.size(),1u);
        const auto feature=result.Features.front().To<ObjectMap>();
        EXPECT_EQ(feature.at("type").To<std::string>(),"curve-pocket");
        EXPECT_EQ(feature.at("face").To<std::string>(),sideY?(sign>0?"right":"left"):(sign>0?"top":"bottom"));
        EXPECT_NEAR(feature.at("cutDepth").To<double>(),0.2,1e-12);
        EXPECT_TRUE(feature.at("blindHole").To<bool>());
        EXPECT_FALSE(feature.at("opposite").To<bool>());
        EXPECT_NEAR(feature.at("station").To<double>(),25,1e-12);
        const auto contours=feature.at("section").To<ObjectMap>().at("profile").To<ObjectMap>().at("contours").To<VariantArray>();
        ASSERT_EQ(contours.size(),2u);
        const auto curves=contours.front().To<ObjectMap>().at("segments").To<VariantArray>();
        const auto spline=curves.front().To<ObjectMap>();
        EXPECT_EQ(spline.at("kind").To<std::string>(),"nurbs");
        EXPECT_TRUE(spline.at("reversed").To<bool>());
        EXPECT_EQ(spline.at("weights").To<VariantArray>().size(),3u);
        EXPECT_EQ(spline.at("knots").To<VariantArray>().size(),2u);
        EXPECT_EQ(spline.at("controlPoints").To<VariantArray>().size(),3u);
        EXPECT_EQ(spline.at("startParameter").To<double>(),0);
        EXPECT_EQ(spline.at("endParameter").To<double>(),1);
    }
}

TEST(RecoveredPartDrawingFeatures, ExactCurvePocketRejectsMissingEvidenceAndApproximateBoundaries)
{
    for(int bad=0;bad<8;++bad) {
        SCOPED_TRACE(bad);
        auto geometry=ExactCurvePocket(false,1);
        auto& node=geometry.SolidNodes[1];
        auto& cut=std::get<SExtrudedRegionNode>(node.Data);
        if(bad==0)node.Metadata["source"]="unproven-region";
        if(bad==1)node.Relations->MaterialSpans.front().Start.Boundary.reset();
        if(bad==2)node.Relations->MaterialSpans.front().End.Station.Observability=EParameterObservability::Reconstructed;
        if(bad==3)node.Relations->MaterialSpans.front().End.Kind=EMaterialSpanEndpointKind::HostBoundary;
        if(bad==4)cut.Last=0.1;
        if(bad==5)cut.Frame.XDirection={2,0,0};
        if(bad==6)cut.Section.Boundaries.front().Curves.front().Segment.Curve=Polyline2{{{20,0},{25,-2},{30,0}},false};
        if(bad==7)std::get<NURBS2>(cut.Section.Boundaries.front().Curves.front().Segment.Curve).Poles.front().X=std::numeric_limits<double>::infinity();
        const auto result=MapNeutralTubeFeatures(geometry,100);
        EXPECT_FALSE(result.Complete);
        EXPECT_TRUE(result.Features.empty());
        EXPECT_EQ(result.UnsupportedNodeIDs,std::vector<std::string>{"curve-pocket"});
    }
}

TEST(RecoveredPartDrawingFeatures, FiniteThroughCurveRetainsCurvesIslandsAndEntrySide)
{
    for (bool sideY : {false,true}) for (double sign : {-1.,1.}) {
        SCOPED_TRACE(sideY);
        SCOPED_TRACE(sign);
        auto geometry=ExactCurvePocket(sideY,sign);
        auto& node=geometry.SolidNodes[1];
        node.Metadata["source"]="occ.removal-extrusion";
        node.Metadata["independentReplay"]="complete-removal";
        node.RemovalSemantics->Extent=ERemovalExtentKind::Through;
        node.Relations->MaterialSpans.clear();
        std::get<SExtrudedRegionNode>(node.Data).Last=2.;
        const auto result=MapNeutralTubeFeatures(geometry,100);
        ASSERT_TRUE(result.Complete);
        ASSERT_EQ(result.Features.size(),1u);
        const auto feature=result.Features.front().To<ObjectMap>();
        EXPECT_EQ(feature.at("face").To<std::string>(),sideY?(sign>0?"right":"left"):(sign>0?"top":"bottom"));
        EXPECT_FALSE(feature.at("blindHole").To<bool>());
        EXPECT_FALSE(feature.at("opposite").To<bool>());
        const auto contours=feature.at("section").To<ObjectMap>().at("profile").To<ObjectMap>().at("contours").To<VariantArray>();
        ASSERT_EQ(contours.size(),2u);
        const auto spline=contours.front().To<ObjectMap>().at("segments").To<VariantArray>().front().To<ObjectMap>();
        EXPECT_EQ(spline.at("kind").To<std::string>(),"nurbs");
        EXPECT_TRUE(spline.at("reversed").To<bool>());
        const auto weights=spline.at("weights").To<VariantArray>();
        ASSERT_EQ(weights.size(),3u);
        EXPECT_DOUBLE_EQ(weights[0].To<double>(),1.);
        EXPECT_DOUBLE_EQ(weights[1].To<double>(),0.8);
        EXPECT_DOUBLE_EQ(weights[2].To<double>(),1.);
    }
}

TEST(RecoveredPartDrawingFeatures, FiniteThroughCurveRejectsUnprovenOrAmbiguousRemoval)
{
    for(int bad=0;bad<9;++bad) {
        SCOPED_TRACE(bad);
        auto geometry=ExactCurvePocket(false,1);
        auto& node=geometry.SolidNodes[1];
        auto& cut=std::get<SExtrudedRegionNode>(node.Data);
        node.Metadata["source"]="occ.removal-extrusion";
        node.Metadata["independentReplay"]="complete-removal";
        node.RemovalSemantics->Extent=ERemovalExtentKind::Through;
        node.Relations->MaterialSpans.clear(); cut.Last=2.;
        if(bad==0)node.Metadata.erase("independentReplay");
        if(bad==1)node.Metadata["independentReplay"]="partial-removal";
        if(bad==2)node.Metadata["source"]="occ.part-surface-group";
        if(bad==3)node.RemovalSemantics->Extent=ERemovalExtentKind::Blind;
        if(bad==4)node.Relations->MaterialSpans={ObservedBaseSpan(-10,-8)};
        if(bad==5)cut.Last=cut.First;
        if(bad==6)cut.First=std::numeric_limits<double>::quiet_NaN();
        if(bad==7)cut.Frame.ZDirection={1,0,0};
        if(bad==8)node.Relations->MaterialEffect->Role=EFeatureMaterialRole::Truncation;
        const auto result=MapNeutralTubeFeatures(geometry,100);
        EXPECT_FALSE(result.Complete);
        EXPECT_TRUE(result.Features.empty());
        EXPECT_EQ(result.UnsupportedNodeIDs,std::vector<std::string>{"curve-pocket"});
    }
}

namespace
{
    CTubeNeutralGeometry ExactCurveAlternatives()
    {
        auto geometry = ExactCurvePocket(false, 1);
        auto& exact = geometry.SolidNodes[1];
        exact.Metadata["source"] = "occ.removal-extrusion";
        exact.Metadata["independentReplay"] = "complete-removal";
        exact.RemovalSemantics->Extent = ERemovalExtentKind::Through;
        exact.Relations->MaterialSpans.clear();
        std::get<SExtrudedRegionNode>(exact.Data).Last = 2;
        auto approximate = exact;
        approximate.ID = "approximate-cut";
        approximate.Metadata.erase("independentReplay");
        std::get<SExtrudedRegionNode>(approximate.Data).Last = 3;
        geometry.SolidNodes.push_back(std::move(approximate));
        SAlternativeNode choices;
        SInterpretationCandidate unproven;
        unproven.ID = "unproven";
        unproven.CandidateNodeID = "approximate-cut";
        unproven.Confidence = 1;
        choices.RecommendedCandidateID = unproven.ID;
        choices.Candidates.push_back(unproven);
        SInterpretationCandidate proven;
        proven.ID = "exact";
        proven.CandidateNodeID = "curve-pocket";
        choices.Candidates.push_back(proven);
        SSolidNode node;
        node.ID = "choices";
        node.Metadata["source"] = "occ.enumerated-hypotheses";
        node.Data = std::move(choices);
        geometry.SolidNodes.push_back(std::move(node));
        std::get<SBooleanNode>(geometry.SolidNodes[2].Data).Children[1] = "choices";
        return geometry;
    }
}

TEST(RecoveredPartDrawingFeatures, CurveAlternativeSelectsCompleteReplayInsteadOfConfidence)
{
    auto geometry = ExactCurveAlternatives();
    const auto result = MapNeutralTubeFeatures(geometry, 100);
    ASSERT_TRUE(result.Complete);
    ASSERT_EQ(result.Features.size(), 1u);
    const auto feature = result.Features.front().To<ObjectMap>();
    EXPECT_EQ(feature.at("type").To<std::string>(), "curve-pocket");
    EXPECT_EQ(feature.at("face").To<std::string>(), "top");
    EXPECT_FALSE(feature.at("blindHole").To<bool>());
    EXPECT_EQ(feature.at("section").To<ObjectMap>().at("profile").To<ObjectMap>()
        .at("contours").To<VariantArray>().size(), 2u);
    std::get<SAlternativeNode>(geometry.SolidNodes.back().Data).SelectedCandidateID = "exact";
    EXPECT_TRUE(MapNeutralTubeFeatures(geometry, 100).Complete);
}

TEST(RecoveredPartDrawingFeatures, CurveAlternativeRejectsMissingProofAndOpaqueSources)
{
    for (int bad = 0; bad < 7; ++bad)
    {
        SCOPED_TRACE(bad);
        auto geometry = ExactCurveAlternatives();
        auto& exact = geometry.SolidNodes[1];
        auto& choice = geometry.SolidNodes.back();
        if (bad == 0) exact.Metadata.erase("independentReplay");
        if (bad == 1) exact.Metadata["independentReplay"] = "partial-removal";
        if (bad == 2) choice.Metadata["source"] = "opaque-alternatives";
        if (bad == 3) std::get<SAlternativeNode>(choice.Data).Candidates.back().CandidateNodeID = "missing";
        if (bad == 4) std::get<SExtrudedRegionNode>(exact.Data).Section.Boundaries.front()
            .Curves.front().Segment.Curve = Polyline2{{{20, 0}, {25, -2}, {30, 0}}, false};
        if (bad == 5) std::get<SAlternativeNode>(choice.Data).SelectedCandidateID = "unproven";
        if (bad == 6) std::get<SAlternativeNode>(choice.Data).SelectedCandidateID = "missing";
        const auto result = MapNeutralTubeFeatures(geometry, 100);
        EXPECT_FALSE(result.Complete);
        EXPECT_TRUE(result.Features.empty());
        EXPECT_EQ(result.UnsupportedNodeIDs, std::vector<std::string>{"choices"});
    }
}

namespace
{
    CPlanarRegion2 P1033AngleSection(double top)
    {
        // Exact original STEP section, centred on its actual Y/Z bounds.
        // The end cuts stop at original Z=9; the full blank reaches Z=51.
        constexpr double yc=(-70.1-22.0823001646925)/2, zc=25.5;
        const auto p=[](double y,double z) { return Point2{y-yc,z-zc}; };
        SRegionLoop2 loop; loop.ID="outer";
        const auto line=[&](Point2 a,Point2 b) {
            loop.Curves.push_back({"edge-"+std::to_string(loop.Curves.size()),{Segment2{a,b},{0,1,false}}});
        };
        const auto arc=[&](double radius,bool reversed) {
            Circle2 circle; circle.Placement.Location=p(-30.2823001646925,8.2); circle.Radius=radius;
            loop.Curves.push_back({"arc-"+std::to_string(loop.Curves.size()),
                {circle,{-std::acos(-1)/2,0,reversed}}});
        };
        line(p(-70.1,0),p(-30.2823001646925,0));
        arc(8.2,false);
        line(p(-22.0823001646925,8.2),p(-22.0823001646925,top));
        line(p(-22.0823001646925,top),p(-27.0823001646925,top));
        line(p(-27.0823001646925,top),p(-27.0823001646925,8.2));
        arc(3.2,true);
        line(p(-30.2823001646925,5),p(-70.1,5));
        line(p(-70.1,5),p(-70.1,0));
        CPlanarRegion2 section; section.Boundaries.push_back(std::move(loop)); return section;
    }

    CTubeNeutralGeometry P1033EndSteps()
    {
        auto geometry=PlainTube();
        auto& base=std::get<SExtrudedRegionNode>(geometry.SolidNodes.front().Data);
        base.Last=138; base.Section=P1033AngleSection(51);
        const auto blank=base;
        for (bool start:{true,false}) {
            SExtrudedRegionNode cut=blank;
            cut.First=start?0:90; cut.Last=start?48:138; cut.Section=P1033AngleSection(9);
            SSolidNode node; node.ID=start?"start-step":"end-step"; node.Data=std::move(cut);
            node.Metadata["source"]="occ.removal-extrusion";
            // Converter writes this only after the exact finite extrusion has
            // independently replayed against the complete removed component.
            node.Metadata["independentReplay"]="complete-removal";
            geometry.SolidNodes.push_back(std::move(node));
        }
        SBooleanNode cuts; cuts.Operation=EBooleanOperation::Union; cuts.Children={"start-step","end-step"};
        SSolidNode cut; cut.ID="all-removals"; cut.Data=std::move(cuts);
        Subtract(geometry,std::move(cut));
        return geometry;
    }
}

TEST(RecoveredPartDrawingFeatures, ExactFiniteEndStepsReuseEditableStepToolForP1033)
{
    const auto result=MapNeutralTubeFeatures(P1033EndSteps(),138);
    ASSERT_TRUE(result.Complete)<<::testing::PrintToString(result.UnsupportedNodeIDs);
    EXPECT_TRUE(result.Features.empty());
    ASSERT_EQ(result.Ends.size(),2u);
    for (const auto* key:{"start","end"}) {
        const auto end=result.Ends.at(key).To<ObjectMap>();
        EXPECT_EQ(end.at("type").To<std::string>(),"template");
        const auto tool=end.at("toolRef").To<ObjectMap>();
        EXPECT_EQ(tool.at("id").To<std::string>(),"end-step-z");
        EXPECT_EQ(tool.at("scope").To<std::string>(),"system");
        EXPECT_DOUBLE_EQ(end.at("trim").To<double>(),0);
        EXPECT_DOUBLE_EQ(end.at("rotation").To<double>(),90);
        EXPECT_EQ(end.at("datum").To<std::string>(),"long");
        const auto parameters=end.at("toolParameters").To<ObjectMap>();
        ASSERT_EQ(parameters.size(),3u);
        EXPECT_DOUBLE_EQ(parameters.at("depth").To<double>(),48);
        EXPECT_NEAR(parameters.at("splitOffset").To<double>(),-16.5,1e-12);
        EXPECT_EQ(parameters.at("hand").To<std::string>(),"negative");
    }
}

TEST(RecoveredPartDrawingFeatures, EndStepUsesWorldSectionAndFiniteAxisRatherThanCurveOrder)
{
    auto geometry=P1033EndSteps();
    // Reversing the finite cutter axis preserves its world section and end.
    auto& endCut=std::get<SExtrudedRegionNode>(geometry.SolidNodes[2].Data);
    endCut.Frame.Location.X=138; endCut.Frame.ZDirection={-1,0,0}; endCut.Frame.YDirection={0,0,-1};
    endCut.First=0; endCut.Last=48;
    for (auto& curve:endCut.Section.Boundaries.front().Curves) {
        if (auto* line=std::get_if<Segment2>(&curve.Segment.Curve)) {
            line->Start.Y=-line->Start.Y; line->End.Y=-line->End.Y;
        } else if (auto* circle=std::get_if<Circle2>(&curve.Segment.Curve)) {
            circle->Placement.Location.Y=-circle->Placement.Location.Y;
            circle->Placement.XDirection.Y=-circle->Placement.XDirection.Y;
            circle->Placement.YDirection.Y=-circle->Placement.YDirection.Y;
        }
    }
    const auto result=MapNeutralTubeFeatures(geometry,138);
    ASSERT_TRUE(result.Complete)<<::testing::PrintToString(result.UnsupportedNodeIDs);
    const auto end=result.Ends.at("end").To<ObjectMap>();
    EXPECT_DOUBLE_EQ(end.at("rotation").To<double>(),90);
    EXPECT_DOUBLE_EQ(end.at("toolParameters").To<ObjectMap>().at("depth").To<double>(),48);
    EXPECT_NEAR(end.at("toolParameters").To<ObjectMap>().at("splitOffset").To<double>(),-16.5,1e-12);
}

TEST(RecoveredPartDrawingFeatures, EndStepRejectsUnprovenInteriorAndNonHalfSectionRemovals)
{
    for (int bad=0;bad<8;++bad) {
        SCOPED_TRACE(bad);
        auto geometry=P1033EndSteps();
        auto& node=geometry.SolidNodes[1];
        auto& cut=std::get<SExtrudedRegionNode>(node.Data);
        if (bad==0) node.Metadata.erase("independentReplay");
        if (bad==1) node.Metadata["source"]="unproven-extrusion";
        if (bad==2) cut.First=2; // A detached side feature is not an end step.
        if (bad==3) cut.Last=138; // Removing the selected side at both ends.
        if (bad==4) cut.Section.Boundaries.push_back(cut.Section.Boundaries.front());
        if (bad==5) std::get<Circle2>(cut.Section.Boundaries.front().Curves[1].Segment.Curve).Radius=8.1;
        if (bad==6) std::get<Segment2>(cut.Section.Boundaries.front().Curves[0].Segment.Curve).Start.Y+=0.1;
        if (bad==7) cut.Last=std::numeric_limits<double>::infinity();
        const auto result=MapNeutralTubeFeatures(geometry,138);
        EXPECT_FALSE(result.Complete);
        EXPECT_FALSE(result.Ends.contains("start"));
        EXPECT_EQ(result.UnsupportedNodeIDs,std::vector<std::string>{"start-step"});
    }
}

namespace
{
    CPlanarRegion2 EndStepPolygon(std::initializer_list<Point2> points)
    {
        const std::vector<Point2> vertices(points);
        SRegionLoop2 loop;
        loop.ID = "outer";
        for (size_t index = 0; index < vertices.size(); ++index)
            loop.Curves.push_back({"edge-" + std::to_string(index),
                {Segment2{vertices[index], vertices[(index + 1) % vertices.size()]}, {0, 1, false}}});
        CPlanarRegion2 section;
        section.Boundaries.push_back(std::move(loop));
        return section;
    }

    CTubeNeutralGeometry SharpChannelEndStep(bool start, bool completeSubset)
    {
        auto geometry = PlainTube();
        auto& base = std::get<SExtrudedRegionNode>(geometry.SolidNodes.front().Data);
        // Open +Y channel: 4 mm walls, Y [-20, 20], Z [-15, 15].
        base.Section = EndStepPolygon({{20, -15}, {-20, -15}, {-20, 15}, {20, 15},
            {20, 11}, {-16, 11}, {-16, -11}, {20, -11}});
        SExtrudedRegionNode cut = base;
        cut.First = start ? 0 : 80;
        cut.Last = start ? 20 : 100;
        // Remove the connected back half, Y <= -10, at one end. The new
        // Y=-10 boundary intersects only Z [-15,-11] and [11,15].
        cut.Section = EndStepPolygon({{-10, -15}, {-20, -15}, {-20, 15}, {-10, 15},
            {-10, 11}, {-16, 11}, {-16, -11}, {-10, -11}});
        SSolidNode node;
        node.ID = "channel-end-step";
        node.Data = std::move(cut);
        node.Metadata["source"] = "occ.removal-extrusion";
        node.Metadata["independentReplay"] = completeSubset ? "complete-subset" : "complete-removal";
        if (completeSubset) node.Metadata["recognitionMode"] = "global-subset";
        Subtract(geometry, std::move(node));
        return geometry;
    }
}

TEST(RecoveredPartDrawingFeatures, ChannelEndStepMatchesBothSeparatedFlangeSeams)
{
    for (const bool start : {false, true})
        for (const bool completeSubset : {false, true})
        {
            SCOPED_TRACE(start);
            SCOPED_TRACE(completeSubset);
            const auto result = MapNeutralTubeFeatures(SharpChannelEndStep(start, completeSubset), 100);
            ASSERT_TRUE(result.Complete) << ::testing::PrintToString(result.UnsupportedNodeIDs);
            EXPECT_TRUE(result.Features.empty());
            ASSERT_EQ(result.Ends.size(), 1u);
            const auto end = result.Ends.at(start ? "start" : "end").To<ObjectMap>();
            EXPECT_EQ(end.at("type").To<std::string>(), "template");
            const auto tool = end.at("toolRef").To<ObjectMap>();
            EXPECT_EQ(tool.at("id").To<std::string>(), "end-step-z");
            EXPECT_EQ(tool.at("scope").To<std::string>(), "system");
            EXPECT_DOUBLE_EQ(end.at("trim").To<double>(), 0);
            EXPECT_NEAR(end.at("rotation").To<double>(), 0, 1.0e-12);
            EXPECT_EQ(end.at("datum").To<std::string>(), "long");
            const auto parameters = end.at("toolParameters").To<ObjectMap>();
            ASSERT_EQ(parameters.size(), 3u);
            EXPECT_DOUBLE_EQ(parameters.at("depth").To<double>(), 20);
            EXPECT_DOUBLE_EQ(parameters.at("splitOffset").To<double>(), -10);
            EXPECT_EQ(parameters.at("hand").To<std::string>(), "negative");
        }
}

TEST(RecoveredPartDrawingFeatures, ChannelEndStepRejectsMissingMaterialIntervalsAndUnprovenSubsets)
{
    const char* cases[] = {
        "one flange only", "missing seam", "non-coplanar seam", "one seam spanning the void",
        "partial proof", "missing proof", "subset without mode", "subset with wrong mode",
        "same crossings but wrong interval pairing", "duplicate seam", "cut through a base vertex"
    };
    for (size_t bad = 0; bad < std::size(cases); ++bad)
    {
        SCOPED_TRACE(cases[bad]);
        auto geometry = SharpChannelEndStep(false, true);
        auto& node = geometry.SolidNodes[1];
        auto& cut = std::get<SExtrudedRegionNode>(node.Data);
        auto& curves = cut.Section.Boundaries.front().Curves;
        if (bad == 0)
            // Valid closed rectangle in only the upper flange. Its three
            // inherited edges and one seam must not imply cutting both flanges.
            cut.Section = EndStepPolygon({{-10, 11}, {20, 11}, {20, 15}, {-10, 15}});
        if (bad == 1) curves.erase(curves.begin() + 3);
        if (bad == 2)
        {
            // Preserve a closed contour and inherited outer/inner edges while
            // making the upper new seam oblique to the lower seam's plane.
            std::get<Segment2>(curves[2].Segment.Curve).End.X = -9;
            std::get<Segment2>(curves[3].Segment.Curve).Start.X = -9;
        }
        if (bad == 3)
            cut.Section = EndStepPolygon({{-10, -15}, {-20, -15}, {-20, 15}, {-10, 15}});
        if (bad == 4) node.Metadata["independentReplay"] = "partial-removal";
        if (bad == 5) node.Metadata.erase("independentReplay");
        if (bad == 6) node.Metadata.erase("recognitionMode");
        if (bad == 7) node.Metadata["recognitionMode"] = "exact-residual-section";
        if (bad == 8)
        {
            // Four endpoints and two coplanar seams are insufficient: these
            // pairings bridge empty space instead of the two material spans.
            std::get<Segment2>(curves[3].Segment.Curve).End.Y = -11;
            std::get<Segment2>(curves[7].Segment.Curve).Start.Y = 11;
        }
        if (bad == 9) curves.push_back(curves[3]);
        if (bad == 10)
        {
            auto& base = std::get<SExtrudedRegionNode>(geometry.SolidNodes[0].Data);
            // This extra collinear vertex changes no material. Multi-seam
            // vertex/tangent parity is deliberately unsupported for now.
            base.Section = EndStepPolygon({{20, -15}, {-10, -15}, {-20, -15}, {-20, 15},
                {20, 15}, {20, 11}, {-16, 11}, {-16, -11}, {20, -11}});
        }
        const auto result = MapNeutralTubeFeatures(geometry, 100);
        EXPECT_FALSE(result.Complete);
        EXPECT_TRUE(result.Features.empty());
        EXPECT_TRUE(result.Ends.empty());
        EXPECT_EQ(result.UnsupportedNodeIDs, std::vector<std::string>{"channel-end-step"});
    }
}
