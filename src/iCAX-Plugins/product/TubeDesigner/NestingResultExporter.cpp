#include "pch.h"
#include "NestingResultExporter.h"
#include "PartListXlsxExporter.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Trsf.hxx>

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <set>

namespace
{
    using namespace iCAX::TubeDesigner;
    constexpr double S_Tolerance = 0.011;
    constexpr double S_MaxLength = 1.0e9;
    constexpr std::size_t S_MaxPieces = 2000;

    std::string PathText(const std::filesystem::path& Path_)
    {
        const auto _Text = Path_.u8string();
        return std::string(reinterpret_cast<const char*>(_Text.data()), _Text.size());
    }

    std::array<double, 6> ShapeBounds(const TopoDS_Shape& Shape_)
    {
        if (Shape_.IsNull()) throw std::invalid_argument("导出零件缺少真实制造几何");
        Bnd_Box _Box;
        BRepBndLib::AddOptimal(Shape_, _Box, false, false);
        if (_Box.IsVoid() || _Box.IsWhole()) throw std::invalid_argument("导出零件几何包围盒无效");
        std::array<double, 6> _Bounds;
        _Box.Get(_Bounds[0], _Bounds[1], _Bounds[2], _Bounds[3], _Bounds[4], _Bounds[5]);
        for (const auto _Value : _Bounds)
            if (!std::isfinite(_Value)) throw std::invalid_argument("导出零件几何尺寸无效");
        if (_Bounds[3] <= _Bounds[0]) throw std::invalid_argument("导出零件没有有效轴向长度");
        return _Bounds;
    }

    void ValidatePlan(const SNestingExportPlan& Plan_, const double PartGap_)
    {
        if (!std::isfinite(PartGap_) || PartGap_ < 0 || PartGap_ > S_MaxLength)
            throw std::invalid_argument("导出零件间距无效");
        if (Plan_.ID.empty() || Plan_.Placements.empty() || Plan_.Placements.size() > S_MaxPieces
            || !std::isfinite(Plan_.StockLength) || Plan_.StockLength <= 0 || Plan_.StockLength > S_MaxLength
            || !std::isfinite(Plan_.UsedLength) || !std::isfinite(Plan_.RemainingLength)
            || Plan_.UsedLength <= 0 || Plan_.RemainingLength < -S_Tolerance
            || std::abs(Plan_.UsedLength + Plan_.RemainingLength - Plan_.StockLength) > S_Tolerance)
            throw std::invalid_argument("导出母材长度或排样结果无效");
        double _End = 0.0;
        const auto _RequiredGap = std::ceil(PartGap_ * 100.0 - 1.0e-6) / 100.0;
        for (std::size_t _Index = 0; _Index < Plan_.Placements.size(); ++_Index)
        {
            const auto& _Placement = Plan_.Placements[_Index];
            const auto _ActualGap = _Placement.Start - _End;
            const auto _DeclaredGap = std::isfinite(_Placement.GapBefore)
                ? _Placement.GapBefore : _ActualGap;
            if (_Placement.PartID.empty() || !std::isfinite(_Placement.Start) || !std::isfinite(_Placement.End)
                || !std::isfinite(_Placement.RotationRadians)
                || _Placement.Start < 0 || _Placement.End <= _Placement.Start
                || std::abs(_DeclaredGap - _ActualGap) > S_Tolerance
                || (!_Index && std::abs(_DeclaredGap) > S_Tolerance)
                || (!_Index && _Placement.NestedWithPrevious)
                || (_Index && _Placement.NestedWithPrevious
                    && _DeclaredGap >= _RequiredGap - 1.0e-6)
                || (_Index && !_Placement.NestedWithPrevious
                    && _DeclaredGap < _RequiredGap - S_Tolerance)
                || _Placement.End > Plan_.StockLength + S_Tolerance)
                throw std::invalid_argument("导出排样存在非法套切、间距不足或超出母材的零件");
            _End = _Placement.End;
        }
        if (std::abs(_End - Plan_.UsedLength) > S_Tolerance)
            throw std::invalid_argument("导出母材已用长度与零件位置不一致");
    }

