#include "pch.h"

#include <gtest/gtest.h>

#include <ExtrusionRecognition/ExtrusionRecognitionService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h>

#include <STEPControl_Reader.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <algorithm>
#include <array>
#include <limits>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::ExtrusionRecognition;
    using namespace iCAX::GeometryData;

    Variant Path(const std::string& Path_)
    {
        return ObjectMap{ { "path", Path_ } };
    }

    Variant Op(const std::string& Name_, VariantArray Arguments_)
    {
        return ObjectMap{
            { "op", Name_ },
            { "args", std::move(Arguments_) }
        };
    }

    void AddLoopWalls(
        Triangulation3& Mesh_,
        const std::array<Point2, 4>& Loop_,
        double dLength_)
    {
        for (std::size_t _Index = 0; _Index < Loop_.size(); ++_Index)
        {
            const auto& _A = Loop_[_Index];
            const auto& _B = Loop_[(_Index + 1) % Loop_.size()];
            const auto _Base = static_cast<std::uint32_t>(Mesh_.Vertices.size());
            Mesh_.Vertices.insert(Mesh_.Vertices.end(), {
                { _A.X, _A.Y, 0.0 },
                { _B.X, _B.Y, 0.0 },
                { _B.X, _B.Y, dLength_ },
                { _A.X, _A.Y, dLength_ }
            });
            Mesh_.Triangles.push_back({ _Base, _Base + 1, _Base + 2 });
            Mesh_.Triangles.push_back({ _Base, _Base + 2, _Base + 3 });
        }
    }

    void AddTaperedLoopWalls(
        Triangulation3& Mesh_,
        const std::array<Point2, 4>& First_,
        const std::array<Point2, 4>& Last_,
        double dLength_)
    {
        for (std::size_t _Index = 0; _Index < First_.size(); ++_Index)
        {
            const auto _Next = (_Index + 1) % First_.size();
            const auto _Base = static_cast<std::uint32_t>(Mesh_.Vertices.size());
            Mesh_.Vertices.insert(Mesh_.Vertices.end(), {
                { First_[_Index].X, First_[_Index].Y, 0.0 },
                { First_[_Next].X, First_[_Next].Y, 0.0 },
                { Last_[_Next].X, Last_[_Next].Y, dLength_ },
                { Last_[_Index].X, Last_[_Index].Y, dLength_ }
            });
            Mesh_.Triangles.push_back({ _Base, _Base + 1, _Base + 2 });
            Mesh_.Triangles.push_back({ _Base, _Base + 2, _Base + 3 });
        }
    }

    BRepModel MakeRectangularTube(double dLength_ = 240.0)
    {
        Triangulation3Record _Record;
        _Record.Id = 10;
        AddLoopWalls(_Record.Geometry, {
            Point2{ -50.0, -25.0 },
            Point2{  50.0, -25.0 },
            Point2{  50.0,  25.0 },
            Point2{ -50.0,  25.0 }
        }, dLength_);
        AddLoopWalls(_Record.Geometry, {
            Point2{ -44.0, -19.0 },
            Point2{ -44.0,  19.0 },
            Point2{  44.0,  19.0 },
            Point2{  44.0, -19.0 }
        }, dLength_);

        BRepModel _Model;
        _Model.Triangulations3.push_back(std::move(_Record));
        _Model.Solids.push_back(BRepSolid{ 1 });
        _Model.RootSolidIds.push_back(1);
        return _Model;
    }

    BRepModel MakeTaperedRectangularTube(double dLength_ = 240.0)
    {
        Triangulation3Record _Record;
        _Record.Id = 11;
        AddTaperedLoopWalls(_Record.Geometry, {
            Point2{ -50.0, -25.0 }, Point2{ 50.0, -25.0 },
            Point2{ 50.0, 25.0 }, Point2{ -50.0, 25.0 }
        }, {
            Point2{ -70.0, -35.0 }, Point2{ 70.0, -35.0 },
            Point2{ 70.0, 35.0 }, Point2{ -70.0, 35.0 }
        }, dLength_);
        AddTaperedLoopWalls(_Record.Geometry, {
            Point2{ -44.0, -19.0 }, Point2{ -44.0, 19.0 },
            Point2{ 44.0, 19.0 }, Point2{ 44.0, -19.0 }
        }, {
            Point2{ -64.0, -29.0 }, Point2{ -64.0, 29.0 },
            Point2{ 64.0, 29.0 }, Point2{ 64.0, -29.0 }
        }, dLength_);

        BRepModel _Model;
        _Model.Triangulations3.push_back(std::move(_Record));
        _Model.Solids.push_back(BRepSolid{ 1 });
        _Model.RootSolidIds.push_back(1);
        return _Model;
    }

    SSectionTypeDefinition MakeRectangleDefinition(
        const std::string& TypeID_,
        unsigned long long nCavityCount_)
    {
        SSectionTypeDefinition _Definition;
        _Definition.TypeID = TypeID_;
        _Definition.DisplayName = TypeID_;
        _Definition.Bindings["outer"] = Op(
            "fit.rectangle",
            { std::string("outer") });
        _Definition.Match = Op("all", {
            Op("equals", { Path("section.cavityCount"), nCavityCount_ }),
            Path("$outer.valid")
        });
        _Definition.Parameters["width"] = Path("$outer.width");
        _Definition.Parameters["height"] = Path("$outer.height");
        _Definition.Parameters["cavityCount"] = Path("section.cavityCount");
        _Definition.Placement["origin"] = Path("$outer.center");
        _Definition.Placement["xDirection"] = Path("$outer.longAxis");
        return _Definition;
    }

    std::pair<double, double> YRange(const BRepModel& Geometry_)
    {
        auto _Minimum = (std::numeric_limits<double>::max)();
        auto _Maximum = (std::numeric_limits<double>::lowest)();
        for (const auto& _Record : Geometry_.Triangulations3)
        {
            for (const auto& _Point : _Record.Geometry.Vertices)
            {
                _Minimum = std::min(_Minimum, _Point.Y);
                _Maximum = std::max(_Maximum, _Point.Y);
            }
        }
        return { _Minimum, _Maximum };
    }

    std::filesystem::path RepositoryRoot()
    {
        auto _Path = std::filesystem::path(__FILE__).parent_path();
        for (int _Index = 0; _Index < 6; ++_Index)
        {
            _Path = _Path.parent_path();
        }
        return _Path;
    }

    constexpr std::string_view kSampleDefinitions = R"json(
    {
      "definitions": [
        {
          "typeId": "tube.circular.hollow",
          "bindings": {
            "outer": { "op": "fit.circle", "args": ["outer"] },
            "inner": { "op": "fit.circle", "args": ["inner:0"] }
          },
          "match": { "op": "all", "args": [
            { "op": "equals", "args": [{ "path": "section.cavityCount" }, 1] },
            { "path": "$outer.valid" },
            { "path": "$inner.valid" }
          ] }
        },
        {
          "typeId": "tube.rectangular.multi-cavity",
          "bindings": {
            "outer": { "op": "fit.rectangle", "args": ["outer"] }
          },
          "match": { "op": "all", "args": [
            { "op": "greater", "args": [{ "path": "section.cavityCount" }, 1] },
            { "path": "$outer.valid" }
          ] },
          "parameters": {
            "width": { "path": "$outer.width" },
            "height": { "path": "$outer.height" },
            "cavityCount": { "path": "section.cavityCount" }
          },
          "placement": {
            "origin": { "path": "$outer.center" },
            "xDirection": { "path": "$outer.longAxis" }
          }
        }
      ]
    })json";
}

