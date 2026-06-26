#include "pch.h"
#include "ProductManifest.h"

#include <boost/json.hpp>
#include <boost/json/src.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace
{
    namespace json = boost::json;

    std::string _ReadAllText(IN const std::filesystem::path& Path_)
    {
        std::ifstream _Input(Path_, std::ios::binary);
        if (!_Input)
        {
            throw std::runtime_error("Cannot open product manifest: " + Path_.string());
        }

        std::ostringstream _Buffer;
        _Buffer << _Input.rdbuf();
        return _Buffer.str();
    }

    const json::object& _RequireObject(IN const json::value& Value_, IN const std::string& strName_)
    {
        if (!Value_.is_object())
        {
            throw std::runtime_error("Manifest field must be object: " + strName_);
        }
        return Value_.as_object();
    }

    const json::object& _RequireChildObject(
        IN const json::object& Object_,
        IN const std::string& strName_)
    {
        const auto* _pValue = Object_.if_contains(strName_);
        if (!_pValue || !_pValue->is_object())
        {
            throw std::runtime_error("Manifest field must be object: " + strName_);
        }
        return _pValue->as_object();
    }

    std::string _RequireString(
        IN const json::object& Object_,
        IN const std::string& strName_)
    {
        const auto* _pValue = Object_.if_contains(strName_);
        if (!_pValue || !_pValue->is_string())
        {
            throw std::runtime_error("Manifest field must be string: " + strName_);
        }
        return std::string(_pValue->as_string().c_str());
    }

    std::string _OptionalString(
        IN const json::object& Object_,
        IN const std::string& strName_,
        IN const std::string& strDefault_ = {})
    {
        const auto* _pValue = Object_.if_contains(strName_);
        if (!_pValue || _pValue->is_null())
        {
            return strDefault_;
        }
        if (!_pValue->is_string())
        {
            throw std::runtime_error("Manifest field must be string: " + strName_);
        }
        return std::string(_pValue->as_string().c_str());
    }

    uint32_t _OptionalUInt32(
        IN const json::object& Object_,
        IN const std::string& strName_,
        IN uint32_t nDefault_)
    {
        const auto* _pValue = Object_.if_contains(strName_);
        if (!_pValue || _pValue->is_null())
        {
            return nDefault_;
        }
        if (!_pValue->is_int64() && !_pValue->is_uint64())
        {
            throw std::runtime_error("Manifest field must be integer: " + strName_);
        }

        const auto _Value = _pValue->is_uint64()
            ? _pValue->as_uint64()
            : static_cast<uint64_t>(_pValue->as_int64());
        if (_Value > UINT32_MAX)
        {
            throw std::runtime_error("Manifest integer field is out of uint32 range: " + strName_);
        }
        return static_cast<uint32_t>(_Value);
    }

    uint64_t _OptionalUInt64(
        IN const json::object& Object_,
        IN const std::string& strName_,
        IN uint64_t nDefault_)
    {
        const auto* _pValue = Object_.if_contains(strName_);
        if (!_pValue || _pValue->is_null())
        {
            return nDefault_;
        }
        if (_pValue->is_uint64())
        {
            return _pValue->as_uint64();
        }
        if (_pValue->is_int64() && _pValue->as_int64() >= 0)
        {
            return static_cast<uint64_t>(_pValue->as_int64());
        }
        throw std::runtime_error("Manifest field must be unsigned integer: " + strName_);
    }

    std::vector<std::string> _OptionalStringArray(
        IN const json::object& Object_,
        IN const std::string& strName_)
    {
        std::vector<std::string> _Items;
        const auto* _pValue = Object_.if_contains(strName_);
        if (!_pValue || _pValue->is_null())
        {
            return _Items;
        }
        if (!_pValue->is_array())
        {
            throw std::runtime_error("Manifest field must be array: " + strName_);
        }

        for (const auto& _Item : _pValue->as_array())
        {
            if (!_Item.is_string())
            {
                throw std::runtime_error("Manifest array item must be string: " + strName_);
            }
            _Items.emplace_back(_Item.as_string().c_str());
        }
        return _Items;
    }

    std::string _ResolveBackendPath(
        IN const std::filesystem::path& ProductRoot_,
        IN const std::string& strPath_)
    {
        if (strPath_.empty())
        {
            return {};
        }

        std::filesystem::path _Path(strPath_);
        if (_Path.is_relative())
        {
            _Path = ProductRoot_ / _Path;
        }
        return std::filesystem::weakly_canonical(_Path).string();
    }

    std::vector<std::string> _ResolveBackendPaths(
        IN const std::filesystem::path& ProductRoot_,
        IN const std::vector<std::string>& Paths_)
    {
        std::vector<std::string> _Resolved;
        _Resolved.reserve(Paths_.size());
        for (const auto& _Path : Paths_)
        {
            _Resolved.emplace_back(_ResolveBackendPath(ProductRoot_, _Path));
        }
        return _Resolved;
    }

    void _AppendModuleArray(
        IN const json::object& Modules_,
        IN const std::string& strName_,
        IN const std::filesystem::path& ProductRoot_,
        IN OUT std::vector<std::string>& Target_)
    {
        auto _Items = _ResolveBackendPaths(ProductRoot_, _OptionalStringArray(Modules_, strName_));
        Target_.insert(Target_.end(), _Items.begin(), _Items.end());
    }
}

