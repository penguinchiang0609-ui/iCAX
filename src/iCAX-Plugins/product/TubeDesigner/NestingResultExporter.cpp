#include "pch.h"
#include "NestingResultExporter.h"
#include "PartListXlsxExporter.h"
#include "GeometryData/GeometryData.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "Resources/ResourcePool.h"
#include "Task/Task.h"

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
#include <condition_variable>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace
{
    using namespace iCAX::TubeDesigner;
    constexpr double S_Tolerance = 0.011;
    constexpr double S_MaxLength = 1.0e9;
    constexpr std::size_t S_MaxPieces = 1'000'000;
    struct SNestingCachedCompound final
    {
        std::string Signature;
        TopoDS_Shape Shape;
    };

    struct SNestingResourceCache final
    {
        iCAX::Resource::CResourcePool Pool;
        std::mutex Mutex;
        std::condition_variable Changed;
        std::set<std::string> Building;
        std::map<std::string, std::uint64_t> ScopeEpoch;
        std::map<std::string, std::set<std::string>> ScopeKeys;
        std::atomic_bool Stopping = false;
        std::shared_ptr<iCAX::Tasks::ThreadPoolTaskScheduler> Scheduler =
            std::make_shared<iCAX::Tasks::ThreadPoolTaskScheduler>(1);

        ~SNestingResourceCache()
        {
            Stopping.store(true, std::memory_order_release);
            Scheduler->Shutdown();
        }
    };

    SNestingResourceCache& ResultResourceCache()
    {
        static SNestingResourceCache _Cache;
        return _Cache;
    }

    iCAX::Tasks::TaskSchedulerPtr ResultResourceScheduler()
    {
        // Deliberately one worker: background pre-generation must not compete
        // with the solver or interactive geometry work.
        return ResultResourceCache().Scheduler;
    }

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

    std::string ResourceSignature(
        const SNestingExportPlan& Plan_, const std::vector<SNestingExportPart>& Parts_,
        const double PartGap_)
    {
        const auto _Parts = PartMap(Parts_);
        std::ostringstream _Text;
        _Text << std::setprecision(17) << Plan_.StockLength << '|' << Plan_.UsedLength
            << '|' << Plan_.RemainingLength << '|' << PartGap_;
        for (const auto& _Placement : Plan_.Placements)
        {
            const auto _Found = _Parts.find(_Placement.PartID);
            if (_Found == _Parts.end()) throw std::invalid_argument("导出排样引用了不存在的零件");
            const auto& _Part = *_Found->second;
            _Text << ';' << _Placement.PartID << '@' << _Part.SourceResourceID << '@'
                << _Part.SourceResourceVersion << ':' << _Placement.Start << ':' << _Placement.End
                << ':' << _Placement.Flipped << ':' << _Placement.RotationRadians << ':'
                << _Placement.VariantID << ':' << _Placement.HasTransform;
            if (_Placement.HasTransform)
                for (const auto _Value : _Placement.Transform) _Text << ':' << _Value;
        }
        return _Text.str();
    }

    std::string ResourceKey(const std::string& Scope_, const std::string& Signature_)
    {
        std::ostringstream _Key;
        _Key << "runtime://tube-designer/nesting/" << std::hex << std::hash<std::string>{}(Scope_)
            << '/' << std::hash<std::string>{}(Signature_);
        return _Key.str();
    }

    std::shared_ptr<SNestingCachedCompound> FindCachedResourceLocked(
        SNestingResourceCache& Cache_, const std::string& Key_, const std::string& Signature_)
    {
        auto _Resource = std::static_pointer_cast<SNestingCachedCompound>(
            Cache_.Pool.GetUntyped(iCAX::Resource::CResourceKey{ Key_ }, typeid(SNestingCachedCompound)));
        return _Resource && _Resource->Signature == Signature_ ? _Resource : nullptr;
    }

    iCAX::Resource::CResourceInfo ResultResourceInfo(
        const std::string& Key_, const SNestingExportPlan& Plan_)
    {
        iCAX::Resource::CResourceInfo _Info;
        _Info.Key = { Key_ };
        _Info.Source = Key_;
        _Info.Name = "TubeDesigner nesting result";
        _Info.MediaType = "application/vnd.icax.tube-designer.nesting-result";
        _Info.ResourceTypeID = "icax.tube-designer.nesting-result";
        _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        // This is a dedicated runtime pool, so source references are represented
        // by the cache signature instead of cross-pool dependency records.
        _Info.Metadata["placementCount"] = std::to_string(Plan_.Placements.size());
        return _Info;
    }

    void MaterializePlanParts(
        const SNestingExportPlan& Plan_, std::vector<SNestingExportPart>& Parts_)
    {
        std::map<std::string, SNestingExportPart*> _Parts;
        for (auto& _Part : Parts_) _Parts.emplace(_Part.ID, &_Part);
        for (const auto& _Placement : Plan_.Placements)
        {
            const auto _Found = _Parts.find(_Placement.PartID);
            if (_Found == _Parts.end()) throw std::invalid_argument("排样结果引用了不存在的制造零件");
            auto& _Part = *_Found->second;
            if (!_Part.Shape.IsNull()) continue;
            if (!_Part.SourceModel) throw std::invalid_argument("排样结果资源缺少制造几何源");
            const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_Part.SourceModel);
            if (!_Rebuilt.bOK || _Rebuilt.Shape.IsNull())
                throw std::runtime_error("无法在后台重建排样零件制造几何");
            _Part.Shape = _Rebuilt.Shape;
        }
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

    std::filesystem::path ProfileDirectoryName(const std::string& Profile_, std::size_t Index_)
    {
        auto _Label = std::filesystem::path(std::u8string(Profile_.begin(), Profile_.end())).wstring();
        for (auto& _Character : _Label)
            if (_Character < 32 || std::wstring(L"<>:\"/\\|?*").find(_Character) != std::wstring::npos) _Character = L'_';
        if (_Label.size() > 48) {
            _Label.resize(48);
            if (_Label.back() >= 0xd800 && _Label.back() <= 0xdbff) _Label.pop_back();
        }
        while (!_Label.empty() && (_Label.back() == L'.' || _Label.back() == L' ')) _Label.pop_back();
        if (_Label.empty()) _Label = L"profile";
        // Prefix avoids reserved Windows names and keeps identically named but
        // distinct profile keys in separate directories. Labels are never paths.
        return std::filesystem::path(L"spec-" + std::to_wstring(Index_ + 1) + L"-" + _Label);
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
        gp_Trsf _Transform;
        if (_Placement.HasTransform)
        {
            if (!std::ranges::all_of(_Placement.Transform,
                [](const double Value_) { return std::isfinite(Value_); })
                || std::abs(_Placement.Transform[12]) > 1e-9
                || std::abs(_Placement.Transform[13]) > 1e-9
                || std::abs(_Placement.Transform[14]) > 1e-9
                || std::abs(_Placement.Transform[15] - 1.0) > 1e-9)
                throw std::invalid_argument("导出排样零件 TRSF 无效");
            const auto& _M = _Placement.Transform;
            _Transform.SetValues(
                _M[0], _M[1], _M[2], _M[3],
                _M[4], _M[5], _M[6], _M[7],
                _M[8], _M[9], _M[10], _M[11]);
        }
        else
        {
            // Backward compatibility for saved projects created before TRSF was
            // part of the placement contract.
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
            _Transform.SetValues(
                _XDirection, 0, 0,
                (_Placement.Start + _Placement.End) * 0.5 - _XDirection * _CenterX,
                0, _M11, _M12, -(_M11 * _CenterY + _M12 * _CenterZ),
                0, _M21, _M22, -(_M21 * _CenterY + _M22 * _CenterZ));
        }
        BRepBuilderAPI_Transform _Placed(_Shape, _Transform, true);
        if (!_Placed.IsDone() || _Placed.Shape().IsNull()) throw std::runtime_error("无法放置排样零件几何");
        _Builder.Add(_Compound, _Placed.Shape());
    }
    return _Compound;
}

