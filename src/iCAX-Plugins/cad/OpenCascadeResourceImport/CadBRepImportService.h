#pragma once

#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace iCAX::CAD
{
    struct SCadBRepImportRequest final
    {
        std::filesystem::path SourcePath;
        double DisplayDeflectionMm = 0.1;
        // Downstream modelling may need exact topology only. Its final result
        // supplies the display mesh; do not tessellate an intermediate source.
        bool IncludeDisplayMesh = true;
        std::uint64_t MaximumSourceBytes = 512ull * 1024 * 1024;
        // Thread-safe, nonblocking. No Scene/Repository capture is permitted.
        std::function<bool()> IsCancelled;
    };

    struct SCadBRepImportResult final
    {
        iCAX::GeometryData::BRepModel Geometry;
        iCAX::GeometryData::BoundingBox3 Bounds;
        std::vector<std::uint8_t> SourceBytes;
        std::string SourceSha256;
        std::string FormatId;
        std::string DisplayName;
        std::vector<std::string> SourceLengthUnits;
        std::string GeometryLengthUnit = "mm";
        std::uint32_t TransferredRoots = 0;
        bool IsValidBRep = false;
        bool SourceIsValidBRep = false;
        // Only source-visible objects/occurrences/layers are imported. Hidden
        // support curves/surfaces required by a visible face remain dependencies.
        // The retained shape hierarchy is in Geometry.RootShapes/Compounds.
        // STEP product names/colors/PMI are not inferred from this geometry tree.
        std::vector<std::string> Diagnostics;
    };

    // Decode and validate a frozen source file without publishing resources or
    // creating business objects. The product owns resource versions/transactions.
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadBRepImportService
        : public iCAX::Services::IService
    {
    public:
        ~ICadBRepImportService() override = default;
        virtual SCadBRepImportResult Load(const SCadBRepImportRequest& Request_) const = 0;
    };
}
