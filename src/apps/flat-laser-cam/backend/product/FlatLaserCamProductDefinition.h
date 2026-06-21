#pragma once

#include "Product/ProductDefinition.h"

#include <filesystem>
#include <string>

namespace iCAX::FlatLaserCAM
{
    inline constexpr const char* KProductId = "icax.flat-laser-cam";
    inline constexpr const char* KProductName = "Flat Laser CAM";
    inline constexpr const char* KProductVersion = "0.1.0";
    inline constexpr const char* KProjectMagic = "ICAX_FLAT_LASER_CAM";
    inline constexpr const char* KProjectFileExtension = ".ilcam";
    inline constexpr const char* KFrontendEntry = "apps/flat-laser-cam/frontend/entry.mjs";
    inline constexpr const char* KDefaultStartupComponent = "CFlatLaserProjectComponent";

    inline std::string MakeModulePath(const std::filesystem::path& binaryRoot, const char* moduleName)
    {
        return (binaryRoot / moduleName).string();
    }

    inline iCAX::Product::CProductDefinition MakeFlatLaserCamProductDefinition(
        const std::filesystem::path& binaryRoot)
    {
        iCAX::Product::CProductDefinition definition;
        definition.ProductID = KProductId;
        definition.ProductName = KProductName;
        definition.ProductVersion = KProductVersion;
        definition.FrontendEntry = KFrontendEntry;
        definition.DefaultProjectStartupComponent = KDefaultStartupComponent;

        definition.ProjectFile.Magic = KProjectMagic;
        definition.ProjectFile.FormatVersion = 1;
        definition.ProjectFile.FileExtensions = { KProjectFileExtension };
        definition.ProjectFile.MagicOffset = 0;
        definition.ProjectFile.ProbeBytes = 64;

        definition.Modules.ComponentModules = {
            MakeModulePath(binaryRoot, "FlatLaserCamComponent.dll")
        };
        definition.Modules.BehaviourModules = {
            MakeModulePath(binaryRoot, "FlatLaserCamBehaviour.dll")
        };
        definition.Modules.ServiceModules = {
            MakeModulePath(binaryRoot, "FlatLaserCamService.dll")
        };
        definition.Modules.CommandModules = {
            MakeModulePath(binaryRoot, "FlatLaserCamCommand.dll")
        };

        return definition;
    }
}
