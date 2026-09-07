#include "pch.h"
#include <TubeDesigner/NestingResultExporter.h>
#include <TubeDesigner/PartListXlsxExporter.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

namespace
{
    using namespace iCAX::TubeDesigner;

    std::filesystem::path TestRoot()
    {
        const auto _Root = std::filesystem::temp_directory_path()
            / ("icax-nesting-export-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(_Root)) throw std::runtime_error("Could not allocate export test directory");
        return _Root;
    }

    std::filesystem::path Utf8Path(const std::string& Text_)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(Text_.data()), Text_.size()));
    }

    std::string PathText(const std::filesystem::path& Path_)
    {
        const auto _Text = Path_.u8string();
        return { reinterpret_cast<const char*>(_Text.data()), _Text.size() };
    }

    std::string ReadBytes(const std::filesystem::path& Path_)
    {
        std::ifstream _Stream(Path_, std::ios::binary);
        if (!_Stream) throw std::runtime_error("Could not read export test artifact");
        return { std::istreambuf_iterator<char>(_Stream), std::istreambuf_iterator<char>() };
    }

    TopoDS_Shape RealTube()
    {
        const auto _Outer = BRepPrimAPI_MakeBox(gp_Pnt(-500, -10, -5), 1000, 20, 10).Shape();
        const auto _Inner = BRepPrimAPI_MakeBox(gp_Pnt(-501, -8, -3), 1002, 16, 6).Shape();
        const auto _Tube = BRepAlgoAPI_Cut(_Outer, _Inner).Shape();
        const auto _Hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(200, -15, 0), gp_Dir(0, 1, 0)), 2.0, 30).Shape();
        return BRepAlgoAPI_Cut(_Tube, _Hole).Shape();
    }

    TopoDS_Shape MiteredParallelogramTube()
    {
        BRepBuilderAPI_MakePolygon _Wire;
        _Wire.Add(gp_Pnt(-60, -10, -5));
        _Wire.Add(gp_Pnt(50, -10, -5));
        _Wire.Add(gp_Pnt(60, 10, -5));
        _Wire.Add(gp_Pnt(-50, 10, -5));
        _Wire.Close();
        return BRepPrimAPI_MakePrism(
            BRepBuilderAPI_MakeFace(_Wire.Wire()).Face(), gp_Vec(0, 0, 10)).Shape();
    }

    std::vector<SNestingExportPart> Parts()
    {
        return { { "actual-part", "真实带孔管 & <编号>", "P-001", RealTube() } };
    }

    SNestingExportPlan Plan()
    {
        return { "plan-one", "矩形管 20 × 10 × 2", 6000, 2003, 3997,
            { { "actual-part", 0, 1000, false }, { "actual-part", 1003, 2003, true } }, "母材 5" };
    }

    std::array<double, 6> Bounds(const TopoDS_Shape& Shape_)
    {
        Bnd_Box _Box;
        BRepBndLib::AddOptimal(Shape_, _Box, false, false);
        std::array<double, 6> _Result;
        _Box.Get(_Result[0], _Result[1], _Result[2], _Result[3], _Result[4], _Result[5]);
        return _Result;
    }

    double Volume(const TopoDS_Shape& Shape_)
    {
        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(Shape_, _Properties);
        return _Properties.Mass();
    }
}

TEST(TubeDesignerNestingExport, RigidlyPlacesRealHoledPartsWithoutStockProxy)
{
    const auto _Parts = Parts();
    const auto _Shape = BuildNestingExportCompound(Plan(), _Parts, 3.0);
    const auto _Bounds = Bounds(_Shape);
    EXPECT_NEAR(_Bounds[0], 0.0, 1e-6);
    EXPECT_NEAR(_Bounds[3], 2003.0, 1e-6);
    EXPECT_NEAR(_Bounds[1], -10.0, 1e-6);
    EXPECT_NEAR(_Bounds[4], 10.0, 1e-6);
    EXPECT_NEAR(_Bounds[2], -5.0, 1e-6);
    EXPECT_NEAR(_Bounds[5], 5.0, 1e-6);
    EXPECT_NEAR(Volume(_Shape), 2.0 * Volume(_Parts[0].Shape), 1e-5);
    int _Solids = 0;
    for (TopExp_Explorer _Explorer(_Shape, TopAbs_SOLID); _Explorer.More(); _Explorer.Next()) ++_Solids;
    EXPECT_EQ(_Solids, 2);
}