namespace
{
    void ReleaseBuildClaim(const std::string& Key_)
    {
        auto& _Cache = ResultResourceCache();
        {
            const std::lock_guard _Lock(_Cache.Mutex);
            _Cache.Building.erase(Key_);
        }
        _Cache.Changed.notify_all();
    }

    void PublishResultResource(
        const std::string& Key_, const std::string& Signature_, const SNestingExportPlan& Plan_,
        TopoDS_Shape Shape_)
    {
        auto& _Cache = ResultResourceCache();
        auto _Resource = std::make_shared<SNestingCachedCompound>(
            SNestingCachedCompound{ Signature_, std::move(Shape_) });
        std::exception_ptr _Failure;
        {
            const std::lock_guard _Lock(_Cache.Mutex);
            const bool _StillCurrent = std::ranges::any_of(
                _Cache.ScopeKeys, [&](const auto& Scope_) { return Scope_.second.contains(Key_); });
            // The resource does not become visible until the whole compound
            // exists, and an edit cannot publish a just-finished stale build.
            try
            {
                if (_StillCurrent)
                    _Cache.Pool.SetUntyped(
                        iCAX::Resource::CResourceKey{ Key_ }, std::move(_Resource),
                        typeid(SNestingCachedCompound), ResultResourceInfo(Key_, Plan_));
            }
            catch (...) { _Failure = std::current_exception(); }
            _Cache.Building.erase(Key_);
        }
        _Cache.Changed.notify_all();
        if (_Failure) std::rethrow_exception(_Failure);
    }