    std::map<std::string, const SNestingExportPart*> PartMap(const std::vector<SNestingExportPart>& Parts_)
    {
        if (Parts_.empty() || Parts_.size() > S_MaxPieces)
            throw std::invalid_argument("导出零件清单数量无效");
        std::map<std::string, const SNestingExportPart*> _Parts;
        for (const auto& _Part : Parts_)
        {
            if (_Part.ID.empty() || !_Parts.emplace(_Part.ID, &_Part).second)
                throw std::invalid_argument("导出零件标识无效或重复");
        }
        return _Parts;
    }

    std::filesystem::path CreateOutputDirectory(const std::filesystem::path& Root_)
    {
        static std::atomic<unsigned long long> _Sequence{ 0 };
        const auto _Stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        for (int _Attempt = 0; _Attempt < 1000; ++_Attempt)
        {
            const auto _Name = "nesting-" + std::to_string(_Stamp) + "-" + std::to_string(++_Sequence);
            const auto _Target = Root_ / _Name;
            std::error_code _Error;
            if (std::filesystem::create_directory(_Target, _Error)) return _Target;
            if (_Error) throw std::runtime_error("无法新建排样导出目录：" + _Error.message());
        }
        throw std::runtime_error("无法分配新的排样导出目录");
    }

    std::string StepName(const std::size_t Index_)
    {
        std::ostringstream _Name;
        _Name << "stock-" << std::setfill('0') << std::setw(3) << Index_ + 1 << ".step";
        return _Name.str();
    }
}

TopoDS_Shape iCAX::TubeDesigner::BuildNestingExportCompound(
    const SNestingExportPlan& Plan_,
    const std::vector<SNestingExportPart>& Parts_,
    const double PartGap_)
{
    ValidatePlan(Plan_, PartGap_);
    const auto _Parts = PartMap(Parts_);
    TopoDS_Compound _Compound;
    BRep_Builder _Builder;
    _Builder.MakeCompound(_Compound);
    for (const auto& _Placement : Plan_.Placements)
    {
        const auto _Found = _Parts.find(_Placement.PartID);
        if (_Found == _Parts.end()) throw std::invalid_argument("导出排样引用了不存在的零件");
        const auto& _Shape = _Found->second->Shape;
        const auto _Bounds = ShapeBounds(_Shape);
        if (_Bounds[3] - _Bounds[0] > _Placement.End - _Placement.Start + S_Tolerance)
            throw std::invalid_argument("真实制造零件长度超出了排样占用长度");
        const double _CenterX = (_Bounds[0] + _Bounds[3]) * 0.5;
        const double _CenterY = (_Bounds[1] + _Bounds[4]) * 0.5;
        const double _CenterZ = (_Bounds[2] + _Bounds[5]) * 0.5;
        const double _XDirection = _Placement.Flipped ? -1.0 : 1.0;
        const double _YDirection = _Placement.Flipped ? -1.0 : 1.0;
        const double _Cosine = std::cos(_Placement.RotationRadians);
        const double _Sine = std::sin(_Placement.RotationRadians);
        const double _M11 = _Cosine * _YDirection;
        const double _M12 = -_Sine;
        const double _M21 = _Sine * _YDirection;
        const double _M22 = _Cosine;
        // Same rigid transform as nestingPlacementMatrix(): optional end-for-end
        // reversal followed by an allowed rotation around the stock axis.
        gp_Trsf _Transform;
        _Transform.SetValues(
            _XDirection, 0, 0,
            (_Placement.Start + _Placement.End) * 0.5 - _XDirection * _CenterX,
            0, _M11, _M12, -(_M11 * _CenterY + _M12 * _CenterZ),
            0, _M21, _M22, -(_M21 * _CenterY + _M22 * _CenterZ));
        BRepBuilderAPI_Transform _Placed(_Shape, _Transform, true);
        if (!_Placed.IsDone() || _Placed.Shape().IsNull()) throw std::runtime_error("无法放置排样零件几何");
        _Builder.Add(_Compound, _Placed.Shape());
    }
    return _Compound;
}