TEST(ExtrusionRecognitionTest, RecognizesHollowSectionAndNormalizesToYAxis)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();

    const std::vector<SSectionTypeDefinition> _Definitions{
        MakeRectangleDefinition("solid-rectangle", 0),
        MakeRectangleDefinition("rectangular-tube", 1)
    };
    const auto _Result = _Service.Recognize(
        MakeRectangularTube(),
        _Definitions);

    ASSERT_TRUE(_Result.IsOK());
    EXPECT_EQ("rectangular-tube", _Result.SectionTypeID);
    EXPECT_EQ(1u, _Result.Section.nCavityCount);
    EXPECT_EQ(2u, _Result.Section.Loops.size());
    EXPECT_NEAR(240.0, _Result.dLength, 1.0e-6);
    ASSERT_TRUE(_Result.SectionParameters.at("width").Is<double>());
    ASSERT_TRUE(_Result.SectionParameters.at("height").Is<double>());
    EXPECT_NEAR(100.0, _Result.SectionParameters.at("width").To<double>(), 1.0e-6);
    EXPECT_NEAR(50.0, _Result.SectionParameters.at("height").To<double>(), 1.0e-6);

    const auto [_MinimumY, _MaximumY] = YRange(_Result.NormalizedGeometry);
    EXPECT_NEAR(0.0, _MinimumY, 1.0e-6);
    EXPECT_NEAR(240.0, _MaximumY, 1.0e-6);
}

