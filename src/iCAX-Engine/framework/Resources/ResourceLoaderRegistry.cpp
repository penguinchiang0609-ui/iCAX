#include "pch.h"
#include "ResourceLoaderRegistry.h"


namespace
{
    std::string _ToLower(IN std::string Text_)
    {
        std::transform(Text_.begin(), Text_.end(), Text_.begin(), [](IN const unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return Text_;
    }

    std::string _GetExtension(IN const std::string& strPath_)
    {
        const auto _QueryPos = strPath_.find_first_of("?#");
        const auto _CleanEnd = _QueryPos == std::string::npos ? strPath_.size() : _QueryPos;
        const auto _SlashPos = strPath_.find_last_of("/\\", _CleanEnd == 0 ? 0 : _CleanEnd - 1);
        const auto _DotPos = strPath_.find_last_of('.', _CleanEnd == 0 ? 0 : _CleanEnd - 1);
        if (_DotPos == std::string::npos || (_SlashPos != std::string::npos && _DotPos < _SlashPos))
        {
            return {};
        }
        return _ToLower(strPath_.substr(_DotPos, _CleanEnd - _DotPos));
    }

    bool _HasExtension(IN const iCAX::Resource::CResourceFormatDescriptor& Format_, IN const std::string& strExtension_)
    {
        if (strExtension_.empty())
        {
            return false;
        }
        return std::any_of(Format_.Extensions.begin(), Format_.Extensions.end(), [&strExtension_](IN const std::string& strCandidate_) {
            return _ToLower(strCandidate_) == strExtension_;
        });
    }

    bool _MatchesRuleExtension(IN const std::vector<std::string>& Extensions_, IN const std::string& strExtension_)
    {
        if (Extensions_.empty())
        {
            return true;
        }
        return std::any_of(Extensions_.begin(), Extensions_.end(), [&strExtension_](IN const std::string& strCandidate_) {
            return _ToLower(strCandidate_) == strExtension_;
        });
    }

    bool _MatchesRuleText(IN const std::string& strRuleValue_, IN const std::string& strActualValue_)
    {
        return strRuleValue_.empty() || strRuleValue_ == strActualValue_;
    }

    bool _MatchesImportFormat(
        IN const iCAX::Resource::IResourceImporter& Importer_,
        IN const iCAX::Resource::CResourceImportRequest& Request_)
    {
        const auto _Formats = Importer_.GetImportFormats();
        if (!Request_.FormatID.empty())
        {
            return std::any_of(_Formats.begin(), _Formats.end(), [&Request_](IN const iCAX::Resource::CResourceFormatDescriptor& Format_) {
                return Format_.bCanImport && Format_.FormatID == Request_.FormatID;
            });
        }

        const auto _Extension = _GetExtension(Request_.SourcePath);
        return std::any_of(_Formats.begin(), _Formats.end(), [&_Extension](IN const iCAX::Resource::CResourceFormatDescriptor& Format_) {
            return Format_.bCanImport && _HasExtension(Format_, _Extension);
        });
    }

    bool _MatchesImporterRule(
        IN const iCAX::Resource::CResourceLoaderRegistry::CHandlerSelectionRule& Rule_,
        IN const iCAX::Resource::CResourceImportRequest& Request_,
        IN const iCAX::Resource::CResourceLoaderRegistry::CImporterEntry& Entry_)
    {
        if (Rule_.Kind != "importer" && Rule_.Kind != "import")
        {
            return false;
        }
        const auto _TypeName = Request_.TargetResourceTypeName;
        if (!Rule_.ResourceTypeName.empty() && Rule_.ResourceTypeName != _TypeName)
        {
            return false;
        }
        if (!Rule_.FormatID.empty() && Rule_.FormatID != Request_.FormatID)
        {
            const auto _Formats = Entry_.Importer ? Entry_.Importer->GetImportFormats() : std::vector<iCAX::Resource::CResourceFormatDescriptor>();
            const auto _HasFormat = std::any_of(_Formats.begin(), _Formats.end(), [&Rule_](IN const iCAX::Resource::CResourceFormatDescriptor& Format_) {
                return Format_.bCanImport && Format_.FormatID == Rule_.FormatID;
            });
            if (!_HasFormat)
            {
                return false;
            }
        }
        if (!_MatchesRuleExtension(Rule_.Extensions, _GetExtension(Request_.SourcePath)))
        {
            return false;
        }
        return _MatchesRuleText(Rule_.ProviderID, Entry_.ProviderID)
            && _MatchesRuleText(_ToLower(Rule_.ModulePath), Entry_.ModulePath);
    }

    bool _MatchesExportFormat(
        IN const iCAX::Resource::IResourceExporter& Exporter_,
        IN const iCAX::Resource::CResourceExportRequest& Request_)
    {
        const auto _Formats = Exporter_.GetExportFormats();
        if (!Request_.FormatID.empty())
        {
            return std::any_of(_Formats.begin(), _Formats.end(), [&Request_](IN const iCAX::Resource::CResourceFormatDescriptor& Format_) {
                return Format_.bCanExport && Format_.FormatID == Request_.FormatID;
            });
        }

        const auto _Extension = _GetExtension(Request_.TargetPath);
        return std::any_of(_Formats.begin(), _Formats.end(), [&_Extension](IN const iCAX::Resource::CResourceFormatDescriptor& Format_) {
            return Format_.bCanExport && _HasExtension(Format_, _Extension);
        });
    }

    bool _MatchesExporterRule(
        IN const iCAX::Resource::CResourceLoaderRegistry::CHandlerSelectionRule& Rule_,
        IN const iCAX::Resource::CResourceExportRequest& Request_,
        IN const iCAX::Resource::CResourceLoaderRegistry::CExporterEntry& Entry_)
    {
        if (Rule_.Kind != "exporter" && Rule_.Kind != "export")
        {
            return false;
        }
        if (!Rule_.ResourceTypeName.empty() && Rule_.ResourceTypeName != Request_.SourceResourceTypeName)
        {
            return false;
        }
        if (!Rule_.FormatID.empty() && Rule_.FormatID != Request_.FormatID)
        {
            const auto _Formats = Entry_.Exporter ? Entry_.Exporter->GetExportFormats() : std::vector<iCAX::Resource::CResourceFormatDescriptor>();
            const auto _HasFormat = std::any_of(_Formats.begin(), _Formats.end(), [&Rule_](IN const iCAX::Resource::CResourceFormatDescriptor& Format_) {
                return Format_.bCanExport && Format_.FormatID == Rule_.FormatID;
            });
            if (!_HasFormat)
            {
                return false;
            }
        }
        if (!_MatchesRuleExtension(Rule_.Extensions, _GetExtension(Request_.TargetPath)))
        {
            return false;
        }
        return _MatchesRuleText(Rule_.ProviderID, Entry_.ProviderID)
            && _MatchesRuleText(_ToLower(Rule_.ModulePath), Entry_.ModulePath);
    }

    bool _MatchesLoaderRule(
        IN const iCAX::Resource::CResourceLoaderRegistry::CHandlerSelectionRule& Rule_,
        IN const iCAX::Resource::CResourceLoadContext& Context_,
        IN const iCAX::Resource::CResourceLoaderRegistry::CLoaderEntry& Entry_)
    {
        if (Rule_.Kind != "loader" && Rule_.Kind != "load")
        {
            return false;
        }
        if (!Rule_.ResourceTypeName.empty() && Rule_.ResourceTypeName != Context_.TargetResourceTypeName)
        {
            return false;
        }
        if (!Rule_.FormatID.empty())
        {
            const auto _Ite = Context_.Options.find("formatId");
            if (_Ite == Context_.Options.end() || _Ite->second != Rule_.FormatID)
            {
                return false;
            }
        }
        if (!_MatchesRuleExtension(Rule_.Extensions, _GetExtension(Context_.Source)))
        {
            return false;
        }
        return _MatchesRuleText(Rule_.ProviderID, Entry_.ProviderID)
            && _MatchesRuleText(_ToLower(Rule_.ModulePath), Entry_.ModulePath);
    }
}

bool iCAX::Resource::CResourceLoaderRegistry::RegisterLoader(IN const std::type_info& ResourceType_, IN const std::shared_ptr<IResourceLoader>& pLoader_)
{
    if (!pLoader_)
    {
        throw std::invalid_argument("Resource loader cannot be null");
    }
    return RegisterLoader(ResourceType_, pLoader_, typeid(*pLoader_).name(), {});
}

bool iCAX::Resource::CResourceLoaderRegistry::RegisterLoader(
    IN const std::type_info& ResourceType_,
    IN const std::shared_ptr<IResourceLoader>& pLoader_,
    IN const std::string& strProviderID_,
    IN const std::string& strModulePath_)
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
        if (_Loader.Loader == pLoader_)
        {
            return false;
        }
        if (_Loader.Loader && typeid(*_Loader.Loader) == typeid(*pLoader_))
        {
            return false;
        }
    }