    std::shared_ptr<SNestingCachedCompound> GetOrBuildResultResource(
        const std::string& Scope_, const SNestingExportPlan& Plan_,
        const std::vector<SNestingExportPart>& Parts_, const double PartGap_)
    {
        if (Scope_.empty())
            return std::make_shared<SNestingCachedCompound>(
                SNestingCachedCompound{ {}, iCAX::TubeDesigner::BuildNestingExportCompound(Plan_, Parts_, PartGap_) });
        const auto _Signature = ResourceSignature(Plan_, Parts_, PartGap_);
        const auto _Key = ResourceKey(Scope_, _Signature);
        auto& _Cache = ResultResourceCache();
        {
            std::unique_lock _Lock(_Cache.Mutex);
            // Export may be the first operation after reopening a saved project.
            _Cache.ScopeKeys[Scope_].insert(_Key);
            if (auto _Found = FindCachedResourceLocked(_Cache, _Key, _Signature)) return _Found;
            while (_Cache.Building.contains(_Key))
            {
                _Cache.Changed.wait(_Lock, [&] { return !_Cache.Building.contains(_Key); });
                if (auto _Found = FindCachedResourceLocked(_Cache, _Key, _Signature)) return _Found;
            }
            _Cache.Building.insert(_Key);
        }
        try
        {
            auto _Shape = iCAX::TubeDesigner::BuildNestingExportCompound(Plan_, Parts_, PartGap_);
            PublishResultResource(_Key, _Signature, Plan_, std::move(_Shape));
        }
        catch (...)
        {
            ReleaseBuildClaim(_Key);
            throw;
        }
        const std::lock_guard _Lock(_Cache.Mutex);
        auto _Result = FindCachedResourceLocked(_Cache, _Key, _Signature);
        if (!_Result) throw std::runtime_error("排样结果资源生成后未能进入资源池");
        return _Result;
    }
}

