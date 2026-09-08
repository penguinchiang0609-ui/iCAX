#include "pch.h"
#include <TubeDesigner/MachiningToolpaths.h>
#include <TubeDesigner/ComponentModelLibrary.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include <IGESControl_Writer.hxx>
#include <gp_Pnt.hxx>
#include <chrono>
#include <filesystem>

namespace
{
    TopoDS_Shape MachiningTube(double x)
    {
        const auto outer = BRepPrimAPI_MakeBox(gp_Pnt(x, 30, 40), 200, 30, 20).Shape();
        const auto inner = BRepPrimAPI_MakeBox(gp_Pnt(x - 1, 32, 42), 202, 26, 16).Shape();
        return BRepAlgoAPI_Cut(outer, inner).Shape();
    }
    std::shared_ptr<iCAX::CAM::IBRepToCamPathService> Parser(iCAX::Services::CServiceProvider& services)
    {
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
        return services.Resolve<iCAX::CAM::IBRepToCamPathService>();
    }
}

TEST(BRepToCamPath, ServiceProducesOwnedWorldCoordinatePathsAndRejectsInvalidInput)
{
    iCAX::Services::CServiceProvider services;
    const auto parser = Parser(services);
    ASSERT_NE(parser, nullptr);
    auto brep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(MachiningTube(125), "tube", "source");
    const auto result = parser->Analyze(brep);
    std::string diagnostics;
    for (const auto& item : result.Diagnostics) diagnostics += item.Code + ": " + item.Message + "\n";
    ASSERT_EQ(result.Paths.size(), 2u) << diagnostics;
    EXPECT_TRUE(result.Complete) << diagnostics;
    // Releasing/replacing the input does not invalidate any returned CamPath.
    brep = {};
    for (const auto& path : result.Paths)
    {
        ASSERT_EQ(path.Curve.Segments.size(), 1u);
        const auto& points = std::get<iCAX::GeometryData::Polyline3>(path.Curve.Segments[0].Curve).Points;
        ASSERT_GE(points.size(), 2u);
        EXPECT_TRUE(path.Poses.empty() || points.size() == path.Poses.size());
        EXPECT_TRUE(path.Curve.Closed);
        double length = 0;
        for (std::size_t i = 1; i < points.size(); ++i)
            length += gp_Pnt(points[i-1].X,points[i-1].Y,points[i-1].Z).Distance(gp_Pnt(points[i].X,points[i].Y,points[i].Z));
        if (path.Curve.Closed) length += gp_Pnt(points.back().X,points.back().Y,points.back().Z).Distance(gp_Pnt(points.front().X,points.front().Y,points.front().Z));
        EXPECT_NEAR(length,100.0,0.001); // outer rim only, not inner rim or longitudinal edges
        for (const auto& point : points)
        {
            EXPECT_GE(point.X, 124.99); EXPECT_LE(point.X, 325.01);
            EXPECT_GE(point.Y, 29.99); EXPECT_LE(point.Y, 60.01);
            EXPECT_GE(point.Z, 39.99); EXPECT_LE(point.Z, 60.01);
            EXPECT_TRUE(std::abs(point.X-125)<0.001 || std::abs(point.X-325)<0.001);
        }
    }
    const auto snapshot = iCAX::TubeDesigner::SerializeMachiningToolpaths(result);
    EXPECT_TRUE(snapshot.at("independent").To<bool>());
    EXPECT_EQ(snapshot.at("paths").To<iCAX::Data::VariantArray>().size(), result.Paths.size());
    EXPECT_THROW(parser->Analyze({}), std::invalid_argument);
}

TEST(BRepToCamPath, RoundTubeExcludesSeamsAndInnerRims)
{
    const gp_Ax2 axis(gp_Pnt(100,30,40),gp_Dir(1,0,0));
    const auto shape = BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(axis,20,200).Shape(),
        BRepPrimAPI_MakeCylinder(axis,18,200).Shape()).Shape();
    iCAX::Services::CServiceProvider services;
    const auto result = Parser(services)->Analyze(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"round","source"));
    std::string diagnostics;
    for (const auto& d : result.Diagnostics) diagnostics += d.Code + ": " + d.Message + "\n";
    ASSERT_EQ(result.Paths.size(),2u) << diagnostics;
    EXPECT_TRUE(result.Complete) << diagnostics;
    for (const auto& path : result.Paths)
    {
        EXPECT_TRUE(path.Curve.Closed);
        for (const auto& p : std::get<iCAX::GeometryData::Polyline3>(path.Curve.Segments[0].Curve).Points)
        {
            EXPECT_NEAR(std::hypot(p.Y-30,p.Z-40),20,0.001);
            EXPECT_TRUE(std::abs(p.X-100)<0.001 || std::abs(p.X-300)<0.001);
        }
    }
}