    _Loaders.push_back({ pLoader_, strProviderID_, _ToLower(strModulePath_), std::type_index(ResourceType_), m_nNextRegistrationOrder++ });
    return true;
}

std::vector<std::shared_ptr<iCAX::Resource::IResourceLoader>> iCAX::Resource::CResourceLoaderRegistry::GetLoadersFor(IN const std::type_info& ResourceType_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_Loaders.find(std::type_index(ResourceType_));
    if (_Ite != m_Loaders.end())
    {
        std::vector<std::shared_ptr<IResourceLoader>> _Result;
        _Result.reserve(_Ite->second.size());
        for (const auto& _Entry : _Ite->second)
        {
            _Result.push_back(_Entry.Loader);
        }
        return _Result;
    }

    return {};
}

std::shared_ptr<iCAX::Resource::IResourceLoader> iCAX::Resource::CResourceLoaderRegistry::FindLoaderFor(IN const CResourceLoadContext& Context_) const
{
    if (Context_.TargetResourceType == std::type_index(typeid(void)))
    {
        return nullptr;
    }

    std::vector<CLoaderEntry> _Candidates;
    std::vector<CHandlerSelectionRule> _Rules;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        auto _Ite = m_Loaders.find(Context_.TargetResourceType);
        if (_Ite != m_Loaders.end())
        {
            _Candidates = _Ite->second;
        }
        _Rules = m_SelectionRules;
    }

    std::stable_sort(_Rules.begin(), _Rules.end(), [](IN const CHandlerSelectionRule& Left_, IN const CHandlerSelectionRule& Right_) {
        return Left_.Priority > Right_.Priority;
    });

    // 在锁外调用 CanLoad，避免 loader 内部间接注册或查询时造成注册表锁重入。
    for (const auto& _Rule : _Rules)
    {
        for (const auto& _Entry : _Candidates)
        {
            if (_Entry.Loader && _MatchesLoaderRule(_Rule, Context_, _Entry) && _Entry.Loader->CanLoad(Context_))
            {
                return _Entry.Loader;
            }
        }
    }

    for (const auto& _Entry : _Candidates)
    {
        if (_Entry.Loader && _Entry.Loader->CanLoad(Context_))
        {
            return _Entry.Loader;
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
    return RegisterImporter(pImporter_, typeid(*pImporter_).name(), {});
}

bool iCAX::Resource::CResourceLoaderRegistry::RegisterImporter(
    IN const std::shared_ptr<IResourceImporter>& pImporter_,
    IN const std::string& strProviderID_,
    IN const std::string& strModulePath_)
{
    if (!pImporter_)
    {
        throw std::invalid_argument("Resource importer cannot be null");
    }

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Importer : m_Importers)
    {
        if (_Importer.Importer == pImporter_)
        {
            return false;
        }
        if (_Importer.Importer && typeid(*_Importer.Importer) == typeid(*pImporter_))
        {
            return false;
        }
    }

    m_Importers.push_back({ pImporter_, strProviderID_, _ToLower(strModulePath_), m_nNextRegistrationOrder++ });
    return true;
}