TEST(ExtrusionRecognitionTest, UsesFirstMatchingDefinitionWithoutConfidenceCompetition)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();

    auto _First = MakeRectangleDefinition("first", 1);
    auto _Second = MakeRectangleDefinition("second", 1);
    const std::vector<SSectionTypeDefinition> _Definitions{ _First, _Second };

    const auto _Result = _Service.Recognize(
        MakeRectangularTube(),
        _Definitions);

    ASSERT_TRUE(_Result.IsOK());
    EXPECT_EQ("first", _Result.SectionTypeID);
}

TEST(ExtrusionRecognitionTest, RejectsTaperWhoseAxialSectionsKeepChanging)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const std::vector<SSectionTypeDefinition> _Definitions{
        MakeRectangleDefinition("rectangular-tube", 1)
    };

    const auto _Result = _Service.Recognize(
        MakeTaperedRectangularTube(),
        _Definitions);

    EXPECT_FALSE(_Result.IsOK());
    EXPECT_EQ(ERecognitionStatus::NotExtrusionBased, _Result.Status);
}

TEST(ExtrusionRecognitionTest, RejectsUnknownDeclarativeOperator)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();

    SSectionTypeDefinition _Definition;
    _Definition.TypeID = "invalid";
    _Definition.Match = Op("company.privateShapeGuess", {});

    const auto _Validation = _Service.ValidateDefinition(_Definition);

    EXPECT_FALSE(_Validation.bValid);
    ASSERT_FALSE(_Validation.Diagnostics.empty());
    EXPECT_NE(std::string::npos,
        _Validation.Diagnostics.front().find("company.privateShapeGuess"));
}

TEST(ExtrusionRecognitionTest, LoadsStandardJsonAndPreservesDefinitionOrder)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();

    constexpr std::string_view _JSON = R"json(
    {
      "schema": "icax.extrusion-section-types",
      "schemaVersion": 1,
      "definitions": [
        {
          "typeId": "solid-rectangle",
          "bindings": {
            "outer": { "op": "fit.rectangle", "args": ["outer"] }
          },
          "match": {
            "op": "all",
            "args": [
              { "op": "equals", "args": [
                { "path": "section.cavityCount" }, 0
              ] },
              { "path": "$outer.valid" }
            ]
          }
        },
        {
          "typeId": "rectangular-tube",
          "bindings": {
            "outer": { "op": "fit.rectangle", "args": ["outer"] }
          },
          "match": {
            "op": "all",
            "args": [
              { "op": "equals", "args": [
                { "path": "section.cavityCount" }, 1
              ] },
              { "path": "$outer.valid" }
            ]
          },
          "parameters": {
            "width": { "path": "$outer.width" },
            "height": { "path": "$outer.height" }
          },
          "placement": {
            "origin": { "path": "$outer.center" },
            "xDirection": { "path": "$outer.longAxis" }
          }
        }
      ]
    })json";

    const auto _Loaded = _Service.ParseDefinitionsJSON(_JSON);
    ASSERT_TRUE(_Loaded.bValid);
    ASSERT_EQ(2u, _Loaded.Definitions.size());
    EXPECT_EQ("solid-rectangle", _Loaded.Definitions[0].TypeID);
    EXPECT_EQ("rectangular-tube", _Loaded.Definitions[1].TypeID);

    const auto _Result = _Service.Recognize(
        MakeRectangularTube(),
        _Loaded.Definitions);
    ASSERT_TRUE(_Result.IsOK());
    EXPECT_EQ("rectangular-tube", _Result.SectionTypeID);
}