iCAX::Data::ObjectMap iCAX::TubeDesigner::ExportNestingResults(
    const std::filesystem::path& TargetRoot_,
    const std::vector<SNestingExportPlan>& Plans_,
    const std::vector<SNestingExportPart>& Parts_,
    const double PartGap_)
{
    if (TargetRoot_.empty() || !std::filesystem::is_directory(TargetRoot_))
        throw std::invalid_argument("请选择一个已存在的排样导出目录");
    if (Plans_.empty() || Plans_.size() > S_MaxPieces)
        throw std::invalid_argument("请选择有效的母材排样结果进行导出");
    const auto _Root = std::filesystem::canonical(TargetRoot_);
    const auto _Parts = PartMap(Parts_);
    std::vector<TopoDS_Shape> _Compounds;
    std::set<std::string> _PlanIDs;
    std::size_t _PieceCount = 0;
    std::vector<std::vector<STableWorkbookCell>> _Rows;
    // Complete validation before creating files, so invalid requests have no filesystem effects.
    for (std::size_t _PlanIndex = 0; _PlanIndex < Plans_.size(); ++_PlanIndex)
    {
        const auto& _Plan = Plans_[_PlanIndex];
        if (!_PlanIDs.insert(_Plan.ID).second) throw std::invalid_argument("所选母材结果重复");
        _PieceCount += _Plan.Placements.size();
        if (_PieceCount > S_MaxPieces) throw std::invalid_argument("单次最多导出 2000 件排样零件");
        _Compounds.push_back(BuildNestingExportCompound(_Plan, Parts_, PartGap_));
        double _NetLength = _Plan.PartLength;
        if (!std::isfinite(_NetLength) || _NetLength <= 0.0)
        {
            _NetLength = 0.0;
            for (const auto& _Placement : _Plan.Placements)
                _NetLength += _Placement.End - _Placement.Start;
        }
        double _PreviousEnd = 0.0;
        for (std::size_t _Index = 0; _Index < _Plan.Placements.size(); ++_Index)
        {
            const auto& _Placement = _Plan.Placements[_Index];
            const auto& _Part = *_Parts.at(_Placement.PartID);
            const auto _Bounds = ShapeBounds(_Part.Shape);
            _Rows.push_back({ _Plan.Name.empty() ? _Plan.ID : _Plan.Name, _Plan.Profile, _Plan.StockLength,
                _Plan.UsedLength, _Plan.RemainingLength, _NetLength / _Plan.StockLength * 100.0,
                static_cast<double>(_Index + 1), _Part.Number, _Part.Name, _Bounds[3] - _Bounds[0],
                _Placement.End - _Placement.Start, _Placement.Start, _Placement.End,
                std::string(_Placement.Flipped ? "是" : "否"), PartGap_,
                _Index ? _Placement.Start - _PreviousEnd : 0.0, 1.0, StepName(_PlanIndex) });
            _PreviousEnd = _Placement.End;
        }
    }
    const auto _Directory = CreateOutputDirectory(_Root);
    iCAX::Data::VariantArray _Files;
    try
    {
        for (std::size_t _Index = 0; _Index < _Compounds.size(); ++_Index)
        {
            const auto _Path = _Directory / StepName(_Index);
            if (std::filesystem::exists(_Path)) throw std::runtime_error("排样 STEP 目标已存在，已停止以避免覆盖");
            STEPControl_Writer _Writer;
            if (_Writer.Transfer(_Compounds[_Index], STEPControl_AsIs) != IFSelect_RetDone
                || _Writer.Write(PathText(_Path).c_str()) != IFSelect_RetDone)
                throw std::runtime_error("无法导出排样真实零件 STEP");
            _Files.emplace_back(PathText(_Path));
        }
        const auto _Workbook = _Directory / "nesting-list.xlsx";
        WriteTableWorkbook(_Workbook, "TubeDesigner 下料排样清单", {
            "母材", "截面规格", "母材长度 (mm)", "已用长度 (mm)", "余料 (mm)", "利用率 (%)",
            "切割顺序", "零件编号", "零件名称", "实际零件长 (mm)", "排样占用长 (mm)", "起点 (mm)", "终点 (mm)",
            "翻转", "设定间距 (mm)", "前段间距 (mm)", "数量", "STEP 文件" }, _Rows);
        return {
            { "exportedCount", static_cast<unsigned long long>(Plans_.size()) },
            { "exportedFiles", std::move(_Files) },
            { "partListFile", PathText(_Workbook) },
            { "outputDirectory", PathText(_Directory) },
        };
    }
    catch (const std::exception& Error_)
    {
        // This directory is newly created for this run. Leave partial outputs inspectable;
        // never remove user-selected roots or any pre-existing files on failure.
        throw std::runtime_error(std::string(Error_.what()) + "；本次导出目录：" + PathText(_Directory));
    }
}