bool iCAX::Resource::CResourceLoaderRegistry::RegisterExporter(IN const std::shared_ptr<IResourceExporter>& pExporter_)
{
    if (!pExporter_)
    {
        throw std::invalid_argument("Resource exporter cannot be null");
    }
    return RegisterExporter(pExporter_, typeid(*pExporter_).name(), {});
}

bool iCAX::Resource::CResourceLoaderRegistry::RegisterExporter(
    IN const std::shared_ptr<IResourceExporter>& pExporter_,
    IN const std::string& strProviderID_,
    IN const std::string& strModulePath_)
{
    if (!pExporter_)
    {
        throw std::invalid_argument("Resource exporter cannot be null");
    }

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Exporter : m_Exporters)
    {
        if (_Exporter.Exporter == pExporter_)
        {
            return false;
        }
        if (_Exporter.Exporter && typeid(*_Exporter.Exporter) == typeid(*pExporter_))
        {
            return false;
        }
    }

    m_Exporters.push_back({ pExporter_, strProviderID_, _ToLower(strModulePath_), m_nNextRegistrationOrder++ });
    return true;
}

void iCAX::Resource::CResourceLoaderRegistry::SetSelectionRules(IN std::vector<CHandlerSelectionRule> Rules_)
{
    for (auto& _Rule : Rules_)
    {
        _Rule.Kind = _ToLower(_Rule.Kind);
        _Rule.ModulePath = _ToLower(_Rule.ModulePath);
        for (auto& _Extension : _Rule.Extensions)
        {
            _Extension = _ToLower(_Extension);
        }
    }

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    m_SelectionRules = std::move(Rules_);
}