TEST(ExtrusionRecognitionTest, RecognizesAllTubeOneStepRegressionSamples)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const auto _Definitions = _Service.ParseDefinitionsJSON(kSampleDefinitions);
    ASSERT_TRUE(_Definitions.bValid);

    const std::array<std::pair<const char*, const char*>, 9> _Cases{ {
        { "01_round_tube_plain.step", "tube.circular.hollow" },
        { "02_double_cavity_rectangular_tube.step", "tube.rectangular.multi-cavity" },
        { "03_round_tube_through_hole.step", "tube.circular.hollow" },
        { "04_round_tube_oblique_hole.step", "tube.circular.hollow" },
        { "05_double_cavity_blind_hole.step", "tube.rectangular.multi-cavity" },
        { "06_opened_chamber_internal_wall_hole.step", "tube.rectangular.multi-cavity" },
        { "07_round_tube_stepped_uv_end.step", "tube.circular.hollow" },
        { "08_double_cavity_beveled_hole.step", "tube.rectangular.multi-cavity" },
        { "09_double_cavity_mitered_ends.step", "tube.rectangular.multi-cavity" }
    } };

    for (const auto& [_FileName, _ExpectedType] : _Cases)
    {
        SCOPED_TRACE(_FileName);
        const auto _Path = RepositoryRoot() / "samples" / "tube-one" / _FileName;
        const auto _Read = iCAX::OpenCascade::ReadBRepFile(
            _Path.string(),
            0.001);
        ASSERT_TRUE(_Read.bOK);
        const auto _Recognition = _Service.Recognize(
            _Read.Geometry,
            _Definitions.Definitions);
        std::string _Diagnostics;
        for (const auto& _Item : _Recognition.Diagnostics)
        {
            if (!_Diagnostics.empty()) _Diagnostics += " | ";
            _Diagnostics += _Item;
        }
        _Diagnostics += " | cavities="
            + std::to_string(_Recognition.Section.nCavityCount)
            + " loops=" + std::to_string(_Recognition.Section.Loops.size());
        for (const auto& _Loop : _Recognition.Section.Loops)
        {
            _Diagnostics += " " + _Loop.ID + "="
                + std::to_string(_Loop.Points.size());
        }
        ASSERT_TRUE(_Recognition.IsOK()) << _Diagnostics;
        EXPECT_EQ(_ExpectedType, _Recognition.SectionTypeID);
        EXPECT_GT(_Recognition.dLength, 0.0);
    }
}

