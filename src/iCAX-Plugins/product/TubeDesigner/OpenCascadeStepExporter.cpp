#include "pch.h"

#include "GeometryData/GeometryData.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "Resources/ResourceImportExport.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourceLoaderRegistry.h"

#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>

namespace
{
    constexpr const char* kStepFormatID = "cad.step";

    std::filesystem::path Utf8Path(const std::string& Value_)
    {
        const std::u8string _Text(
            reinterpret_cast<const char8_t*>(Value_.data()), Value_.size());
        return std::filesystem::path(_Text);
    }

    std::string Utf8PathText(const std::filesystem::path& Value_)
    {
        const auto _Text = Value_.u8string();
        return { reinterpret_cast<const char*>(_Text.data()), _Text.size() };
    }

    bool HasStepExtension(const std::string& Path_)
    {
        auto _Extension = Utf8PathText(Utf8Path(Path_).extension());
        std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](unsigned char Character_) {
            return static_cast<char>(std::tolower(Character_));
        });
        return _Extension == ".step" || _Extension == ".stp";
    }

    std::uint64_t GetRequestedResourceVersion(
        const iCAX::Resource::CResourceExportRequest& Request_)
    {
        const auto _Version = Request_.Options.find("resourceVersion");
        if (_Version == Request_.Options.end() || _Version->second.empty()) return 0;
        std::size_t _Consumed = 0;
        const auto _Parsed = std::stoull(_Version->second, &_Consumed);
        if (_Consumed != _Version->second.size() || _Parsed == 0)
        {
            throw std::invalid_argument("STEP export resourceVersion must be a positive integer");
        }
        return _Parsed;
    }

    std::shared_ptr<iCAX::GeometryData::BRepModel> GetRequestedBRep(
        const iCAX::Resource::CResourceLibrary& Library_,
        const iCAX::Resource::CResourceExportRequest& Request_)
    {
        const auto _Version = GetRequestedResourceVersion(Request_);
        return _Version == 0
            ? Library_.Get<iCAX::GeometryData::BRepModel>(Request_.ResourceID)
            : Library_.Get<iCAX::GeometryData::BRepModel>(Request_.ResourceID, _Version);
    }

    class COpenCascadeStepExporter final : public iCAX::Resource::IResourceExporter
    {
    public:
        std::vector<iCAX::Resource::CResourceFormatDescriptor> GetExportFormats() const override
        {
            return { { kStepFormatID, "STEP CAD", { ".step", ".stp" }, {}, false, true } };
        }

        bool CanExport(
            const iCAX::Resource::CResourceLibrary& Library_,
            const iCAX::Resource::CResourceExportRequest& Request_) const override
        {
            if (Request_.ResourceID.empty() || Request_.TargetPath.empty()) return false;
            if (!Request_.FormatID.empty() && Request_.FormatID != kStepFormatID) return false;
            if (!Request_.SourceResourceTypeName.empty()
                && Request_.SourceResourceTypeName != iCAX::GeometryData::BRepModel::kResourceTypeName)
            {
                return false;
            }
            return HasStepExtension(Request_.TargetPath)
                && static_cast<bool>(GetRequestedBRep(Library_, Request_));
        }

        iCAX::Resource::CResourceExportResult Export(
            const iCAX::Resource::CResourceLibrary& Library_,
            const iCAX::Resource::CResourceExportRequest& Request_) override
        {
            try
            {
                const auto _BRep = GetRequestedBRep(Library_, Request_);
                if (!_BRep)
                {
                    return iCAX::Resource::CResourceExportResult::Invalid(Request_, "STEP export requires a loaded BRep resource");
                }
                const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(*_BRep);
                if (!_Built.bOK || _Built.Shape.IsNull())
                {
                    return iCAX::Resource::CResourceExportResult::Failed(Request_, "STEP export could not rebuild the OpenCascade shape");
                }
                const auto _Target = Utf8Path(Request_.TargetPath);
                if (_Target.has_parent_path()) std::filesystem::create_directories(_Target.parent_path());
                STEPControl_Writer _Writer;
                if (_Writer.Transfer(_Built.Shape, STEPControl_AsIs) != IFSelect_RetDone)
                {
                    return iCAX::Resource::CResourceExportResult::Failed(Request_, "STEP writer rejected the BRep shape");
                }
                const auto _TargetText = Utf8PathText(_Target);
                if (_Writer.Write(_TargetText.c_str()) != IFSelect_RetDone)
                {
                    return iCAX::Resource::CResourceExportResult::Failed(Request_, "STEP writer could not write the target file");
                }
                return iCAX::Resource::CResourceExportResult::Succeeded(
                    _TargetText, kStepFormatID, { Request_.ResourceID });
            }
            catch (const std::exception& Error_)
            {
                return iCAX::Resource::CResourceExportResult::Failed(Request_, Error_.what());
            }
        }
    };
}

ICAX_REGISTER_RESOURCE_EXPORTER_PROVIDER("icax.tube-designer.occ-step", COpenCascadeStepExporter)