void iCAX::Resource::CResourceLoaderRegistry::AddSelectionRule(IN CHandlerSelectionRule Rule_)
{
    Rule_.Kind = _ToLower(Rule_.Kind);
    Rule_.ModulePath = _ToLower(Rule_.ModulePath);
    for (auto& _Extension : Rule_.Extensions)
    {
        _Extension = _ToLower(_Extension);
    }

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    m_SelectionRules.push_back(std::move(Rule_));
}

std::vector<iCAX::Resource::CResourceFormatDescriptor> iCAX::Resource::CResourceLoaderRegistry::GetImportFormats() const
{
    std::vector<CImporterEntry> _Importers;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        _Importers = m_Importers;
    }

    std::vector<CResourceFormatDescriptor> _Formats;
    for (const auto& _Importer : _Importers)
    {
        if (!_Importer.Importer)
        {
            continue;
        }
        auto _ImporterFormats = _Importer.Importer->GetImportFormats();
        _Formats.insert(_Formats.end(), _ImporterFormats.begin(), _ImporterFormats.end());
    }
    return _Formats;
}

std::vector<iCAX::Resource::CResourceFormatDescriptor> iCAX::Resource::CResourceLoaderRegistry::GetExportFormats() const
{
    std::vector<CExporterEntry> _Exporters;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        _Exporters = m_Exporters;
    }

    std::vector<CResourceFormatDescriptor> _Formats;
    for (const auto& _Exporter : _Exporters)
    {
        if (!_Exporter.Exporter)
        {
            continue;
        }
        auto _ExporterFormats = _Exporter.Exporter->GetExportFormats();
        _Formats.insert(_Formats.end(), _ExporterFormats.begin(), _ExporterFormats.end());
    }
    return _Formats;
}

std::shared_ptr<iCAX::Resource::IResourceImporter> iCAX::Resource::CResourceLoaderRegistry::FindImporterFor(IN const CResourceImportRequest& Request_) const
{
    std::vector<CImporterEntry> _Importers;
    std::vector<CHandlerSelectionRule> _Rules;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        _Importers = m_Importers;
        _Rules = m_SelectionRules;
    }

    std::stable_sort(_Rules.begin(), _Rules.end(), [](IN const CHandlerSelectionRule& Left_, IN const CHandlerSelectionRule& Right_) {
        return Left_.Priority > Right_.Priority;
    });

    for (const auto& _Rule : _Rules)
    {
        for (const auto& _Entry : _Importers)
        {
            if (_Entry.Importer && _MatchesImporterRule(_Rule, Request_, _Entry) && _Entry.Importer->CanImport(Request_))
            {
                return _Entry.Importer;
            }
        }
    }

    for (const auto& _Entry : _Importers)
    {
        if (_Entry.Importer && _MatchesImportFormat(*_Entry.Importer, Request_) && _Entry.Importer->CanImport(Request_))
        {
            return _Entry.Importer;
        }
    }

    for (const auto& _Entry : _Importers)
    {
        if (_Entry.Importer && _Entry.Importer->CanImport(Request_))
        {
            return _Entry.Importer;
        }
    }
    return nullptr;
}

std::shared_ptr<iCAX::Resource::IResourceExporter> iCAX::Resource::CResourceLoaderRegistry::FindExporterFor(
    IN const CResourceLibrary& Library_,
    IN const CResourceExportRequest& Request_) const
{
    std::vector<CExporterEntry> _Exporters;
    std::vector<CHandlerSelectionRule> _Rules;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        _Exporters = m_Exporters;
        _Rules = m_SelectionRules;
    }

    std::stable_sort(_Rules.begin(), _Rules.end(), [](IN const CHandlerSelectionRule& Left_, IN const CHandlerSelectionRule& Right_) {
        return Left_.Priority > Right_.Priority;
    });

    for (const auto& _Rule : _Rules)
    {
        for (const auto& _Entry : _Exporters)
        {
            if (_Entry.Exporter && _MatchesExporterRule(_Rule, Request_, _Entry) && _Entry.Exporter->CanExport(Library_, Request_))
            {
                return _Entry.Exporter;
            }
        }
    }

    for (const auto& _Entry : _Exporters)
    {
        if (_Entry.Exporter && _MatchesExportFormat(*_Entry.Exporter, Request_) && _Entry.Exporter->CanExport(Library_, Request_))
        {
            return _Entry.Exporter;
        }
    }

    for (const auto& _Entry : _Exporters)
    {
        if (_Entry.Exporter && _Entry.Exporter->CanExport(Library_, Request_))
        {
            return _Entry.Exporter;
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