TEST(ExtrusionRecognitionTest, RebuildsNormalizedResourceForOnDemandCadIntent)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const auto _Definitions = _Service.ParseDefinitionsJSON(kSampleDefinitions);
    ASSERT_TRUE(_Definitions.bValid);

    const auto _Path = RepositoryRoot()
        / "samples" / "tube-one" / "01_round_tube_plain.step";
    STEPControl_Reader _DirectReader;
    ASSERT_EQ(IFSelect_RetDone, _DirectReader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_DirectReader.TransferRoots(), 0);
    const auto _DirectIntent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _DirectReader.OneShape());
    std::string _DirectDiagnostics;
    for (const auto& _Item : _DirectIntent.Geometry.Diagnostics)
        _DirectDiagnostics += _Item.Code + ": " + _Item.Message + " | ";
    ASSERT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _DirectIntent.Geometry.RecognitionStatus) << _DirectDiagnostics;
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(_Path.string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    const auto _Recognition = _Service.Recognize(
        _Read.Geometry,
        _Definitions.Definitions);
    ASSERT_TRUE(_Recognition.IsOK());

    const auto _UnnormalizedRebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(
        _Read.Geometry);
    ASSERT_TRUE(_UnnormalizedRebuilt.bOK);
    const auto _UnnormalizedIntent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _UnnormalizedRebuilt.Shape);
    std::string _UnnormalizedDiagnostics;
    for (const auto& _Item : _UnnormalizedIntent.Geometry.Diagnostics)
        _UnnormalizedDiagnostics += _Item.Code + ": " + _Item.Message + " | ";
    EXPECT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _UnnormalizedIntent.Geometry.RecognitionStatus) << _UnnormalizedDiagnostics;

    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(
        _Recognition.NormalizedGeometry);
    std::string _Diagnostics;
    for (const auto& _Item : _Rebuilt.Diagnostics)
    {
        if (!_Diagnostics.empty()) _Diagnostics += " | ";
        _Diagnostics += _Item;
    }
    ASSERT_TRUE(_Rebuilt.bOK) << _Diagnostics;

    const auto _Intent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _Rebuilt.Shape);
    for (const auto& _Item : _Intent.Geometry.Diagnostics)
    {
        if (!_Diagnostics.empty()) _Diagnostics += " | ";
        _Diagnostics += _Item.Code + ": " + _Item.Message;
    }
    EXPECT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _Intent.Geometry.RecognitionStatus) << _Diagnostics;
    EXPECT_FALSE(_Intent.Geometry.SolidNodes.empty()) << _Diagnostics;
}

TEST(ExtrusionRecognitionTest, RebuildsNormalizedMultiCavityResourceForCadIntent)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const auto _Definitions = _Service.ParseDefinitionsJSON(kSampleDefinitions);
    ASSERT_TRUE(_Definitions.bValid);
    const auto _Path = RepositoryRoot()
        / "samples" / "tube-one" / "02_double_cavity_rectangular_tube.step";
    STEPControl_Reader _DirectReader;
    ASSERT_EQ(IFSelect_RetDone, _DirectReader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_DirectReader.TransferRoots(), 0);
    const auto _DirectIntent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _DirectReader.OneShape());
    std::string _DirectDiagnostics;
    for (const auto& _Item : _DirectIntent.Geometry.Diagnostics)
        _DirectDiagnostics += _Item.Code + ": " + _Item.Message + " | ";
    ASSERT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _DirectIntent.Geometry.RecognitionStatus) << _DirectDiagnostics;
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(_Path.string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    const auto _Recognition = _Service.Recognize(
        _Read.Geometry,
        _Definitions.Definitions);
    ASSERT_TRUE(_Recognition.IsOK());
    ASSERT_EQ(2u, _Recognition.Section.nCavityCount);

    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(
        _Recognition.NormalizedGeometry);
    ASSERT_TRUE(_Rebuilt.bOK);
    EXPECT_TRUE(_Rebuilt.Diagnostics.empty());
    const auto _Intent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _Rebuilt.Shape);
    std::string _Diagnostics;
    GProp_GProps _DirectMass;
    GProp_GProps _RebuiltMass;
    BRepGProp::VolumeProperties(_DirectReader.OneShape(), _DirectMass);
    BRepGProp::VolumeProperties(_Rebuilt.Shape, _RebuiltMass);
    _Diagnostics += "directVolume=" + std::to_string(_DirectMass.Mass())
        + " rebuiltVolume=" + std::to_string(_RebuiltMass.Mass()) + " | ";
    std::size_t _ForwardFaces = 0;
    std::size_t _ReversedFaces = 0;
    for (const auto& _Shell : _Recognition.NormalizedGeometry.Shells)
    {
        for (const auto _Orientation : _Shell.FaceOrientations)
        {
            if (_Orientation == ETopologyOrientation::Reversed) ++_ReversedFaces;
            else if (_Orientation == ETopologyOrientation::Forward) ++_ForwardFaces;
        }
    }
    _Diagnostics += "shellFaceOrientation(F/R)="
        + std::to_string(_ForwardFaces) + "/" + std::to_string(_ReversedFaces) + " | ";
    for (const auto& _Solid : _Recognition.NormalizedGeometry.Solids)
    {
        _Diagnostics += "solidShellOrientations=";
        for (const auto _Orientation : _Solid.ShellOrientations)
            _Diagnostics += std::to_string(static_cast<int>(_Orientation)) + ",";
        _Diagnostics += " | ";
    }
    for (const auto& _Item : _Intent.Geometry.Diagnostics)
        _Diagnostics += _Item.Code + ": " + _Item.Message + " | ";
    EXPECT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _Intent.Geometry.RecognitionStatus) << _Diagnostics;
    EXPECT_FALSE(_Intent.Geometry.SolidNodes.empty()) << _Diagnostics;
}

