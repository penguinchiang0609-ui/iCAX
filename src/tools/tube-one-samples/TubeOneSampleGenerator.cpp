#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    TopoDS_Wire MakeRectangleWire(
        double minX,
        double minY,
        double maxX,
        double maxY,
        double z,
        bool counterClockwise)
    {
        BRepBuilderAPI_MakePolygon polygon;
        const auto add = [&polygon, z](double x, double y) {
            polygon.Add(gp_Pnt(x, y, z));
        };
        if (counterClockwise)
        {
            add(minX, minY);
            add(maxX, minY);
            add(maxX, maxY);
            add(minX, maxY);
        }
        else
        {
            add(minX, minY);
            add(minX, maxY);
            add(maxX, maxY);
            add(maxX, minY);
        }
        polygon.Close();
        if (!polygon.IsDone())
        {
            throw std::runtime_error("Failed to build rectangle wire");
        }
        return polygon.Wire();
    }

    TopoDS_Shape MakeRoundTube(double length = 300.0)
    {
        const auto outer = BRepPrimAPI_MakeCylinder(50.0, length).Shape();
        const auto inner = BRepPrimAPI_MakeCylinder(42.0, length).Shape();
        BRepAlgoAPI_Cut cut(outer, inner);
        if (!cut.IsDone())
        {
            throw std::runtime_error("Failed to build round tube");
        }
        return cut.Shape();
    }

    TopoDS_Shape MakeDoubleCavityTube(double length = 300.0)
    {
        auto face = BRepBuilderAPI_MakeFace(
            MakeRectangleWire(0.0, 0.0, 120.0, 70.0, 0.0, true));
        face.Add(MakeRectangleWire(8.0, 10.0, 56.0, 60.0, 0.0, false));
        face.Add(MakeRectangleWire(64.0, 10.0, 112.0, 60.0, 0.0, false));
        if (!face.IsDone())
        {
            throw std::runtime_error("Failed to build double-cavity section");
        }
        BRepPrimAPI_MakePrism prism(face.Face(), gp_Vec(0.0, 0.0, length));
        if (!prism.IsDone())
        {
            throw std::runtime_error("Failed to extrude double-cavity tube");
        }
        return prism.Shape();
    }

    TopoDS_Shape Cut(const TopoDS_Shape& base, const TopoDS_Shape& cutter)
    {
        BRepAlgoAPI_Cut cut(base, cutter);
        if (!cut.IsDone())
        {
            throw std::runtime_error("Boolean cut failed");
        }
        return cut.Shape();
    }

    TopoDS_Shape Fuse(const TopoDS_Shape& first, const TopoDS_Shape& second)
    {
        BRepAlgoAPI_Fuse fuse(first, second);
        if (!fuse.IsDone())
        {
            throw std::runtime_error("Boolean fuse failed");
        }
        return fuse.Shape();
    }

    TopoDS_Shape MakeRoundTubeWithThroughHole()
    {
        const auto cutter = BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(0.0, 70.0, 150.0), gp_Dir(0.0, -1.0, 0.0)),
            8.0,
            140.0).Shape();
        return Cut(MakeRoundTube(), cutter);
    }

    TopoDS_Shape MakeRoundTubeWithObliqueHole()
    {
        const auto cutter = BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(0.0, 70.0, 145.0), gp_Dir(0.0, -1.0, 0.25)),
            7.0,
            145.0).Shape();
        return Cut(MakeRoundTube(), cutter);
    }

    TopoDS_Shape MakeDoubleCavityTubeWithBlindHole()
    {
        const auto cutter = BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(30.0, 80.0, 150.0), gp_Dir(0.0, -1.0, 0.0)),
            6.0,
            15.0).Shape();
        return Cut(MakeDoubleCavityTube(), cutter);
    }

    TopoDS_Shape MakeOpenedChamberWithInternalWallHole()
    {
        const auto opening = BRepPrimAPI_MakeBox(
            gp_Pnt(12.0, 58.0, 70.0),
            40.0,
            20.0,
            160.0).Shape();
        const auto opened = Cut(MakeDoubleCavityTube(), opening);
        const auto internalWallHole = BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(52.0, 35.0, 150.0), gp_Dir(1.0, 0.0, 0.0)),
            5.0,
            20.0).Shape();
        return Cut(opened, internalWallHole);
    }

    TopoDS_Shape MakeRoundTubeWithSteppedUVEnd()
    {
        const auto positiveXRemoval = BRepPrimAPI_MakeBox(
            gp_Pnt(0.0, 0.0, 260.0),
            60.0,
            60.0,
            40.0).Shape();
        const auto negativeXRemoval = BRepPrimAPI_MakeBox(
            gp_Pnt(-60.0, 0.0, 240.0),
            60.0,
            60.0,
            60.0).Shape();
        return Cut(
            MakeRoundTube(),
            Fuse(positiveXRemoval, negativeXRemoval));
    }

    TopoDS_Shape MakeDoubleCavityTubeWithBeveledHole()
    {
        const auto axis = gp_Ax2(
            gp_Pnt(30.0, 80.0, 150.0),
            gp_Dir(0.0, -1.0, 0.0));
        const auto nominal = BRepPrimAPI_MakeCylinder(axis, 4.0, 30.0).Shape();
        const auto bevel = BRepPrimAPI_MakeCone(axis, 9.0, 4.0, 16.0).Shape();
        return Cut(MakeDoubleCavityTube(), Fuse(nominal, bevel));
    }

    TopoDS_Shape MakeDoubleCavityTubeWithMiteredEnds()
    {
        const auto startPlane = gp_Pln(
            gp_Pnt(0.0, 0.0, 15.0),
            gp_Dir(0.22, 0.0, 1.0));
        const auto startFace = BRepBuilderAPI_MakeFace(startPlane).Face();
        const auto startRemoval = BRepPrimAPI_MakeHalfSpace(
            startFace,
            gp_Pnt(0.0, 0.0, -100.0)).Solid();
        const auto startCut = Cut(MakeDoubleCavityTube(), startRemoval);

        const auto endPlane = gp_Pln(
            gp_Pnt(0.0, 0.0, 285.0),
            gp_Dir(-0.18, 0.0, 1.0));
        const auto endFace = BRepBuilderAPI_MakeFace(endPlane).Face();
        const auto endRemoval = BRepPrimAPI_MakeHalfSpace(
            endFace,
            gp_Pnt(0.0, 0.0, 400.0)).Solid();
        return Cut(startCut, endRemoval);
    }

    void WriteSTEP(
        const std::filesystem::path& outputDirectory,
        const std::string& fileName,
        const TopoDS_Shape& shape)
    {
        if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())
        {
            throw std::runtime_error("Invalid sample shape: " + fileName);
        }
        STEPControl_Writer writer;
        if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone)
        {
            throw std::runtime_error("STEP transfer failed: " + fileName);
        }
        const auto path = outputDirectory / fileName;
        if (writer.Write(path.string().c_str()) != IFSelect_RetDone)
        {
            throw std::runtime_error("STEP write failed: " + path.string());
        }
        std::cout << path.string() << '\n';
    }
}