TEST(TubeDesignerNestingExport, ReversalIsRigidAndPreservesAsymmetricHole)
{
    const auto _Parts = Parts();
    GProp_GProps _Original;
    BRepGProp::VolumeProperties(_Parts[0].Shape, _Original);
    const double _Offset = _Original.CentreOfMass().X();
    EXPECT_GT(std::abs(_Offset), 1e-4);
    const auto _Shape = BuildNestingExportCompound(Plan(), _Parts, 3.0);
    std::vector<double> _Centers;
    for (TopExp_Explorer _Explorer(_Shape, TopAbs_SOLID); _Explorer.More(); _Explorer.Next())
    {
        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(_Explorer.Current(), _Properties);
        _Centers.push_back(_Properties.CentreOfMass().X());
    }
    std::sort(_Centers.begin(), _Centers.end());
    ASSERT_EQ(_Centers.size(), 2u);
    EXPECT_NEAR(_Centers[0], 500.0 + _Offset, 1e-6);
    EXPECT_NEAR(_Centers[1], 1503.0 - _Offset, 1e-6);
}

TEST(TubeDesignerNestingExport, PlacesValidatedMiterEnvelopesWithNegativeGap)
{
    const std::vector<SNestingExportPart> _Parts{
        { "miter", "斜切件", "M-001", MiteredParallelogramTube() }
    };
    SNestingExportPlan _Plan;
    _Plan.ID = "miter-plan";
    _Plan.Profile = "矩形管 20 × 10";
    _Plan.StockLength = 230.0;
    _Plan.UsedLength = 230.0;
    _Plan.RemainingLength = 0.0;
    _Plan.PartLength = 220.0;
    _Plan.Placements = {
        { "miter", 0.0, 120.0, false, 0.0, false, 0.0, "forward-0" },
        { "miter", 110.0, 230.0, false, -10.0, true, 0.0, "forward-0" }
    };
    const auto _Shape = BuildNestingExportCompound(_Plan, _Parts, 0.0);
    const auto _Bounds = Bounds(_Shape);
    EXPECT_NEAR(_Bounds[0], 0.0, 1e-6);
    EXPECT_NEAR(_Bounds[3], 230.0, 1e-6);
    EXPECT_NEAR(Volume(_Shape), 2.0 * Volume(_Parts.front().Shape), 1e-5);
}

TEST(TubeDesignerNestingExport, RejectsInvalidReferencedPartsLengthsAndGapBeforeWriting)
{
    const auto _Root = TestRoot();
    const auto _Parts = Parts();
    auto _Plan = Plan();
    _Plan.Placements[0].PartID = "not-in-source";
    EXPECT_THROW(ExportNestingResults(_Root, { _Plan }, _Parts, 3), std::invalid_argument);
    EXPECT_TRUE(std::filesystem::is_empty(_Root));
    _Plan = Plan();
    _Plan.Placements[1].Start = 1001;
    EXPECT_THROW(BuildNestingExportCompound(_Plan, _Parts, 3), std::invalid_argument);
    _Plan = Plan();
    _Plan.Placements[0].End = 10;
    EXPECT_THROW(BuildNestingExportCompound(_Plan, _Parts, 3), std::invalid_argument);
    _Plan = Plan();
    _Plan.RemainingLength = std::numeric_limits<double>::infinity();
    EXPECT_THROW(BuildNestingExportCompound(_Plan, _Parts, 3), std::invalid_argument);
    EXPECT_THROW(BuildNestingExportCompound(Plan(), _Parts, -1), std::invalid_argument);
    EXPECT_THROW(ExportNestingResults(_Root / "missing", { Plan() }, _Parts, 3), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(_Root / "missing"));
}

