#include "OpenCascadeResourceImport/OpenCascadeShapeBounds.h"
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <IGESControl_Reader.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace
{
    std::array<double, 6> Extents(Bnd_Box box)
    {
        box.SetGap(0);
        std::array<double, 6> result;
        box.Get(result[0], result[1], result[2], result[3], result[4], result[5]);
        return result;
    }
    void Check(const TopoDS_Shape& shape, const char* name)
    {
        Bnd_Box serial;
        BRepBndLib::AddOptimal(shape, serial, false, false);
        const auto expected = Extents(serial);
        double worst = 0;
        const auto started = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            const auto actual = Extents(iCAX::OpenCascade::ExactShapeBounds(shape, 8));
            for (std::size_t i = 0; i < expected.size(); ++i)
                worst = std::max(worst, std::abs(expected[i] - actual[i]));
        }
        if (worst > 1e-7) throw std::runtime_error(std::string(name) + ": exact bounds changed");
        std::cout << "PASS " << name << " maxErrorMm=" << worst << " parallelTwoRunsMs="
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << '\n';
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        Check(BRepPrimAPI_MakeBox(10, 20, 30).Shape(), "small-solid");
        BRep_Builder builder;
        TopoDS_Compound mixed, surfaces;
        builder.MakeCompound(mixed);
        builder.MakeCompound(surfaces);
        for (int i = 0; i < 96; ++i)
        {
            gp_Trsf pose;
            pose.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 2, 3)), i * .13);
            pose.SetTranslationPart(gp_Vec(i * 11, i * 7, -i * 5));
            const auto part = BRepBuilderAPI_Transform(BRepPrimAPI_MakeBox(10, 20, 30), pose).Shape();
            builder.Add(mixed, part);
            builder.Add(mixed, BRepPrimAPI_MakeSphere(gp_Pnt(i * 3, -40, 50), 4).Shape());
            builder.Add(mixed, BRepPrimAPI_MakeCylinder(6, 90).Shape());
            for (TopExp_Explorer face(part, TopAbs_FACE); face.More(); face.Next())
                builder.Add(surfaces, face.Current());
        }
        builder.Add(mixed, BRepBuilderAPI_MakeEdge(gp_Pnt(-300, 800, -900), gp_Pnt(-200, 900, -800)));
        builder.Add(mixed, BRepBuilderAPI_MakeVertex(gp_Pnt(500, -800, 1300)));
        Check(mixed, "located-solids-free-edge-free-vertex");
        Check(surfaces, "surface-collection");
        for (int i = 1; i < argc; ++i)
        {
            const auto utf8 = std::filesystem::path(argv[i]).u8string();
            const std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
            IGESControl_Reader reader;
            if (reader.ReadFile(path.c_str()) != IFSelect_RetDone || reader.TransferRoots() == 0)
                throw std::runtime_error("Cannot read real IGES fixture");
            Check(reader.OneShape(), path.c_str());
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
