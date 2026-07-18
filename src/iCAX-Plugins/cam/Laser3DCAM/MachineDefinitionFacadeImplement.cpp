#include "pch.h"
#include "MachineDefinitionFacadeImplement.h"
#include "FacadeSupport.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace iCAX::CAM::Facades::Internal
{
namespace
{
    constexpr const char* kMachineDefinitionCatalogInitializedKey = "machineDefinitionsInitialized";
    constexpr const char* kBuiltInGantryDefinitionID = "11111111-1111-4111-8111-111111111111";

    std::filesystem::path _PathFromUTF8(IN const std::string& strPath_)
    {
        std::u8string _Text(strPath_.begin(), strPath_.end());
        return std::filesystem::path(_Text);
    }

    std::string _PathToUTF8(IN const std::filesystem::path& Path_)
    {
        const auto _Text = Path_.u8string();
        return std::string(_Text.begin(), _Text.end());
    }

    std::string _GetStringField(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN const std::string& strDefault_ = std::string())
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::runtime_error("CAM machine definition field must be a string: " + strName_);
        }
        return _Iter->second.To<std::string>();
    }

    bool _GetBoolField(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN const bool bDefault_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return bDefault_;
        }
        if (!_Iter->second.Is<bool>())
        {
            throw std::runtime_error("CAM machine definition field must be a bool: " + strName_);
        }
        return _Iter->second.To<bool>();
    }

    std::string _LowerExtension(IN const std::filesystem::path& Path_)
    {
        auto _Extension = Path_.extension().string();
        std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](IN unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return _Extension;
    }

    std::string _FormatFromPath(IN const std::string& strPath_)
    {
        const auto _Extension = _LowerExtension(_PathFromUTF8(strPath_));
        if (_Extension == ".sdf" || _Extension == ".xml")
        {
            return "SDF";
        }
        if (_Extension == ".urdf")
        {
            return "URDF";
        }
        return {};
    }

    std::string _NormalizeExtension(IN std::string strExtension_)
    {
        std::transform(strExtension_.begin(), strExtension_.end(), strExtension_.begin(), [](IN unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        if (!strExtension_.empty() && strExtension_.front() != '.')
        {
            strExtension_.insert(strExtension_.begin(), '.');
        }
        return strExtension_;
    }

    VariantArray _ReadStringArrayField(
        IN const ObjectMap& Object_,
        IN const std::string& strName_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return {};
        }
        if (!_Iter->second.Is<VariantArray>())
        {
            throw std::runtime_error("CAM product capability field must be an array: " + strName_);
        }

        VariantArray _Result;
        for (const auto& _Item : _Iter->second.To<VariantArray>())
        {
            if (!_Item.Is<std::string>())
            {
                throw std::runtime_error("CAM product capability array item must be a string: " + strName_);
            }
            _Result.emplace_back(_Item.To<std::string>());
        }
        return _Result;
    }

    ObjectMap _GetMachineDefinitionCapability(
        IN const iCAX::Product::IProductContext& ProductContext_)
    {
        const auto& _Capabilities = ProductContext_.GetDefinition().Capabilities;
        const auto _Iter = _Capabilities.find("machineDefinition");
        if (_Iter == _Capabilities.end() || _Iter->second.Is<std::monostate>())
        {
            throw std::runtime_error("Product capability is missing: machineDefinition");
        }
        if (!_Iter->second.Is<ObjectMap>())
        {
            throw std::runtime_error("Product capability must be an object: machineDefinition");
        }
        return _Iter->second.To<ObjectMap>();
    }

    ObjectMap _NormalizeMachineDefinitionFormatCapability(
        IN const ObjectMap& Format_)
    {
        ObjectMap _Format;
        _Format["formatId"] = _GetStringField(Format_, "formatId");
        _Format["name"] = _GetStringField(Format_, "name", _GetStringField(Format_, "formatId"));

        VariantArray _Extensions;
        for (const auto& _ExtensionValue : _ReadStringArrayField(Format_, "extensions"))
        {
            const auto _Extension = _NormalizeExtension(_ExtensionValue.To<std::string>());
            if (!_Extension.empty())
            {
                _Extensions.emplace_back(_Extension);
            }
        }
        _Format["extensions"] = _Extensions;

        if (_GetStringField(_Format, "formatId").empty())
        {
            throw std::runtime_error("Product machineDefinition.supportedFormats item requires formatId");
        }
        if (_Extensions.empty())
        {
            throw std::runtime_error("Product machineDefinition.supportedFormats item requires extensions");
        }
        return _Format;
    }

    bool _IsProductMachineDefinitionObject(IN const Variant& Value_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return false;
        }
        const auto _Object = Value_.To<ObjectMap>();
        return !_GetStringField(_Object, "id").empty();
    }

    VariantArray _ReadProductMachineDefinitionArray(IN const iCAX::Data::PropertyBag& Settings_)
    {
        const auto _Value = Settings_.Get(kMachineDefinitionsSettingKey);
        if (_Value.Is<std::monostate>())
        {
            return {};
        }
        if (!_Value.Is<VariantArray>())
        {
            throw std::runtime_error("CAM product machineDefinitions setting must be an array");
        }

        VariantArray _Definitions;
        for (const auto& _Item : _Value.To<VariantArray>())
        {
            if (_IsProductMachineDefinitionObject(_Item))
            {
                _Definitions.emplace_back(_Item);
            }
        }
        return _Definitions;
    }

    std::string _ReadDefaultProductMachineDefinitionID(IN const iCAX::Data::PropertyBag& Settings_)
    {
        const auto _Value = Settings_.Get(kDefaultMachineDefinitionSettingKey);
        if (_Value.Is<std::monostate>())
        {
            return {};
        }
        if (!_Value.Is<std::string>())
        {
            throw std::runtime_error("CAM defaultMachineDefinitionId setting must be a string");
        }
        return _Value.To<std::string>();
    }

    void _WriteProductMachineDefinitions(
        IN iCAX::Product::IProductContext& ProductContext_,
        IN VariantArray Definitions_,
        IN std::string strDefaultDefinitionID_)
    {
        iCAX::Data::PropertyBag _Settings = ProductContext_.GetSettings();
        _Settings.Set(kMachineDefinitionsSettingKey, Definitions_);
        _Settings.Set(kDefaultMachineDefinitionSettingKey, strDefaultDefinitionID_);
        _Settings.Set(kMachineDefinitionCatalogInitializedKey, true);
        ProductContext_.ReplaceSettings(_Settings);
    }

    std::filesystem::path _GetProductMachineDefinitionRoot(
        IN const iCAX::Application::IApplicationContext& ApplicationContext_,
        IN const iCAX::Product::IProductContext& ProductContext_)
    {
        std::filesystem::path _Root = ApplicationContext_.GetPaths().UserConfigDirectory.empty()
            ? std::filesystem::path("Setting")
            : _PathFromUTF8(ApplicationContext_.GetPaths().UserConfigDirectory);
        return _Root / "Products" / ProductContext_.GetProductID() / "MachineDefinitions";
    }

    std::filesystem::path _ResolveBuiltInGantryMachinePath(
        IN const iCAX::Application::IApplicationContext& ApplicationContext_)
    {
        std::vector<std::filesystem::path> _Roots;
        if (!ApplicationContext_.GetPaths().InstallDirectory.empty())
        {
            auto _Install = _PathFromUTF8(ApplicationContext_.GetPaths().InstallDirectory);
            for (auto _Path = _Install; !_Path.empty(); _Path = _Path.parent_path())
            {
                _Roots.emplace_back(_Path);
                if (_Path == _Path.parent_path())
                {
                    break;
                }
            }
        }

        auto _Current = std::filesystem::current_path();
        for (auto _Path = _Current; !_Path.empty(); _Path = _Path.parent_path())
        {
            _Roots.emplace_back(_Path);
            if (_Path == _Path.parent_path())
            {
                break;
            }
        }

        auto _SourceFile = std::filesystem::path(__FILE__);
        for (auto _Path = _SourceFile.parent_path(); !_Path.empty(); _Path = _Path.parent_path())
        {
            _Roots.emplace_back(_Path);
            if (_Path == _Path.parent_path())
            {
                break;
            }
        }

        for (const auto& _Root : _Roots)
        {
            const auto _Candidate = _Root / "apps" / "laser-3d-cam" / "resources" / "machines" / "icax-gantry-ac5" / "model.sdf";
            if (std::filesystem::exists(_Candidate))
            {
                return std::filesystem::weakly_canonical(_Candidate);
            }
        }
        return {};
    }

    void _CopyDirectoryTree(
        IN const std::filesystem::path& SourceDirectory_,
        IN const std::filesystem::path& TargetDirectory_)
    {
        if (!std::filesystem::exists(SourceDirectory_) || !std::filesystem::is_directory(SourceDirectory_))
        {
            throw std::runtime_error("CAM machine definition source directory does not exist: " + _PathToUTF8(SourceDirectory_));
        }

        const auto _SourceCanonical = std::filesystem::weakly_canonical(SourceDirectory_);
        const auto _TargetCanonical = std::filesystem::weakly_canonical(TargetDirectory_);
        if (_SourceCanonical == _TargetCanonical)
        {
            return;
        }

        std::filesystem::create_directories(TargetDirectory_);
        for (const auto& _Entry : std::filesystem::recursive_directory_iterator(SourceDirectory_))
        {
            const auto _Relative = std::filesystem::relative(_Entry.path(), SourceDirectory_);
            const auto _Target = TargetDirectory_ / _Relative;
            if (_Entry.is_directory())
            {
                std::filesystem::create_directories(_Target);
            }
            else if (_Entry.is_regular_file())
            {
                std::filesystem::create_directories(_Target.parent_path());
                std::filesystem::copy_file(_Entry.path(), _Target, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

}

std::string _MakeProductMachineDefinitionManagedPath(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_,
    IN const std::string& strSourcePath_)
{
    if (strDefinitionID_.empty())
    {
        throw std::invalid_argument("CAM product machine definition id cannot be empty");
    }
    const auto _Source = _PathFromUTF8(strSourcePath_);
    return _PathToUTF8(_GetProductMachineDefinitionRoot(ApplicationContext_, ProductContext_) / strDefinitionID_ / _Source.filename());
}

ObjectMap _MakeProductMachineDefinition(
    IN const std::string& strDefinitionID_,
    IN const std::string& strName_,
    IN const std::string& strSourcePath_,
    IN const std::string& strManagedPath_,
    IN const bool bEnabled_,
    IN const bool bDefault_)
{
    if (strDefinitionID_.empty())
    {
        throw std::invalid_argument("CAM product machine definition id cannot be empty");
    }

    ObjectMap _Definition;
    _Definition["id"] = strDefinitionID_;
    _Definition["entityId"] = strDefinitionID_;
    _Definition["name"] = strName_.empty() ? _GetDisplayNameFromPath(strSourcePath_) : strName_;
    _Definition["sourcePath"] = strSourcePath_;
    _Definition["managedPath"] = strManagedPath_;
    _Definition["format"] = _FormatFromPath(strManagedPath_.empty() ? strSourcePath_ : strManagedPath_);
    _Definition["formatVersion"] = std::string();
    _Definition["modelName"] = _Definition["name"];
    _Definition["enabled"] = bEnabled_;
    _Definition["isDefault"] = bDefault_;
    _Definition["definitionScope"] = std::string("product");
    _Definition["linkCount"] = 0ull;
    _Definition["jointCount"] = 0ull;
    _Definition["visualCount"] = 0ull;
    _Definition["collisionCount"] = 0ull;
    _Definition["includeCount"] = 0ull;
    return _Definition;
}

VariantArray _GetProductMachineDefinitionArray(IN iCAX::Product::IProductContext& ProductContext_)
{
    const auto _Settings = ProductContext_.GetSettings();
    const auto _DefaultID = _ReadDefaultProductMachineDefinitionID(_Settings);
    auto _Definitions = _ReadProductMachineDefinitionArray(_Settings);
    for (auto& _Value : _Definitions)
    {
        auto _Object = _Value.To<ObjectMap>();
        _Object["isDefault"] = !_DefaultID.empty() && _GetStringField(_Object, "id") == _DefaultID;
        _Value = _Object;
    }
    return _Definitions;
}

VariantArray _GetProductMachineDefinitionSupportedFormats(
    IN const iCAX::Product::IProductContext& ProductContext_)
{
    const auto _Capability = _GetMachineDefinitionCapability(ProductContext_);
    const auto _Iter = _Capability.find("supportedFormats");
    if (_Iter == _Capability.end() || _Iter->second.Is<std::monostate>())
    {
        throw std::runtime_error("Product capability is missing: machineDefinition.supportedFormats");
    }
    if (!_Iter->second.Is<VariantArray>())
    {
        throw std::runtime_error("Product capability must be an array: machineDefinition.supportedFormats");
    }

    VariantArray _Formats;
    for (const auto& _Value : _Iter->second.To<VariantArray>())
    {
        if (!_Value.Is<ObjectMap>())
        {
            throw std::runtime_error("Product machineDefinition.supportedFormats item must be an object");
        }
        _Formats.emplace_back(_NormalizeMachineDefinitionFormatCapability(_Value.To<ObjectMap>()));
    }
    if (_Formats.empty())
    {
        throw std::runtime_error("Product machineDefinition.supportedFormats cannot be empty");
    }
    return _Formats;
}

bool _IsSupportedProductMachineDefinitionPath(
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strSourcePath_)
{
    const auto _Extension = _LowerExtension(_PathFromUTF8(strSourcePath_));
    if (_Extension.empty())
    {
        return false;
    }

    for (const auto& _FormatValue : _GetProductMachineDefinitionSupportedFormats(ProductContext_))
    {
        const auto _Format = _FormatValue.To<ObjectMap>();
        const auto _ExtensionsIter = _Format.find("extensions");
        if (_ExtensionsIter == _Format.end() || !_ExtensionsIter->second.Is<VariantArray>())
        {
            continue;
        }
        for (const auto& _ExtensionValue : _ExtensionsIter->second.To<VariantArray>())
        {
            if (_ExtensionValue.Is<std::string>() && _NormalizeExtension(_ExtensionValue.To<std::string>()) == _Extension)
            {
                return true;
            }
        }
    }
    return false;
}

ObjectMap _FindProductMachineDefinition(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_)
{
    if (strDefinitionID_.empty())
    {
        return {};
    }

    for (const auto& _Value : _GetProductMachineDefinitionArray(ProductContext_))
    {
        auto _Object = _Value.To<ObjectMap>();
        if (_GetStringField(_Object, "id") == strDefinitionID_)
        {
            return _Object;
        }
    }
    return {};
}

VariantArray _UpsertProductMachineDefinition(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const ObjectMap& Definition_)
{
    const auto _ID = _GetStringField(Definition_, "id");
    if (_ID.empty())
    {
        throw std::invalid_argument("CAM product machine definition id cannot be empty");
    }

    auto _Settings = ProductContext_.GetSettings();
    auto _Definitions = _ReadProductMachineDefinitionArray(_Settings);
    auto _DefaultID = _ReadDefaultProductMachineDefinitionID(_Settings);
    bool _bUpdated = false;
    for (auto& _Value : _Definitions)
    {
        auto _Object = _Value.To<ObjectMap>();
        if (_GetStringField(_Object, "id") == _ID)
        {
            _Value = Definition_;
            _bUpdated = true;
            break;
        }
    }
    if (!_bUpdated)
    {
        _Definitions.emplace_back(Definition_);
    }
    if (_DefaultID.empty() || _GetBoolField(Definition_, "isDefault", false))
    {
        _DefaultID = _ID;
    }

    _WriteProductMachineDefinitions(ProductContext_, _Definitions, _DefaultID);
    return _GetProductMachineDefinitionArray(ProductContext_);
}

VariantArray _SetProductMachineDefinitionEnabled(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_,
    IN bool bEnabled_)
{
    auto _Settings = ProductContext_.GetSettings();
    auto _Definitions = _ReadProductMachineDefinitionArray(_Settings);
    auto _DefaultID = _ReadDefaultProductMachineDefinitionID(_Settings);
    bool _bFound = false;
    for (auto& _Value : _Definitions)
    {
        auto _Object = _Value.To<ObjectMap>();
        if (_GetStringField(_Object, "id") == strDefinitionID_)
        {
            _Object["enabled"] = bEnabled_;
            _Value = _Object;
            _bFound = true;
            break;
        }
    }
    if (!_bFound)
    {
        throw std::runtime_error("CAM product machine definition is not found: " + strDefinitionID_);
    }
    _WriteProductMachineDefinitions(ProductContext_, _Definitions, _DefaultID);
    return _GetProductMachineDefinitionArray(ProductContext_);
}

VariantArray _DeleteProductMachineDefinition(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_)
{
    auto _Settings = ProductContext_.GetSettings();
    auto _Definitions = _ReadProductMachineDefinitionArray(_Settings);
    auto _DefaultID = _ReadDefaultProductMachineDefinitionID(_Settings);
    VariantArray _Kept;
    bool _bDeleted = false;
    for (const auto& _Value : _Definitions)
    {
        auto _Object = _Value.To<ObjectMap>();
        if (_GetStringField(_Object, "id") == strDefinitionID_)
        {
            _bDeleted = true;
            continue;
        }
        _Kept.emplace_back(_Value);
    }
    if (!_bDeleted)
    {
        throw std::runtime_error("CAM product machine definition is not found: " + strDefinitionID_);
    }
    if (_DefaultID == strDefinitionID_)
    {
        _DefaultID.clear();
        if (!_Kept.empty())
        {
            _DefaultID = _GetStringField(_Kept.front().To<ObjectMap>(), "id");
        }
    }
    _WriteProductMachineDefinitions(ProductContext_, _Kept, _DefaultID);
    return _GetProductMachineDefinitionArray(ProductContext_);
}

VariantArray _SetDefaultProductMachineDefinition(
    IN iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_)
{
    bool _bFound = false;
    for (const auto& _Value : _GetProductMachineDefinitionArray(ProductContext_))
    {
        if (_GetStringField(_Value.To<ObjectMap>(), "id") == strDefinitionID_)
        {
            _bFound = true;
            break;
        }
    }
    if (!_bFound)
    {
        throw std::runtime_error("CAM product machine definition is not found: " + strDefinitionID_);
    }

    auto _Settings = ProductContext_.GetSettings();
    auto _Definitions = _ReadProductMachineDefinitionArray(_Settings);
    _WriteProductMachineDefinitions(ProductContext_, _Definitions, strDefinitionID_);
    return _GetProductMachineDefinitionArray(ProductContext_);
}

std::string _EnsureProductMachineDefinitionFiles(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN const std::string& strDefinitionID_,
    IN const std::string& strSourcePath_)
{
    const auto _Source = _PathFromUTF8(strSourcePath_);
    if (!std::filesystem::exists(_Source) || !std::filesystem::is_regular_file(_Source))
    {
        throw std::runtime_error("CAM machine definition source file does not exist: " + strSourcePath_);
    }
    const auto _SourceDirectory = _Source.parent_path();
    const auto _TargetDirectory = _GetProductMachineDefinitionRoot(ApplicationContext_, ProductContext_) / strDefinitionID_;
    _CopyDirectoryTree(_SourceDirectory, _TargetDirectory);
    return _PathToUTF8(_TargetDirectory / _Source.filename());
}

void _EnsureBuiltInProductMachineDefinitions(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext& ProductContext_)
{
    auto _Settings = ProductContext_.GetSettings();
    const auto _Initialized = _Settings.Get(kMachineDefinitionCatalogInitializedKey);
    if (_Initialized.Is<bool>() && _Initialized.To<bool>())
    {
        return;
    }

    auto _Definitions = _ReadProductMachineDefinitionArray(_Settings);
    auto _DefaultID = _ReadDefaultProductMachineDefinitionID(_Settings);
    const auto _BuiltInPath = _ResolveBuiltInGantryMachinePath(ApplicationContext_);
    if (!_BuiltInPath.empty() && std::filesystem::exists(_BuiltInPath))
    {
        const auto _BuiltInPathText = _PathToUTF8(_BuiltInPath);
        bool _bExists = false;
        for (const auto& _Value : _Definitions)
        {
            if (_GetStringField(_Value.To<ObjectMap>(), "id") == kBuiltInGantryDefinitionID)
            {
                _bExists = true;
                break;
            }
        }
        if (!_bExists)
        {
            _Definitions.emplace_back(_MakeProductMachineDefinition(
                kBuiltInGantryDefinitionID,
                "icax_gantry_ac5_machine",
                _BuiltInPathText,
                _BuiltInPathText,
                true,
                true));
        }
        if (_DefaultID.empty())
        {
            _DefaultID = kBuiltInGantryDefinitionID;
        }
    }

    _WriteProductMachineDefinitions(ProductContext_, _Definitions, _DefaultID);
}
} // namespace iCAX::CAM::Facades::Internal