TEST(TubeDesignerNestingExport, ExportsRealStepAndFullWorkbookIntoUniqueChildDirectory)
{
    const auto _Root = TestRoot() / Utf8Path("中文导出目录");
    ASSERT_TRUE(std::filesystem::create_directory(_Root));
    const auto _Parts = Parts();
    const auto _Result = ExportNestingResults(_Root, { Plan() }, _Parts, 3);
    EXPECT_EQ(_Result.at("exportedCount").To<unsigned long long>(), 1u);
    const auto _Directory = Utf8Path(_Result.at("outputDirectory").To<std::string>());
    EXPECT_EQ(_Directory.parent_path(), std::filesystem::canonical(_Root));
    const auto _Files = _Result.at("exportedFiles").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(_Files.size(), 1u);
    const auto _Step = Utf8Path(_Files[0].To<std::string>());
    EXPECT_EQ(_Step.filename(), std::filesystem::path("stock-001.step"));
    EXPECT_TRUE(std::filesystem::is_regular_file(_Step));
    STEPControl_Reader _Reader;
    ASSERT_EQ(_Reader.ReadFile(PathText(_Step).c_str()), IFSelect_RetDone);
    ASSERT_GT(_Reader.TransferRoots(), 0);
    const auto _RoundTrip = _Reader.OneShape();
    EXPECT_NEAR(Volume(_RoundTrip), 2.0 * Volume(_Parts[0].Shape), 1e-3);
    EXPECT_NEAR(Bounds(_RoundTrip)[3], 2003.0, 1e-4);
    int _Solids = 0;
    for (TopExp_Explorer _Explorer(_RoundTrip, TopAbs_SOLID); _Explorer.More(); _Explorer.Next()) ++_Solids;
    EXPECT_EQ(_Solids, 2);

    const auto _Workbook = Utf8Path(_Result.at("partListFile").To<std::string>());
    const auto _Content = ReadBytes(_Workbook);
    EXPECT_EQ(_Content.substr(0, 4), std::string("PK\x03\x04", 4));
    EXPECT_NE(_Content.find("TubeDesigner 下料排样清单"), std::string::npos);
    EXPECT_NE(_Content.find("母材长度 (mm)"), std::string::npos);
    EXPECT_NE(_Content.find("前段间距 (mm)"), std::string::npos);
    EXPECT_NE(_Content.find("真实带孔管 &amp; &lt;编号&gt;"), std::string::npos);
    EXPECT_NE(_Content.find("stock-001.step"), std::string::npos);
    EXPECT_NE(_Content.find("母材 5"), std::string::npos);
    EXPECT_NE(_Content.find("<dimension ref=\"A1:R4\"/>"), std::string::npos);

    const auto _OriginalStepBytes = ReadBytes(_Step);
    const auto _Second = ExportNestingResults(_Root, { Plan() }, _Parts, 3);
    EXPECT_NE(_Result.at("outputDirectory").To<std::string>(), _Second.at("outputDirectory").To<std::string>());
    EXPECT_EQ(ReadBytes(_Step), _OriginalStepBytes);
}

