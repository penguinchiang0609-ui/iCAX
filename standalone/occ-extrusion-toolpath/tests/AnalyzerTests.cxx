#include "TestShapes.hxx"

#include "etp/Analyzer.hxx"
#include "etp/Pipeline.hxx"

#include <BRepAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp.hxx>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Require(const bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    etp::ExtrusionToolpathAnalyzer Analyzer()
    {
        etp::AnalyzerOptions options;
        options.DistanceTolerance = 1.0e-4;
        options.SamplingDeflection = 1.0e-3;
        options.MinimumEdgeSamples = 11;
        options.ToolpathSamples = 21;
        return etp::ExtrusionToolpathAnalyzer(options);
    }

    gp_Ax3 SectionFrame()
    {
        return gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp::DY(), gp::DX());
    }

    bool SharesSameEdge(const TopoDS_Face& left, const TopoDS_Face& right)
    {
        for (TopExp_Explorer leftEdges(left, TopAbs_EDGE);
             leftEdges.More(); leftEdges.Next())
        {
            for (TopExp_Explorer rightEdges(right, TopAbs_EDGE);
                 rightEdges.More(); rightEdges.Next())
            {
                if (leftEdges.Current().IsSame(rightEdges.Current()))
                    return true;
            }
        }
        return false;
    }

    void TrapezoidProducesVaryingStrip()
    {
        const auto result = Analyzer().CompileFeatureGroup(
            etp::tests::TrapezoidFeatureFace(),
            etp::tests::RectangularTubeSection(),
            SectionFrame());
        Require(result.SweepUnits.size() == 1, "trapezoid must produce one sweep unit");
        const auto& unit = result.SweepUnits.front();
        Require(unit.Topology == etp::SweepTopology::Strip, "trapezoid must be a strip");
        Require(unit.StartSide.has_value() && unit.EndSide.has_value(),
            "trapezoid must retain both access side segments");
        Require(unit.Poses.size() == 21, "trapezoid path sample count mismatch");
        const auto first = unit.Poses.front().ToolDirection;
        const auto last = unit.Poses.back().ToolDirection;
        Require(std::abs(first.Dot(last)) < 0.9999,
            "trapezoid tool direction must vary along the path");
    }

    void TriangleProducesFan()
    {
        const auto result = Analyzer().CompileFeatureGroup(
            etp::tests::TriangleFeatureFace(),
            etp::tests::RectangularTubeSection(),
            SectionFrame());
        Require(result.SweepUnits.size() == 1, "triangle must produce one sweep unit");
        const auto topology = result.SweepUnits.front().Topology;
        Require(topology == etp::SweepTopology::FanFromPoint
                || topology == etp::SweepTopology::FanToPoint,
            "triangle must use a point rail fan");
        Require(result.SweepUnits.front().EntryRail.Kind == etp::RailKind::Point
                || result.SweepUnits.front().ExitRail.Kind == etp::RailKind::Point,
            "triangle must contain one collapsed point rail");
    }

    void NurbsRepresentationIsGeometricNotNominal()
    {
        const auto face = etp::tests::NurbsEncodedTrapezoidFeatureFace();
        bool containsBspline = false;
        for (TopExp_Explorer explorer(face, TopAbs_EDGE); explorer.More(); explorer.Next())
        {
            BRepAdaptor_Curve curve(TopoDS::Edge(explorer.Current()));
            containsBspline = containsBspline || curve.GetType() == GeomAbs_BSplineCurve;
        }
        Require(containsBspline, "test fixture must contain a B-spline edge");

        const auto result = Analyzer().CompileFeatureGroup(
            face, etp::tests::RectangularTubeSection(), SectionFrame());
        Require(result.SweepUnits.size() == 1,
            "geometrically straight NURBS side must still compile");
        const auto& unit = result.SweepUnits.front();
        Require(unit.StartSide.has_value() && unit.EndSide.has_value(),
            "NURBS-encoded feature must retain access sides");
        Require(BRepAdaptor_Curve(unit.StartSide->Edge).GetType() == GeomAbs_BSplineCurve
                || BRepAdaptor_Curve(unit.EndSide->Edge).GetType() == GeomAbs_BSplineCurve,
            "one recognized access side must keep its B-spline OCC edge");
    }

    void WholeTubeRecognitionFindsSectionAndNoCut()
    {
        const auto result = Analyzer().Analyze(etp::tests::RectangularTubeSolid());
        Require(result.SectionWires.size() == 2,
            "rectangular tube must recover outer and inner section loops");
        Require(std::abs(result.SectionFrame.Direction().Dot(gp::DY())) > 0.999,
            "rectangular tube extrusion axis must be Y");
        Require(result.Features.size() == 2,
            "tube end caps are machining features; only longitudinal skins are removed");
    }

    void PipelineStagesAreComposableAndObservable()
    {
        etp::AnalysisJob job;
        job.Id = "scaled-tube";
        job.SourceShape = etp::tests::RotatedRectangularTubeSolid();
        job.Options.DistanceTolerance = 1.0e-4;
        job.Options.SamplingDeflection = 1.0e-3;
        job.Options.MinimumEdgeSamples = 11;
        job.Options.ToolpathSamples = 21;
        job.Options.InputLengthScale = 2.0;

        auto pipe = etp::MakeDefaultPipe();
        Require(pipe.Works().size() == 7, "default pipe must contain seven Works");
        Require(pipe.Works()[0]->Name() == "preprocess", "first Work must preprocess");
        Require(pipe.Works()[1]->Name() == "normalize", "second Work must normalize");
        Require(pipe.Works()[2]->Name() == "section-profile",
            "third Work must recover the section profile");
        Require(pipe.Works()[4]->Name() == "feature-grouping",
            "OCCT Sewing feature grouping must follow face classification");
        Require(pipe.Works()[5]->Name() == "sweep-face-decompose",
            "semantic Face decomposition must run inside sewn feature groups");

        const auto result = pipe.Run(job);
        Require(job.Reports.size() == pipe.Works().size(),
            "each executed Work must publish a report");
        for (const auto& report : job.Reports)
            Require(report.Succeeded, "all default Works must succeed for a clean tube");
        Require(!job.AxisParallelFaces.empty(), "section Work must retain axis-parallel faces");
        Require(job.SectionBreakPoints.size() == 8,
            "rectangular tube section must expose eight distinct breakpoints");
        Require(!job.FacePatches.empty(), "classification Work must publish face patches");
        Require(std::abs((result.AxialLast - result.AxialFirst) - 160.0) < 1.0e-6,
            "normalization Work must apply the requested length scale");
        gp_Vec transformedSourceAxis(0.0, 0.0, 1.0);
        transformedSourceAxis.Transform(job.SourceToNormalized);
        Require(std::abs(gp_Dir(transformedSourceAxis).Dot(gp::DY())) > 0.999,
            "normalization Work must rotate the recognized source axis onto Y");
        Require(result.Id == "scaled-tube", "job identity must flow through the Pipe");
    }

    void SectionBreakPointSplitsWideFace()
    {
        etp::AnalysisJob job;
        job.WorkingShape = etp::tests::WideTopFace();
        job.ExtrusionAxis = gp::DY();
        job.SectionFrame = SectionFrame();
        job.SectionWires = { etp::tests::SegmentedOuterSection() };
        job.Options.DistanceTolerance = 1.0e-4;
        job.Options.SamplingDeflection = 1.0e-3;
        job.Options.MinimumEdgeSamples = 11;
        job.Options.MaterializeFaceSplits = true;

        // The test supplies the recovered profile directly and exercises the
        // next Work's explicit breakpoint contract.
        job.SectionBreakPoints = {
            gp_Pnt2d(-20.0, 15.0),
            gp_Pnt2d(20.0, 15.0),
            gp_Pnt2d(20.0, -15.0),
            gp_Pnt2d(0.0, -15.0),
            gp_Pnt2d(-20.0, -15.0)
        };

        etp::FaceSplitClassifyWork splitAndClassify;
        splitAndClassify.Execute(job);
        Require(job.FacePatches.size() == 2,
            "one section breakpoint inside a wide rail must create two face patches");
        for (const auto& patch : job.FacePatches)
        {
            Require(patch.MaterializedSplit, "wide-face split must be materialized in OCC topology");
            Require(patch.Role == etp::FaceRole::Contour,
                "both split patches must remain contour faces");
        }
    }

    void CompositeStadiumSideIsMaterializedAsFourFaces()
    {
        etp::AnalysisJob job;
        etp::FacePatch patch;
        patch.Id = 0;
        patch.SourceFaceId = 17;
        patch.Face = etp::tests::CompositeStadiumSideFace();
        patch.Role = etp::FaceRole::Machining;
        job.FacePatches.push_back(patch);
        job.Options.DistanceTolerance = 1.0e-5;
        job.Options.MaterializeFaceSplits = true;

        // Sewing establishes the feature group first. Semantic splitting then
        // runs group-wide, so all generated ruling Edges stay shared.
        etp::FeatureGroupingWork groupFeatures;
        groupFeatures.Execute(job);
        Require(job.FeatureGroups.size() == 1,
            "the original stadium Face must form one sewn feature group");

        etp::SweepFaceDecomposeWork decompose;
        decompose.Execute(job);

        Require(job.FacePatches.size() == 4,
            "one composite stadium side Face must become four semantic Faces");
        for (const auto& child : job.FacePatches)
        {
            Require(child.SourceFaceId == 17,
                "semantic children must retain their source Face identity");
            Require(child.MaterializedSplit,
                "stadium children must report a materialized OCC split");

            std::size_t edgeCount = 0;
            for (TopExp_Explorer explorer(child.Face, TopAbs_EDGE);
                 explorer.More(); explorer.Next())
            {
                ++edgeCount;
            }
            Require(edgeCount == 4,
                "each stadium sweep Face must expose four explicit boundary coedges");
        }

        Require(job.FeatureGroups.size() == 1,
            "the four stadium Faces must remain one connected feature group");
        Require(job.FeatureGroups.front().FacePatchIds.size() == 4,
            "the stadium feature group must contain all four semantic Faces");
    }

    void SewingGroupsGeometricallyCoincidentIndependentFaces()
    {
        const auto faces = etp::tests::IndependentAdjacentFaces();
        Require(faces.size() == 2, "adjacent Face fixture must contain two Faces");
        Require(!SharesSameEdge(faces[0], faces[1]),
            "fixture Faces must not share a TopoDS_Edge before Sewing");

        etp::AnalysisJob job;
        job.Options.DistanceTolerance = 1.0e-5;
        for (std::size_t index = 0; index < faces.size(); ++index)
        {
            etp::FacePatch patch;
            patch.Id = index;
            patch.SourceFaceId = 30 + index;
            patch.Face = faces[index];
            patch.Role = etp::FaceRole::Machining;
            job.FacePatches.push_back(std::move(patch));
        }

        etp::FeatureGroupingWork groupFeatures;
        groupFeatures.Execute(job);

        Require(job.FeatureGroups.size() == 1,
            "Sewing must place coincident adjacent Faces in one feature group");
        Require(job.FeatureGroups.front().FacePatchIds.size() == 2,
            "the sewn feature group must retain both source Faces");
        Require(SharesSameEdge(job.FacePatches[0].Face, job.FacePatches[1].Face),
            "Sewing replacements must share the same TopoDS_Edge");
    }

    void OffsetGridRemainsSevenFacesAfterGroupWideDecomposition()
    {
        const auto faces = etp::tests::OffsetRectangularFaceGrid();
        Require(faces.size() == 7, "offset grid fixture must contain seven Faces");

        etp::AnalysisJob job;
        job.Options.DistanceTolerance = 1.0e-5;
        job.Options.MaterializeFaceSplits = true;
        for (std::size_t index = 0; index < faces.size(); ++index)
        {
            etp::FacePatch patch;
            patch.Id = index;
            patch.SourceFaceId = 50 + index;
            patch.Face = faces[index];
            patch.Role = etp::FaceRole::Machining;
            job.FacePatches.push_back(std::move(patch));
        }

        etp::FeatureGroupingWork grouping;
        grouping.Execute(job);
        Require(job.FeatureGroups.size() == 1,
            "all seven T-connected rectangles must form one sewn feature group");
        Require(job.FeatureGroups.front().FacePatchIds.size() == 7,
            "Sewing must retain all seven grid Faces");

        etp::SweepFaceDecomposeWork decomposition;
        decomposition.Execute(job);
        Require(job.FacePatches.size() == 7,
            "T-junction edge cuts must not cause extra Face decomposition");
        Require(job.FeatureGroups.size() == 1
                && job.FeatureGroups.front().FacePatchIds.size() == 7,
            "group-wide decomposition must preserve the one-group, seven-Face result");
    }

    void SemicircleTrajectoryBuildsHalfCylinderToolpath()
    {
        const auto fixture = etp::tests::HalfCylinderFixture();
        etp::Toolpath path;
        const auto success = Analyzer().TryBuildToolpath(
            fixture.Face,
            fixture.TransverseTrajectory,
            etp::tests::SolidRectangularSection(),
            SectionFrame(),
            path);

        Require(success, "a semicircle must span the half-cylinder ruling family");
        Require(path.Poses.size() == 21, "half-cylinder toolpath sample count mismatch");
        for (const auto& pose : path.Poses)
        {
            Require(std::abs(pose.ToolDirection.Dot(gp::DY())) > 0.999,
                "half-cylinder IJK must remain parallel to the cylinder axis");
            Require(std::abs(pose.MaterialDepth - 20.0) < 1.0e-4,
                "half-cylinder material depth must equal its axial length");
        }
    }

    void AxialCylinderTrajectoryIsRejectedAndClearsOutput()
    {
        const auto fixture = etp::tests::HalfCylinderFixture();
        etp::Toolpath path;
        path.Id = "stale";
        path.Poses.push_back({});

        const auto success = Analyzer().TryBuildToolpath(
            fixture.Face,
            fixture.RulingTrajectory,
            etp::tests::SolidRectangularSection(),
            SectionFrame(),
            path);

        Require(!success,
            "an axial trajectory follows one cylinder ruling and must be rejected");
        Require(path.Id.empty() && path.Poses.empty(),
            "false must not expose a stale or partially generated toolpath");
    }

    void NoSideCylinderWithTwoPinchesBuildsToolpath()
    {
        const auto fixture = etp::tests::PinchedCylinderFixture();
        const auto result = Analyzer().CompileFeatureGroup(
            fixture.Face,
            etp::tests::SolidRectangularSection(),
            SectionFrame());

        Require(result.SweepUnits.size() == 1,
            "a no-side cylindrical Face must be compiled from a transverse rail");
        const auto& unit = result.SweepUnits.front();
        Require(unit.Topology == etp::SweepTopology::DoublePinched,
            "two zero-depth rail intersections must form DoublePinched topology");
        Require(!unit.StartSide.has_value() && !unit.EndSide.has_value(),
            "a double-pinched cylinder must not invent access side segments");
        Require(unit.Poses.front().MaterialDepth <= 1.0e-4
                && unit.Poses.back().MaterialDepth <= 1.0e-4,
            "both pinched endpoints must have zero material depth");
        Require(unit.Poses[unit.Poses.size() / 2].MaterialDepth > 5.0,
            "the middle ruling must retain the non-zero cylinder depth");
    }
}

int main()
{
    try
    {
        TrapezoidProducesVaryingStrip();
        TriangleProducesFan();
        NurbsRepresentationIsGeometricNotNominal();
        WholeTubeRecognitionFindsSectionAndNoCut();
        PipelineStagesAreComposableAndObservable();
        SectionBreakPointSplitsWideFace();
        SewingGroupsGeometricallyCoincidentIndependentFaces();
        OffsetGridRemainsSevenFacesAfterGroupWideDecomposition();
        CompositeStadiumSideIsMaterializedAsFourFaces();
        SemicircleTrajectoryBuildsHalfCylinderToolpath();
        AxialCylinderTrajectoryIsRejectedAndClearsOutput();
        NoSideCylinderWithTwoPinchesBuildsToolpath();
        std::cout << "All extrusion toolpath tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
