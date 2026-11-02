// Compare the shared neutral evaluator with the production side-drawing
// kernel, using independent directed Boolean differences on actual solids.
#define main ExistingCadBevelAcceptanceMain
#include "CadBevelAcceptanceBridge.cpp"
#undef main
#include "OpenCascadeResourceImport/OpenCascadeTubeWrapped.h"
#include "TubeDesigner/TubeSideSketchPlanarCurves.h"
#include <etp/Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <TopoDS_Compound.hxx>

namespace
{
    double DirectedDifference(const TopoDS_Shape& Left, const TopoDS_Shape& Right)
    {
        BRepAlgoAPI_Cut Cut;
        NCollection_List<TopoDS_Shape> Arguments, Tools;
        Arguments.Append(BRepBuilderAPI_Copy(Left, true, false).Shape());
        Tools.Append(BRepBuilderAPI_Copy(Right, true, false).Shape());
        Cut.SetArguments(Arguments); Cut.SetTools(Tools);
        Cut.SetNonDestructive(true); Cut.SetToFillHistory(false); Cut.Build();
        Require(Cut.IsDone() && BRepCheck_Analyzer(Cut.Shape()).IsValid(), "Independent comparison failed");
        return Volume(Cut.Shape());
    }

    void WrappedCheck(const TopoDS_Shape& Stock, double U, double Radius, const char* Name,
        double EllipseRadiusY = 0.0, double Station = 25.0)
    {
        using iCAX::Data::ObjectMap; using iCAX::Data::VariantArray;
        const auto Reference = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(Stock);
        Require(Reference.bOK, Reference.Diagnostic);
        ObjectMap Entity{{"id", std::string(Name)}, {"kind", std::string("circle")},
            {"cx", U}, {"cy", Station}, {"radius", Radius}, {"closed", true}};
        if (EllipseRadiusY > 0.0) {
            Entity["kind"] = std::string("ellipse"); Entity.erase("radius");
            Entity["radiusX"] = Radius; Entity["radiusY"] = EllipseRadiusY; Entity["rotation"] = 0.0;
        }
        const ObjectMap Sketch{{"coordinateSpace", std::string("arc-length-axial")},
            {"length", Reference.Length}, {"faceHeight", Reference.Perimeter}, {"perimeter", Reference.Perimeter},
            {"entities", VariantArray{Entity}}};
        const auto Expected = iCAX::TubeDesigner::ApplyTubeSideSketchCurves(Stock, Sketch);
        Require(Expected.bOK, std::string(Name) + ": original kernel: " + Expected.Diagnostic);
        etp::AnalyzerOptions Options;
        Options.MaterializeFaceSplits = false; Options.DistanceTolerance = 1.0e-4;
        Options.AngularToleranceRadians = 1.0e-3;
        const auto Tube = etp::ExtrusionToolpathAnalyzer(Options).Analyze(Stock);
        const auto Regions = iCAX::TubeDesigner::BuildTubeSideSketchPlanarRegions(
            Sketch, Reference.Length, Reference.Perimeter, 1.0e-7, false);
        Require(Regions.bOK, Regions.Diagnostic);
        TopoDS_Compound UV; BRep_Builder Builder; Builder.MakeCompound(UV);
        for (const auto& Face : Regions.Faces) Builder.Add(UV, Face);
        std::vector<TopoDS_Wire> SectionWires;
        for (const auto& Wire : Tube.SectionWires)
            SectionWires.push_back(TopoDS::Wire(BRepBuilderAPI_Transform(
                Wire, Tube.NormalizedToSource, false).Shape()));
        const iCAX::CAD::STubeWrappedHostGeometry Host{Stock,
            Tube.SectionFrame.Transformed(Tube.NormalizedToSource),
            Tube.AxialFirst, Tube.AxialLast, SectionWires};
        const auto Wrapped = iCAX::CAD::EvaluateTubeWrappedRegion(Host, UV);
        Require(Wrapped.ToolPatchCount == Expected.ToolPatchCount, "Cutter patch count changed");
        Require(Wrapped.Approximate == Expected.Approximate, "Approximation reporting changed");
        BRepAlgoAPI_Cut Evaluate;
        NCollection_List<TopoDS_Shape> Arguments, Tools;
        Arguments.Append(Stock); Tools.Append(Wrapped.Shape);
        Evaluate.SetArguments(Arguments); Evaluate.SetTools(Tools);
        Evaluate.SetNonDestructive(true); Evaluate.SetFuzzyValue(1.0e-7); Evaluate.Build();
        Require(Evaluate.IsDone(), "Shared cutter application failed");
        const auto Actual = Evaluate.Shape();
        Require(BRepCheck_Analyzer(Actual).IsValid(), "Shared wrapped result is invalid");
        const auto Neutral = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Actual, "", "", 0.025);
        const auto Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(Neutral);
        if (!Rebuilt.bOK || Rebuilt.Shape.IsNull() || !BRepCheck_Analyzer(Rebuilt.Shape).IsValid()) {
            BRepBuilderAPI_NurbsConvert Nurbs(Actual, true);
            std::cerr << "NURBS BRep conversion valid="
                << (Nurbs.IsDone() && BRepCheck_Analyzer(Nurbs.Shape()).IsValid()) << "\n";
            if (Nurbs.IsDone()) {
                const BRepCheck_Analyzer Check(Nurbs.Shape());
                int FaceIndex = 0;
                for (TopExp_Explorer Face(Nurbs.Shape(), TopAbs_FACE); Face.More(); Face.Next()) {
                    ++FaceIndex;
                    if (Check.IsValid(Face.Current())) continue;
                    for (const auto Type : {TopAbs_WIRE, TopAbs_EDGE, TopAbs_VERTEX}) {
                        int Index = 0;
                        for (TopExp_Explorer Part(Face.Current(), Type); Part.More(); Part.Next()) {
                            ++Index;
                            const auto Result = Check.Result(Part.Current());
                            if (Result.IsNull() || !Result->IsStatusOnShape(Face.Current())) continue;
                            for (const auto Status : Result->StatusOnShape(Face.Current()))
                                if (Status != BRepCheck_NoError)
                                    std::cerr << "NURBS context face=" << FaceIndex << " type=" << Type
                                        << " subshape=" << Index << " status=" << int(Status) << "\n";
                        }
                    }
                }
            }
            DiagnoseRoundTrip(Actual, Rebuilt);
        }
        Require(Rebuilt.bOK && !Rebuilt.Shape.IsNull() && BRepCheck_Analyzer(Rebuilt.Shape).IsValid(),
            std::string(Name) + ": wrapped result neutral BRep roundtrip is invalid");
        const double A = DirectedDifference(Actual, Expected.Shape);
        const double B = DirectedDifference(Expected.Shape, Actual);
        const auto Tolerance = std::max(1.0e-9, Volume(Stock) * 1.0e-8);
        const double TransportA = DirectedDifference(Actual, Rebuilt.Shape);
        const double TransportB = DirectedDifference(Rebuilt.Shape, Actual);
        Require(TransportA + TransportB <= Tolerance,
            std::string(Name) + ": neutral BRep roundtrip geometry changed");
        std::cout << "CHECK " << Name << " volumes=" << Volume(Expected.Shape) << "," << Volume(Actual)
            << " difference=" << A << "," << B << " tolerance=" << Tolerance << "\n";
        Require(A + B <= Tolerance, std::string(Name) + ": directed geometry difference");
        Require(Volume(Actual) < Volume(Stock), "The drawing must remove real material");
        std::cout << "PASS " << Name << " patches=" << Wrapped.ToolPatchCount
            << " difference=" << A << "," << B
            << " transport=" << TransportA << "," << TransportB << "\n";
        if (std::string(Name) == "flat-wall-circle" || std::string(Name) == "cylindrical-wall-circle")
        {
            const auto Boundaries = iCAX::CAD::BindTubeWrappedBevel(Host, UV, {}, Actual, Wrapped.Shape);
            Require(!Boundaries.empty(), "The exact UV loop must bind actual cut-wall edges");
            for (const std::string Side : {"outer", "inner", "both"})
            {
                auto BeveledSketch = Sketch;
                auto Entity = Sketch.at("entities").To<VariantArray>().front().To<ObjectMap>();
                Entity["bevel"] = ObjectMap{{"enabled", true}, {"side", Side},
                    {"angleDegrees", 45.0}, {"rootFace", 0.5}};
                BeveledSketch["entities"] = VariantArray{Entity};
                iCAX::TubeDesigner::STubeSideSketchOptions BevelOptions;
                BevelOptions.BevelService = BevelService.get();
                const auto ExpectedBevel = iCAX::TubeDesigner::ApplyTubeSideSketchCurves(Stock, BeveledSketch, BevelOptions);
                Require(ExpectedBevel.bOK, std::string(Name) + " expected bevel: " + ExpectedBevel.Diagnostic);
                std::vector<iCAX::CAD::SCadBevelOperation> Operations;
                for (const auto& Boundary : Boundaries)
                    Operations.push_back({Boundary.EdgeID, Boundary.ReferenceFaceID, Side, 45.0, 0.5});
                const auto ActualBevel = BevelService->ApplyNative(Actual, Operations);
                Require(ActualBevel.bOK, std::string(Name) + " exact UV bevel: " + ActualBevel.Diagnostic);
                const double BevelA = DirectedDifference(ActualBevel.Shape, ExpectedBevel.Shape);
                const double BevelB = DirectedDifference(ExpectedBevel.Shape, ActualBevel.Shape);
                Require(BevelA + BevelB <= Tolerance, std::string(Name) + " " + Side + ": bevel geometry difference");
                Require(Volume(ActualBevel.Shape) < Volume(Actual) - 1.0e-5, "UV bevel must remove actual material");
                std::cout << "PASS " << Name << " " << Side << " exact UV bevel difference=" << BevelA << "," << BevelB << "\n";
            }
        }
    }

    void RoundedSeamEllipseCheck()
    {
        const auto RoundedBox = [](const TopoDS_Shape& Box, double Radius) {
            BRepFilletAPI_MakeFillet Fillet(Box);
            for (TopExp_Explorer Edge(Box, TopAbs_EDGE); Edge.More(); Edge.Next()) {
                const auto E = TopoDS::Edge(Edge.Current());
                const BRepAdaptor_Curve Curve(E);
                if (Curve.GetType() == GeomAbs_Line && std::abs(Curve.Line().Direction().X()) > .99)
                    Fillet.Add(Radius, E);
            }
            Fillet.Build(); Require(Fillet.IsDone(), "R3 browser stock fixture failed");
            return Fillet.Shape();
        };
        const auto Stock = BRepAlgoAPI_Cut(
            RoundedBox(BRepPrimAPI_MakeBox(gp_Pnt(0, -30, -20), 1000, 60, 40).Shape(), 3),
            RoundedBox(BRepPrimAPI_MakeBox(gp_Pnt(-1, -28, -18), 1002, 56, 36).Shape(), 1)).Shape();
        const auto Reference = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(Stock);
        Require(Reference.bOK, Reference.Diagnostic);
        WrappedCheck(Stock, Reference.Perimeter - 3, 12, "rounded-R3-periodic-seam-ellipse", 18, 350);
    }
}