TEST(TubeDesignerNestingExport, WorkbookTreatsUserTextAsLiteralAndRefusesOverwrite)
{
    const auto _Root = TestRoot();
    const auto _Path = _Root / "safe.xlsx";
    WriteTableWorkbook(_Path, "测试清单", { "文本", "长度" }, {
        { std::string("=HYPERLINK(\"file:///not-opened\")"), 12.5 },
        { std::string("<tag>& value"), 1.0 },
    });
    const auto _Content = ReadBytes(_Path);
    EXPECT_NE(_Content.find("=HYPERLINK(&quot;file:///not-opened&quot;)"), std::string::npos);
    EXPECT_EQ(_Content.find("<f>"), std::string::npos);
    EXPECT_NE(_Content.find("&lt;tag&gt;&amp; value"), std::string::npos);
    EXPECT_NE(_Content.find("<v>12.5</v>"), std::string::npos);
    EXPECT_THROW(WriteTableWorkbook(_Path, "another", { "text" }, { { std::string("changed") } }), std::invalid_argument);
    EXPECT_EQ(ReadBytes(_Path), _Content);
    EXPECT_THROW(WriteTableWorkbook(_Root / "invalid.xlsx", "invalid", { "number" }, {
        { std::numeric_limits<double>::quiet_NaN() } }), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(_Root / "invalid.xlsx"));
}