TEST(BRepToCamPath, RotatedTubeWithThroughHoleKeepsTwoEndsAndTwoHoleContours)
{
    auto shape = BRepAlgoAPI_Cut(MachiningTube(125),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(210,45,35),gp_Dir(0,0,1)),5,30).Shape()).Shape();
    gp_Trsf rotation;
    rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.73);
    shape = BRepBuilderAPI_Transform(shape,rotation,true).Shape();
    iCAX::Services::CServiceProvider services;
    const auto result = Parser(services)->Analyze(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"drilled","source"));
    std::string diagnostics;
    for (const auto& d : result.Diagnostics) diagnostics += d.Code + ": " + d.Message + "\n";
    for (const auto& path : result.Paths)
    {
        const auto& points = std::get<iCAX::GeometryData::Polyline3>(path.Curve.Segments[0].Curve).Points;
        const auto& p = points.front();
        const auto q=gp_Pnt(p.X,p.Y,p.Z).Transformed(rotation.Inverted());
        diagnostics += path.SourceFeature + " closed=" + std::to_string(path.Curve.Closed) + " start="
            +std::to_string(q.X())+","+std::to_string(q.Y())+","+std::to_string(q.Z())+"\n";
    }
    ASSERT_EQ(result.Paths.size(),4u) << diagnostics;
    EXPECT_TRUE(result.Complete) << diagnostics;
    int ends=0, holes=0;
    for (const auto& path : result.Paths)
    {
        EXPECT_TRUE(path.Curve.Closed);
        const auto& points = std::get<iCAX::GeometryData::Polyline3>(path.Curve.Segments[0].Curve).Points;
        const auto& p = points.front();
        const auto point = gp_Pnt(p.X,p.Y,p.Z).Transformed(rotation.Inverted());
        if (std::abs(point.X()-125)<0.001 || std::abs(point.X()-325)<0.001) ++ends;
        else
        {
            ++holes;
            for (const auto& sample : points)
            {
                const auto q=gp_Pnt(sample.X,sample.Y,sample.Z).Transformed(rotation.Inverted());
                EXPECT_NEAR(std::hypot(q.X()-210,q.Y()-45),5,0.001);
                EXPECT_TRUE(std::abs(q.Z()-40)<0.001 || std::abs(q.Z()-60)<0.001);
            }
        }
    }
    EXPECT_EQ(ends,2); EXPECT_EQ(holes,2);
}

TEST(BRepToCamPath, StepAndIgesAssemblyImportsKeepPlacements)
{
    BRep_Builder builder;
    TopoDS_Compound assembly;
    builder.MakeCompound(assembly);
    builder.Add(assembly, MachiningTube(100));
    builder.Add(assembly, MachiningTube(500));
    const auto directory = std::filesystem::temp_directory_path() / ("icax-machining-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    const auto step = directory / "nesting.step";
    STEPControl_Writer writer;
    ASSERT_EQ(writer.Transfer(assembly, STEPControl_AsIs), IFSelect_RetDone);
    ASSERT_EQ(writer.Write(step.string().c_str()), IFSelect_RetDone);
    const auto iges = directory / "nesting.iges";
    IGESControl_Writer igesWriter("MM", 1);
    ASSERT_TRUE(igesWriter.AddShape(assembly));
    ASSERT_TRUE(igesWriter.Write(iges.string().c_str()));
    for (const auto& file : {step, iges})
    {
        const auto shape = iCAX::TubeDesigner::ImportMachiningAssemblyFile(file);
        Bnd_Box bounds;
        BRepBndLib::Add(shape, bounds, false);
        double x0,y0,z0,x1,y1,z1;
        bounds.Get(x0,y0,z0,x1,y1,z1);
        EXPECT_NEAR(x0,100,0.01); EXPECT_NEAR(x1,700,0.01);
        EXPECT_NEAR(y0,30,0.01); EXPECT_NEAR(z0,40,0.01);
        iCAX::Services::CServiceProvider services;
        const auto parsed = Parser(services)->Analyze(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"assembly","source"));
        EXPECT_EQ(parsed.Paths.size(),4u);
        EXPECT_TRUE(parsed.Complete);
    }
    EXPECT_THROW(iCAX::TubeDesigner::ImportMachiningAssemblyFile(directory / "nesting.dxf"), std::invalid_argument);
    // Only the explicitly created fixture files are removed.
    std::filesystem::remove(step); std::filesystem::remove(iges); std::filesystem::remove(directory);
}

TEST(BRepToCamPath, PersistedPathValidationRejectsAliasedIdsAndBadCoordinates)
{
    using namespace iCAX::Data;
    const ObjectMap path{ {"id", std::string("path-1")}, {"name", std::string("path")}, {"closed", false},
        {"points", VariantArray{VariantArray{1.,2.,3.},VariantArray{11.,2.,3.}}} };
    EXPECT_EQ(iCAX::TubeDesigner::ValidateMachiningToolpaths({path}).size(),1u);
    EXPECT_THROW(iCAX::TubeDesigner::ValidateMachiningToolpaths({path,path}),std::invalid_argument);
    auto bad = path;
    bad["points"] = VariantArray{VariantArray{std::numeric_limits<double>::infinity(),2.,3.},VariantArray{11.,2.,3.}};
    EXPECT_THROW(iCAX::TubeDesigner::ValidateMachiningToolpaths({bad}),std::invalid_argument);
}