TEST(ExtrusionRecognitionTest, PreservesVolumeWhenNormalizingMultiCavityBeveledPart)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const auto _Definitions = _Service.ParseDefinitionsJSON(kSampleDefinitions);
    ASSERT_TRUE(_Definitions.bValid);
    const auto _Path = RepositoryRoot()
        / "samples" / "tube-one" / "08_double_cavity_beveled_hole.step";

    STEPControl_Reader _DirectReader;
    ASSERT_EQ(IFSelect_RetDone, _DirectReader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_DirectReader.TransferRoots(), 0);
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(_Path.string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    const auto _Recognition = _Service.Recognize(
        _Read.Geometry,
        _Definitions.Definitions);
    ASSERT_TRUE(_Recognition.IsOK());
    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(
        _Recognition.NormalizedGeometry);
    ASSERT_TRUE(_Rebuilt.bOK);
    EXPECT_TRUE(_Rebuilt.Diagnostics.empty());

    GProp_GProps _DirectMass;
    GProp_GProps _RebuiltMass;
    BRepGProp::VolumeProperties(_DirectReader.OneShape(), _DirectMass);
    BRepGProp::VolumeProperties(_Rebuilt.Shape, _RebuiltMass);
    ASSERT_GT(std::abs(_DirectMass.Mass()), 1.0);
    EXPECT_NEAR(
        std::abs(_DirectMass.Mass()),
        std::abs(_RebuiltMass.Mass()),
        std::abs(_DirectMass.Mass()) * 1.0e-6);

    const auto _Intent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _Rebuilt.Shape);
    const auto _DirectIntent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _DirectReader.OneShape());
    GProp_GProps _BaseMass;
    GProp_GProps _RemovalMass;
    GProp_GProps _AdditionMass;
    BRepGProp::VolumeProperties(_Intent.BaseShape, _BaseMass);
    BRepGProp::VolumeProperties(_Intent.RemovalShape, _RemovalMass);
    BRepGProp::VolumeProperties(_Intent.AdditionShape, _AdditionMass);
    GProp_GProps _DirectBaseMass;
    GProp_GProps _DirectRemovalMass;
    GProp_GProps _DirectAdditionMass;
    BRepGProp::VolumeProperties(_DirectIntent.BaseShape, _DirectBaseMass);
    BRepGProp::VolumeProperties(_DirectIntent.RemovalShape, _DirectRemovalMass);
    BRepGProp::VolumeProperties(_DirectIntent.AdditionShape, _DirectAdditionMass);
    const auto _Volumes = "direct=" + std::to_string(_DirectMass.Mass())
        + " rebuilt=" + std::to_string(_RebuiltMass.Mass())
        + " base=" + std::to_string(_BaseMass.Mass())
        + " removal=" + std::to_string(_RemovalMass.Mass())
        + " addition=" + std::to_string(_AdditionMass.Mass())
        + " directBase=" + std::to_string(_DirectBaseMass.Mass())
        + " directRemoval=" + std::to_string(_DirectRemovalMass.Mass())
        + " directAddition=" + std::to_string(_DirectAdditionMass.Mass())
        + " rebuildDiagnostics=" + [&_Rebuilt]() {
            std::string _Text;
            for (const auto& _Item : _Rebuilt.Diagnostics) _Text += _Item + "|";
            return _Text;
        }();
    EXPECT_LT(_Intent.Geometry.RelativeOpaqueResidualVolume, 1.0e-6) << _Volumes;
    EXPECT_LT(_Intent.Geometry.RelativeVolumeError, 1.0e-6) << _Volumes;
}