TEST(TubeDesignerNestingExport, PartListWorkbookPreservesLegacyColumnsAndAppendsManufacturingFields)
{
    const auto _Path = TestRoot() / "original-part-list.xlsx";
    WritePartListWorkbook(_Path, {
        { 1, "产品", "PRODUCT-1", 1, "P-001", "横杆", "矩形管", "40 × 20 × R2 × 1.5",
            1200, 2, "P-001.step", "产品/P-001.step" },
        { 1, "产品", "PRODUCT-1", 2, "P-002", "中间封板", "板件", "300 × 600 × 2",
            600, 1, "P-002.step", "产品/P-002.step", "plate", 300, 600, 2, "Q235B", "made", "激光切割" },
        { 1, "产品", "PRODUCT-1", 3, "P-003", "玻璃板", "玻璃", "500 × 800 × 8",
            800, 1, "P-003.step", "产品/P-003.step", "glass", 500, 800, 8, "钢化玻璃", "purchased", "磨边钢化" },
        { 1, "产品", "PRODUCT-1", 4, "P-004", "柱帽", "配件", "40 × 40 × 20",
            40, 2, "P-004.step", "产品/P-004.step", "accessory", 40, 40, 20, "304", "purchased", "purchased" },
    });
    const auto _Content = ReadBytes(_Path);
    EXPECT_NE(_Content.find("TubeDesigner 零件清单"), std::string::npos);
    EXPECT_NE(_Content.find("<dimension ref=\"A1:S6\"/>"), std::string::npos);
    EXPECT_NE(_Content.find("<mergeCell ref=\"A1:S1\"/>"), std::string::npos);
    EXPECT_NE(_Content.find("<autoFilter ref=\"D2:S6\"/>"), std::string::npos);
    const auto _Cell = [&](const std::string& Reference_) -> std::string {
        const auto _Start = _Content.find("<c r=\"" + Reference_ + "\"");
        EXPECT_NE(_Start, std::string::npos) << Reference_;
        if (_Start == std::string::npos) return {};
        const auto _End = _Content.find("</c>", _Start);
        EXPECT_NE(_End, std::string::npos) << Reference_;
        return _End == std::string::npos ? std::string{} : _Content.substr(_Start, _End + 4 - _Start);
    };
    // Existing A-L data and M-Q plate fields must not shift when R-S are appended.
    const std::array<std::string, 12> _LegacyValues{
        "1", "产品", "PRODUCT-1", "1", "P-001", "横杆", "矩形管",
        "40 × 20 × R2 × 1.5", "1200", "2", "P-001.step", "产品/P-001.step"
    };
    for (std::size_t _Index = 0; _Index < _LegacyValues.size(); ++_Index)
    {
        const auto _Reference = std::string(1, static_cast<char>('A' + _Index)) + "3";
        const bool _Numeric = _Index == 0 || _Index == 3 || _Index == 8 || _Index == 9;
        const auto _Value = _Numeric ? "<v>" + _LegacyValues[_Index] + "</v>"
            : "<t xml:space=\"preserve\">" + _LegacyValues[_Index] + "</t>";
        EXPECT_NE(_Cell(_Reference).find(_Value), std::string::npos) << _Reference;
    }
    const std::array<std::string, 19> _Headers{
        "产品序号", "产品名称", "产品编码", "零件序号", "零件号", "零件名称",
        "管型 / 类别", "规格 / 板件宽×高×厚", "管材长度 (mm)", "数量", "文件名", "相对路径",
        "零件类型", "板宽 (mm)", "板高 (mm)", "板厚 (mm)", "材料", "供料方式", "加工方式"
    };
    for (std::size_t _Index = 0; _Index < _Headers.size(); ++_Index)
    {
        const auto _Reference = std::string(1, static_cast<char>('A' + _Index)) + "2";
        EXPECT_NE(_Cell(_Reference).find(_Headers[_Index]), std::string::npos) << _Reference;
    }
    EXPECT_NE(_Cell("M3").find("管材"), std::string::npos);
    EXPECT_EQ(_Cell("N3").find("<v>"), std::string::npos);
    EXPECT_EQ(_Cell("O3").find("<v>"), std::string::npos);
    EXPECT_EQ(_Cell("P3").find("<v>"), std::string::npos);
    EXPECT_NE(_Cell("H4").find("300 × 600 × 2"), std::string::npos);
    EXPECT_EQ(_Cell("I4").find("<v>"), std::string::npos);
    EXPECT_NE(_Cell("L4").find("产品/P-002.step"), std::string::npos);
    EXPECT_NE(_Cell("M4").find("板件"), std::string::npos);
    EXPECT_NE(_Cell("N4").find("<v>300</v>"), std::string::npos);
    EXPECT_NE(_Cell("O4").find("<v>600</v>"), std::string::npos);
    EXPECT_NE(_Cell("P4").find("<v>2</v>"), std::string::npos);
    EXPECT_NE(_Cell("Q4").find("Q235B"), std::string::npos);
    EXPECT_NE(_Cell("R3").find("<t xml:space=\"preserve\"></t>"), std::string::npos);
    EXPECT_NE(_Cell("S3").find("<t xml:space=\"preserve\"></t>"), std::string::npos);
    EXPECT_NE(_Cell("R4").find("自制"), std::string::npos);
    EXPECT_NE(_Cell("S4").find("激光切割"), std::string::npos);
    EXPECT_EQ(_Cell("I5").find("<v>"), std::string::npos);
    EXPECT_NE(_Cell("M5").find("玻璃"), std::string::npos);
    EXPECT_NE(_Cell("N5").find("<v>500</v>"), std::string::npos);
    EXPECT_NE(_Cell("O5").find("<v>800</v>"), std::string::npos);
    EXPECT_NE(_Cell("P5").find("<v>8</v>"), std::string::npos);
    EXPECT_NE(_Cell("Q5").find("钢化玻璃"), std::string::npos);
    EXPECT_NE(_Cell("R5").find("外购"), std::string::npos);
    EXPECT_NE(_Cell("S5").find("磨边钢化"), std::string::npos);
    EXPECT_EQ(_Cell("I6").find("<v>"), std::string::npos);
    EXPECT_NE(_Cell("M6").find("配件"), std::string::npos);
    EXPECT_EQ(_Cell("N6").find("<v>"), std::string::npos);
    EXPECT_EQ(_Cell("O6").find("<v>"), std::string::npos);
    EXPECT_EQ(_Cell("P6").find("<v>"), std::string::npos);
    EXPECT_NE(_Cell("Q6").find("304"), std::string::npos);
    EXPECT_NE(_Cell("R6").find("外购"), std::string::npos);
    EXPECT_NE(_Cell("S6").find("外购成品"), std::string::npos);
    EXPECT_EQ(_Content.find("母材长度 (mm)"), std::string::npos);
}
