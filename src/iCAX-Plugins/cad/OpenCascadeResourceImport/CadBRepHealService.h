#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include <functional>
#include <string>
#include <vector>

namespace iCAX::CAD
{
    struct SCadBRepHealOptions final
    {
        double LinearTolerance = 1e-6;
        double AngularTolerance = 1e-8;
        // Unification is restricted to each existing solid independently.
        bool MergeSameDomain = false;
        // 0 uses the bounded CAD Task pool; 1 executes the same jobs serially.
        std::size_t MaximumConcurrency = 0;
    };

    struct SCadBRepHealFace final
    {
        std::uint64_t SourceFaceId = 0;
        std::vector<std::uint64_t> ResultFaceIds;
        std::string SourceType, ResultType, Status, Reason;
        std::string AreaMethod = "OCC adaptive surface integral";
        double SupportDeviation = 0, BoundaryDeviation = 0, AreaDifference = 0;
    };

    struct SCadBRepHealResult final
    {
        bool Success = false;
        // A rejected conversion batch returns the validated original geometry.
        // Success describes that returned geometry, not conversion coverage.
        bool RejectedChanges = false;
        GeometryData::BRepModel Geometry;
        std::vector<SCadBRepHealFace> Faces;
        std::vector<std::string> Diagnostics;
        std::size_t ConvertedFaces = 0, MergedFaces = 0;
        std::size_t NormalizationWorkers = 0;
        double InputVolume = 0, OutputVolume = 0, VolumeError = 0, VolumeAllowance = 0;
        std::string VolumeMethod = "OCC adaptive solid integral";
    };

    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadBRepHealService : public Services::IService
    {
    public:
        virtual SCadBRepHealResult Heal(const GeometryData::BRepModel& geometry,
            const SCadBRepHealOptions& options = {}, const std::function<bool()>& isCancelled = {}) const = 0;
    };

    // Neutral in-memory geometry operation; callers own publication and policy.
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP CCadBRepHealService final : public ICadBRepHealService
    {
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        SCadBRepHealResult Heal(const GeometryData::BRepModel& geometry,
            const SCadBRepHealOptions& options = {}, const std::function<bool()>& isCancelled = {}) const override;
    };
}