int main(int argc, char** argv)
{
    try
    {
        const auto outputDirectory = argc > 1
            ? std::filesystem::absolute(argv[1])
            : std::filesystem::current_path() / "tube-one-samples";
        std::filesystem::create_directories(outputDirectory);

        const std::vector<std::pair<std::string, TopoDS_Shape>> samples = {
            { "01_round_tube_plain.step", MakeRoundTube() },
            { "02_double_cavity_rectangular_tube.step", MakeDoubleCavityTube() },
            { "03_round_tube_through_hole.step", MakeRoundTubeWithThroughHole() },
            { "04_round_tube_oblique_hole.step", MakeRoundTubeWithObliqueHole() },
            { "05_double_cavity_blind_hole.step", MakeDoubleCavityTubeWithBlindHole() },
            { "06_opened_chamber_internal_wall_hole.step", MakeOpenedChamberWithInternalWallHole() },
            { "07_round_tube_stepped_uv_end.step", MakeRoundTubeWithSteppedUVEnd() },
            { "08_double_cavity_beveled_hole.step", MakeDoubleCavityTubeWithBeveledHole() },
            { "09_double_cavity_mitered_ends.step", MakeDoubleCavityTubeWithMiteredEnds() },
        };
        for (const auto& [fileName, shape] : samples)
        {
            WriteSTEP(outputDirectory, fileName, shape);
        }
        std::cout << "Generated " << samples.size() << " TubeOne STEP samples.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