int main(int argc, char** argv)
{
    try
    {
        iCAX::Services::CServiceProvider Services;
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(Services);
        BevelService = Services.Resolve<iCAX::CAD::ICadBevelService>();
        Require(BevelService != nullptr, "Registered bevel service is required");
        if (argc > 1 && std::string(argv[1]) == "--rounded-seam-ellipse") {
            RoundedSeamEllipseCheck(); return 0;
        }
        const auto Sharp = RoundedTube(0);
        WrappedCheck(Sharp, 15, 5, "flat-wall-circle");
        const auto SharpReference = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(Sharp);
        WrappedCheck(Sharp, SharpReference.Perimeter + 2, 6, "periodic-seam-circle");
        WrappedCheck(Sharp, SharpReference.Perimeter * 5 + 2, 6, "complete-source-circle-five-laps-away");
        WrappedCheck(Sharp, 40, 12, "adjacent-flat-walls");
        const auto Rounded = RoundedTube(5);
        WrappedCheck(Rounded, CornerMidpoint(Rounded), 4, "rounded-corner-and-flat-wall");
        const auto Small = RoundedTube(1.5);
        WrappedCheck(Small, CornerMidpoint(Small), 2, "radius-smaller-than-wall");
        const auto Round = BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(20, 50).Shape(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp_Dir(0, 0, 1)), 18, 52).Shape()).Shape();
        WrappedCheck(Round, 10, 5, "cylindrical-wall-circle");
        RoundedSeamEllipseCheck();
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::cerr << "FAIL " << Error.what() << "\n";
        return 1;
    }
}
