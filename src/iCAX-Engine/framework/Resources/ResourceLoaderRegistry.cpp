#include "pch.h"
#include "ResourceLoaderRegistry.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>

bool iCAX::Resource::CResourceLoaderRegistry::RegisterLoader(IN const std::type_info& ResourceType_, IN const std::shared_ptr<IResourceLoader>& pLoader_)
{
    if (!pLoader_)
    {
        throw std::invalid_argument("Resource loader cannot be null");
    }

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto& _Loaders = m_Loaders[std::type_index(ResourceType_)];
    for (const auto& _Loader : _Loaders)
    {
        // 同一实例或同一 loader 实现类型只保留一份，避免重复 CanLoad/Load。
        if (_Loader == pLoader_)
        {
            return false;
        }
        if (_Loader && typeid(*_Loader) == typeid(*pLoader_))
        {
            return false;
        }
    }

    _Loaders.push_back(pLoader_);
    return true;
}

std::vector<std::shared_ptr<iCAX::Resource::IResourceLoader>> iCAX::Resource::CResourceLoaderRegistry::GetLoadersFor(IN const std::type_info& ResourceType_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_Loaders.find(std::type_index(ResourceType_));
    if (_Ite != m_Loaders.end())
    {
        return _Ite->second;
    }

    return {};
}

std::shared_ptr<iCAX::Resource::IResourceLoader> iCAX::Resource::CResourceLoaderRegistry::FindLoaderFor(IN const CResourceLoadContext& Context_) const
{
    if (Context_.TargetResourceType == std::type_index(typeid(void)))
    {
        return nullptr;
    }

    std::vector<std::shared_ptr<IResourceLoader>> _Candidates;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        auto _Ite = m_Loaders.find(Context_.TargetResourceType);
        if (_Ite != m_Loaders.end())
        {
            _Candidates = _Ite->second;
        }
    }

    // 在锁外调用 CanLoad，避免 loader 内部间接注册或查询时造成注册表锁重入。
    for (const auto& _Loader : _Candidates)
    {
        if (_Loader && _Loader->CanLoad(Context_))
        {
            return _Loader;
        }
    }
    return nullptr;
}

iCAX::Resource::CResourceLoadResult iCAX::Resource::CResourceLoaderRegistry::LoadResource(IN const CResourceLoadContext& Context_)
{
    auto _Loader = FindLoaderFor(Context_);
    if (!_Loader)
    {
        return CResourceLoadResult::NoLoader(Context_.TargetKey);
    }

    auto _Result = _Loader->Load(Context_);
    if (!_Result.IsOK())
    {
        return _Result;
    }

    auto _Info = _Result.Info;
    if (!_Info.Key.IsValid())
    {
        _Info.Key = Context_.TargetKey;
    }
    if (!_Info.Key.IsValid())
    {
        return CResourceLoadResult::Invalid(Context_.TargetKey, "Loaded resource must have a valid key");
    }
    if (_Result.pResource && !_Result.RuntimeType.has_value())
    {
        return CResourceLoadResult::Invalid(_Info.Key, "Loaded resource object must carry runtime type");
    }

    _Result.Info = _Info;
    return _Result;
}

bool iCAX::Resource::CResourceLoaderRegistry::RegisterImporter(IN const std::shared_ptr<IResourceImporter>& pImporter_)
{
    if (!pImporter_)
    {
        throw std::invalid_argument("Resource importer cannot be null");
    }

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Importer : m_Importers)
    {
        if (_Importer == pImporter_)
        {
            return false;
        }
        if (_Importer && typeid(*_Importer) == typeid(*pImporter_))
        {
            return false;
        }
    }

    m_Importers.push_back(pImporter_);
    return true;
}

bool iCAX::Resource::CResourceLoaderRegistry::RegisterExporter(IN const std::shared_ptr<IResourceExporter>& pExporter_)
{
    if (!pExporter_)
    {
        throw std::invalid_argument("Resource exporter cannot be null");
    }

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Exporter : m_Exporters)
    {
        if (_Exporter == pExporter_)
        {
            return false;
        }
        if (_Exporter && typeid(*_Exporter) == typeid(*pExporter_))
        {
            return false;
        }
    }

    m_Exporters.push_back(pExporter_);
    return true;
}

std::vector<iCAX::Resource::CResourceFormatDescriptor> iCAX::Resource::CResourceLoaderRegistry::GetImportFormats() const
{
    std::vector<std::shared_ptr<IResourceImporter>> _Importers;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        _Importers = m_Importers;
    }

    std::vector<CResourceFormatDescriptor> _Formats;
    for (const auto& _Importer : _Importers)
    {
        if (!_Importer)
        {
            continue;
        }
        auto _ImporterFormats = _Importer->GetImportFormats();
        _Formats.insert(_Formats.end(), _ImporterFormats.begin(), _ImporterFormats.end());
    }
    return _Formats;
}

std::vector<iCAX::Resource::CResourceFormatDescriptor> iCAX::Resource::CResourceLoaderRegistry::GetExportFormats() const
{
    std::vector<std::shared_ptr<IResourceExporter>> _Exporters;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        _Exporters = m_Exporters;
    }

    std::vector<CResourceFormatDescriptor> _Formats;
    for (const auto& _Exporter : _Exporters)
    {
        if (!_Exporter)
        {
            continue;
        }
        auto _ExporterFormats = _Exporter->GetExportFormats();
        _Formats.insert(_Formats.end(), _ExporterFormats.begin(), _ExporterFormats.end());
    }
    return _Formats;
}

std::shared_ptr<iCAX::Resource::IResourceImporter> iCAX::Resource::CResourceLoaderRegistry::FindImporterFor(IN const CResourceImportRequest& Request_) const
{
    std::vector<std::shared_ptr<IResourceImporter>> _Importers;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        _Importers = m_Importers;
    }

    for (const auto& _Importer : _Importers)
    {
        if (_Importer && _Importer->CanImport(Request_))
        {
            return _Importer;
        }
    }
    return nullptr;
}

std::shared_ptr<iCAX::Resource::IResourceExporter> iCAX::Resource::CResourceLoaderRegistry::FindExporterFor(
    IN const CResourceLibrary& Library_,
    IN const CResourceExportRequest& Request_) const
{
    std::vector<std::shared_ptr<IResourceExporter>> _Exporters;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        _Exporters = m_Exporters;
    }

    for (const auto& _Exporter : _Exporters)
    {
        if (_Exporter && _Exporter->CanExport(Library_, Request_))
        {
            return _Exporter;
        }
    }
    return nullptr;
}

iCAX::Resource::CResourceImportResult iCAX::Resource::CResourceLoaderRegistry::ImportResource(
    IN CResourceLibrary& Library_,
    IN const CResourceImportRequest& Request_)
{
    auto _Importer = FindImporterFor(Request_);
    if (!_Importer)
    {
        return CResourceImportResult::NoHandler(Request_);
    }
    return _Importer->Import(Library_, Request_);
}

iCAX::Resource::CResourceExportResult iCAX::Resource::CResourceLoaderRegistry::ExportResource(
    IN const CResourceLibrary& Library_,
    IN const CResourceExportRequest& Request_)
{
    auto _Exporter = FindExporterFor(Library_, Request_);
    if (!_Exporter)
    {
        return CResourceExportResult::NoHandler(Request_);
    }
    return _Exporter->Export(Library_, Request_);
}