iCAX::Product::CProductManifest iCAX::Product::LoadProductManifest(IN const std::string& strManifestPath_)
{
    const std::filesystem::path _ManifestPath(strManifestPath_);
    const auto _ProductRoot = _ManifestPath.parent_path();

    boost::system::error_code _Error;
    auto _Value = json::parse(_ReadAllText(_ManifestPath), _Error);
    if (_Error)
    {
        throw std::runtime_error("Invalid product manifest json: " + _Error.message());
    }

    const auto& _Root = _RequireObject(_Value, "root");

    CProductManifest _Manifest;
    _Manifest.ManifestPath = std::filesystem::weakly_canonical(_ManifestPath).string();
    _Manifest.Schema = _OptionalString(_Root, "schema", "icax.product.manifest");
    _Manifest.nSchemaVersion = _OptionalUInt32(_Root, "schemaVersion", 1);

    auto& _Definition = _Manifest.Definition;
    _Definition.ProductID = _RequireString(_Root, "productId");
    _Definition.ProductName = _RequireString(_Root, "productName");
    _Definition.ProductVersion = _OptionalString(_Root, "productVersion");

    const auto& _ProjectFile = _RequireChildObject(_Root, "projectFile");
    _Definition.ProjectFile.Magic = _RequireString(_ProjectFile, "magic");
    _Definition.ProjectFile.FormatVersion = _OptionalString(_ProjectFile, "formatVersion");
    _Definition.ProjectFile.QuickSaveLogMagic = _OptionalString(_ProjectFile, "quickSaveLogMagic");
    _Definition.ProjectFile.QuickSaveLogVersion = _OptionalUInt32(_ProjectFile, "quickSaveLogVersion", 1);
    _Definition.ProjectFile.FileExtensions = _OptionalStringArray(_ProjectFile, "fileExtensions");
    _Definition.ProjectFile.MagicOffset = _OptionalUInt64(_ProjectFile, "magicOffset", 0);
    _Definition.ProjectFile.ProbeBytes = _OptionalUInt64(_ProjectFile, "probeBytes", 256);

    const auto& _Backend = _RequireChildObject(_Root, "backend");
    _Definition.DefaultProjectStartupComponent = _OptionalString(_Backend, "startupComponent");

    if (const auto* _pModules = _Backend.if_contains("modules"))
    {
        const auto& _Modules = _RequireObject(*_pModules, "backend.modules");
        _AppendModuleArray(_Modules, "components", _ProductRoot, _Definition.Modules.ComponentModules);
        _AppendModuleArray(_Modules, "behaviours", _ProductRoot, _Definition.Modules.BehaviourModules);
        _AppendModuleArray(_Modules, "behaviors", _ProductRoot, _Definition.Modules.BehaviourModules);
        _AppendModuleArray(_Modules, "services", _ProductRoot, _Definition.Modules.ServiceModules);
        _AppendModuleArray(_Modules, "commands", _ProductRoot, _Definition.Modules.CommandModules);
    }

    if (const auto* _pWebPage = _Root.if_contains("webpage"))
    {
        const auto& _WebPage = _RequireObject(*_pWebPage, "webpage");
        _Definition.FrontendEntry = _RequireString(_WebPage, "entry");
    }

    if (!iCAX::Product::IsValidProductID(_Definition.ProductID))
    {
        throw std::runtime_error("Product manifest productId is invalid: " + _Definition.ProductID);
    }
    if (_Definition.ProjectFile.Magic.empty())
    {
        throw std::runtime_error("Product manifest projectFile.magic cannot be empty: " + _Definition.ProductID);
    }
    if (_Definition.FrontendEntry.empty())
    {
        throw std::runtime_error("Product manifest webpage.entry cannot be empty: " + _Definition.ProductID);
    }
    return _Manifest;
}

std::vector<iCAX::Product::CProductDefinition> iCAX::Product::LoadProductDefinitions(
    IN const std::string& strProductRoot_)
{
    std::vector<iCAX::Product::CProductDefinition> _Definitions;
    if (strProductRoot_.empty())
    {
        return _Definitions;
    }

    const std::filesystem::path _ProductRoot(strProductRoot_);
    if (!std::filesystem::exists(_ProductRoot))
    {
        throw std::runtime_error("Product root does not exist: " + _ProductRoot.string());
    }

    for (const auto& _Entry : std::filesystem::directory_iterator(_ProductRoot))
    {
        if (!_Entry.is_directory())
        {
            continue;
        }

        const auto _ManifestPath = _Entry.path() / "product.manifest.json";
        if (!std::filesystem::exists(_ManifestPath))
        {
            continue;
        }

        _Definitions.push_back(LoadProductManifest(_ManifestPath.string()).Definition);
    }
    return _Definitions;
}