void iCAX::TubeDesigner::QueueNestingResultResources(
    const std::string& CacheScope_, const std::vector<SNestingExportPlan>& Plans_,
    const std::vector<SNestingExportPart>& Parts_, const double PartGap_)
{
    if (CacheScope_.empty() || Plans_.empty() || Parts_.empty()) return;
    auto& _Cache = ResultResourceCache();
    std::set<std::string> _CurrentKeys;
    for (const auto& _Plan : Plans_)
    {
        const auto _Signature = ResourceSignature(_Plan, Parts_, PartGap_);
        _CurrentKeys.insert(ResourceKey(CacheScope_, _Signature));
    }
    std::uint64_t _Epoch;
    {
        const std::lock_guard _Lock(_Cache.Mutex);
        // No time-based expiry. Resources remain valid until this scene publishes
        // a changed result set (future manual edits use this same invalidation).
        for (const auto& _OldKey : _Cache.ScopeKeys[CacheScope_])
            if (!_CurrentKeys.contains(_OldKey) && !_Cache.Building.contains(_OldKey))
                _Cache.Pool.Unload(iCAX::Resource::CResourceKey{ _OldKey });
        _Cache.ScopeKeys[CacheScope_] = _CurrentKeys;
        _Epoch = ++_Cache.ScopeEpoch[CacheScope_];
    }
    auto _Plans = std::make_shared<const std::vector<SNestingExportPlan>>(Plans_);
    auto _Parts = std::make_shared<const std::vector<SNestingExportPart>>(Parts_);
    (void)iCAX::Tasks::Run([CacheScope_, _Epoch, _Plans, _Parts, PartGap_]
    {
        if (ResultResourceCache().Stopping.load(std::memory_order_acquire)) return;
        auto _Materialized = *_Parts;
        for (const auto& _Plan : *_Plans)
        {
            if (ResultResourceCache().Stopping.load(std::memory_order_acquire)) return;
            std::string _Signature, _Key;
            try
            {
                _Signature = ResourceSignature(_Plan, _Materialized, PartGap_);
                _Key = ResourceKey(CacheScope_, _Signature);
                auto& _Cache = ResultResourceCache();
                {
                    const std::lock_guard _Lock(_Cache.Mutex);
                    if (_Cache.ScopeEpoch[CacheScope_] != _Epoch) return;
                    if (FindCachedResourceLocked(_Cache, _Key, _Signature)
                        || _Cache.Building.contains(_Key))
                        continue;
                    _Cache.Building.insert(_Key);
                }
                MaterializePlanParts(_Plan, _Materialized);
                auto _Shape = BuildNestingExportCompound(_Plan, _Materialized, PartGap_);
                {
                    const std::lock_guard _Lock(_Cache.Mutex);
                    if (_Cache.ScopeEpoch[CacheScope_] != _Epoch)
                    {
                        _Cache.Building.erase(_Key);
                        _Cache.Changed.notify_all();
                        return;
                    }
                }
                PublishResultResource(_Key, _Signature, _Plan, std::move(_Shape));
            }
            catch (...)
            {
                if (!_Key.empty()) ReleaseBuildClaim(_Key);
                // One malformed/unavailable plan must not prevent later plans
                // from being warmed; export will report the concrete failure.
            }
        }
    }, ResultResourceScheduler());
}

bool iCAX::TubeDesigner::IsNestingResultResourceReady(
    const std::string& CacheScope_, const SNestingExportPlan& Plan_,
    const std::vector<SNestingExportPart>& Parts_, const double PartGap_)
{
    if (CacheScope_.empty()) return false;
    const auto _Signature = ResourceSignature(Plan_, Parts_, PartGap_);
    const auto _Key = ResourceKey(CacheScope_, _Signature);
    auto& _Cache = ResultResourceCache();
    const std::lock_guard _Lock(_Cache.Mutex);
    const auto _Scope = _Cache.ScopeKeys.find(CacheScope_);
    return _Scope != _Cache.ScopeKeys.end() && _Scope->second.contains(_Key)
        && static_cast<bool>(FindCachedResourceLocked(_Cache, _Key, _Signature));
}

iCAX::Data::ObjectMap iCAX::TubeDesigner::ExportNestingResults(
    const std::filesystem::path& TargetRoot_,
    const std::vector<SNestingExportPlan>& Plans_,
    const std::vector<SNestingExportPart>& Parts_,
    const double PartGap_,
    const std::string& CacheScope_)
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
    struct SExportGroup {
        std::filesystem::path Directory;
        std::vector<std::vector<STableWorkbookCell>> Rows;
    };
    std::map<std::string, SExportGroup> _Groups;
    std::vector<std::filesystem::path> _RelativePaths;
    // Complete validation before creating files, so invalid requests have no filesystem effects.
    for (std::size_t _PlanIndex = 0; _PlanIndex < Plans_.size(); ++_PlanIndex)
    {
        const auto& _Plan = Plans_[_PlanIndex];
        if (!_PlanIDs.insert(_Plan.ID).second) throw std::invalid_argument("所选母材结果重复");
        _PieceCount += _Plan.Placements.size();
        if (_PieceCount > S_MaxPieces) throw std::invalid_argument("单次最多导出 1000000 件排样零件");
        _Compounds.push_back(GetOrBuildResultResource(
            CacheScope_, _Plan, Parts_, PartGap_)->Shape);
        const auto _GroupKey = _Plan.ProfileKey.empty() ? _Plan.Profile : _Plan.ProfileKey;
        if (!_Groups.contains(_GroupKey))
            _Groups.emplace(_GroupKey, SExportGroup{ ProfileDirectoryName(_Plan.Profile, _Groups.size()), {} });
        auto& _Group = _Groups.at(_GroupKey);
        _RelativePaths.push_back(_Group.Directory / StepName(_PlanIndex));
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
                _Index ? _Placement.Start - _PreviousEnd : 0.0, 1.0, PathText(_RelativePaths.back()) });
            auto _GroupRow = _Rows.back();
            _GroupRow.back() = StepName(_PlanIndex);
            _Group.Rows.push_back(std::move(_GroupRow));
            _PreviousEnd = _Placement.End;
        }
    }
    const auto _Directory = CreateOutputDirectory(_Root);
    iCAX::Data::VariantArray _Files;
    try
    {
        for (const auto& [_Key, _Group] : _Groups)
            if (!std::filesystem::create_directory(_Directory / _Group.Directory))
                throw std::runtime_error("无法创建规格组导出目录");
        for (std::size_t _Index = 0; _Index < _Compounds.size(); ++_Index)
        {
            const auto _Path = _Directory / _RelativePaths[_Index];
            if (std::filesystem::exists(_Path)) throw std::runtime_error("排样 STEP 目标已存在，已停止以避免覆盖");
            STEPControl_Writer _Writer;
            if (_Writer.Transfer(_Compounds[_Index], STEPControl_AsIs) != IFSelect_RetDone
                || _Writer.Write(PathText(_Path).c_str()) != IFSelect_RetDone)
                throw std::runtime_error("无法导出排样真实零件 STEP");
            _Files.emplace_back(PathText(_Path));
        }
        const auto _Workbook = _Directory / "nesting-list.xlsx";
        const std::vector<std::string> _Headers{
            "母材", "截面规格", "母材长度 (mm)", "已用长度 (mm)", "余料 (mm)", "利用率 (%)",
            "切割顺序", "零件编号", "零件名称", "实际零件长 (mm)", "排样占用长 (mm)", "起点 (mm)", "终点 (mm)",
            "翻转", "设定间距 (mm)", "前段间距 (mm)", "数量", "STEP 文件" };
        WriteTableWorkbook(_Workbook, "TubeDesigner 下料排样清单", _Headers, _Rows);
        for (const auto& [_Key, _Group] : _Groups)
            WriteTableWorkbook(_Directory / _Group.Directory / "nesting-list.xlsx", "TubeDesigner 规格组排样清单", _Headers, _Group.Rows);
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
