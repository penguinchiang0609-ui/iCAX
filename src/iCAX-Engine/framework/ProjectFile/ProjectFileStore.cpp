#include "pch.h"

#include "ProjectFileStore.h"
#include "ProjectMigrationRegistration.h"

#include "Database/IRepository.h"
#include "Database/IMetaRegistry.h"
#include "Resources/ResourceLibrary.h"

namespace
{
    using namespace iCAX::ProjectFile;

    void RequireValidDefinition(
        IN const CProjectFileDefinition& Definition_)
    {
        if (Definition_.Magic.empty() ||
            Definition_.ProductID.empty() ||
            Definition_.CurrentFormatVersion.empty() ||
            Definition_.nCurrentFormatRevision == 0)
        {
            throw std::invalid_argument(
                "Project file definition is incomplete");
        }
        if (Definition_.DefaultEncoding !=
                EProjectFileEncoding::ASCII &&
            Definition_.DefaultEncoding !=
                EProjectFileEncoding::Binary)
        {
            throw std::invalid_argument(
                "Project file default encoding is invalid");
        }
    }

    iCAX::Resource::CResourceReference ToRuntimeReference(
        IN const CProjectResourceReference& Reference_)
    {
        return {Reference_.URL, Reference_.nVersion};
    }

    CProjectResourceReference ToProjectReference(
        IN const iCAX::Resource::CResourceReference& Reference_)
    {
        return {Reference_.URL, Reference_.nVersion};
    }

    CProjectResourceRecord ToProjectResource(
        IN const iCAX::Resource::CResourcePersistentPayload& Payload_)
    {
        const auto& _Info = Payload_.Info;
        CProjectResourceRecord _Record;
        _Record.Reference = {
            _Info.Key.Source,
            _Info.nVersion};
        _Record.ResourceTypeID = _Info.ResourceTypeID;
        _Record.nSchemaVersion = _Info.nSchemaVersion;
        _Record.Persistence = _Info.IsEmbedded()
            ? EProjectResourcePersistence::Embedded
            : EProjectResourcePersistence::External;
        _Record.Name = _Info.Name;
        _Record.MediaType = _Info.MediaType;
        _Record.FlatBufferIdentifier =
            _Info.FlatBufferIdentifier;
        _Record.ContentHash = _Info.ContentHash;
        _Record.Source = _Info.Source;
        _Record.nSize = _Info.IsEmbedded()
            ? static_cast<uint64_t>(Payload_.Body.size())
            : _Info.nSize;
        _Record.nMinimumReaderVersion =
            _Info.nMinimumReaderVersion;
        _Record.nFlags = _Info.nFlags;
        _Record.Metadata = _Info.Metadata;
        _Record.Body = Payload_.Body;
        for (const auto& _Dependency : _Info.Dependencies)
        {
            _Record.Dependencies.push_back(
                ToProjectReference(_Dependency));
        }
        return _Record;
    }

    iCAX::Resource::CResourcePersistentPayload ToRuntimeResource(
        IN const CProjectResourceRecord& Record_)
    {
        iCAX::Resource::CResourcePersistentPayload _Payload;
        auto& _Info = _Payload.Info;
        _Info.Key = iCAX::Resource::MakeResourceKeyFromSource(
            Record_.Reference.URL);
        _Info.Name = Record_.Name;
        _Info.Source = Record_.Source;
        _Info.MediaType = Record_.MediaType;
        _Info.ResourceTypeID = Record_.ResourceTypeID;
        _Info.FlatBufferIdentifier =
            Record_.FlatBufferIdentifier;
        _Info.ContentHash = Record_.ContentHash;
        _Info.nVersion = Record_.Reference.nVersion;
        _Info.nSize =
            Record_.Persistence ==
                EProjectResourcePersistence::Embedded
            ? static_cast<uint64_t>(Record_.Body.size())
            : Record_.nSize;
        _Info.nSchemaVersion = Record_.nSchemaVersion;
        _Info.nMinimumReaderVersion =
            Record_.nMinimumReaderVersion;
        _Info.nFlags = Record_.nFlags;
        _Info.Persistence =
            Record_.Persistence ==
                EProjectResourcePersistence::Embedded
            ? iCAX::Resource::EResourcePersistenceMode::Embedded
            : iCAX::Resource::EResourcePersistenceMode::External;
        _Info.Metadata = Record_.Metadata;
        for (const auto& _Dependency : Record_.Dependencies)
        {
            _Info.Dependencies.push_back(
                ToRuntimeReference(_Dependency));
        }
        _Payload.Body = Record_.Body;
        return _Payload;
    }

    CProjectDocument CollectDocument(
        IN CProjectDocumentInfo Info_,
        IN const CProjectFileDefinition& Definition_,
        IN const iCAX::Database::IRepository& Database_,
        IN const iCAX::Resource::CResourceLibrary& Resources_)
    {
        Info_.Magic = Definition_.Magic;
        Info_.ProductID = Definition_.ProductID;
        Info_.FormatVersion =
            Definition_.CurrentFormatVersion;
        Info_.nFormatRevision =
            Definition_.nCurrentFormatRevision;
        if (Info_.MainSceneID.is_nil())
        {
            Info_.MainSceneID = Database_.GetID();
        }
        if (Info_.MainSceneID != Database_.GetID())
        {
            throw std::invalid_argument(
                "Project main scene id does not match Database id");
        }

        CProjectDocument _Document;
        _Document.Info = std::move(Info_);
        const auto _pMeta = Database_.GetMetaRegistry();
        if (!_pMeta)
        {
            throw std::runtime_error(
                "Database has no meta registry");
        }

        for (const auto& _EntityID : Database_.GetEntityIDs())
        {
            const auto _pEntity = Database_.GetEntity(_EntityID);
            if (!_pEntity)
            {
                throw std::runtime_error(
                    "Database entity disappeared while saving");
            }
            _Document.Entities.push_back({_EntityID});
            for (const auto& _ComponentClass :
                _pEntity->GetComponentClasses())
            {
                const auto _pComponent =
                    _pEntity->GetComponent(_ComponentClass);
                if (!_pComponent)
                {
                    throw std::runtime_error(
                        "Database component disappeared while saving");
                }

                CProjectComponentRecord _Record;
                _Record.EntityID = _EntityID;
                _Record.ComponentClass = _ComponentClass;
                _Record.bEnabled = _pComponent->IsEnable();
                const auto _Properties =
                    _pComponent->GetProperties();
                for (const auto& _PropertyName :
                    _pMeta->GetPropertyNames(_ComponentClass))
                {
                    if (_pMeta->GetPropertyPersistenceByName(
                            _ComponentClass,
                            _PropertyName) !=
                        iCAX::Database::EPropertyPersistence::Persistent)
                    {
                        continue;
                    }
                    const auto _Property =
                        _Properties.find(_PropertyName);
                    if (_Property != _Properties.end())
                    {
                        _Record.Properties.emplace(
                            _PropertyName,
                            _Property->second);
                    }
                }
                _Document.Components.push_back(
                    std::move(_Record));
            }
        }

        std::vector<iCAX::Resource::CResourceReference> _Roots;
        for (const auto& _Info : Resources_.GetManifest(false))
        {
            if (_Info.nVersion == 0)
            {
                throw std::runtime_error(
                    "Persistent resource has no version: " +
                    _Info.Key.Source);
            }
            _Roots.push_back({
                _Info.Key.Source,
                _Info.nVersion});
        }
        const auto _Reachable =
            Resources_.CollectReachable(_Roots);
        if (!_Reachable.IsComplete())
        {
            const auto& _Missing = _Reachable.Missing.front();
            throw std::runtime_error(
                "Persistent resource dependency is missing: " +
                _Missing.URL + "@" +
                std::to_string(_Missing.nVersion));
        }
        for (const auto& _Info : _Reachable.Resources)
        {
            _Document.Resources.push_back(ToProjectResource(
                Resources_.SerializePersistentVersion({
                    _Info.Key.Source,
                    _Info.nVersion})));
        }
        _Document.Canonicalize();
        RequireValidProjectDocument(_Document);
        return _Document;
    }

    std::vector<size_t> ResourceRestoreOrder(
        IN const CProjectDocument& Document_)
    {
        const auto _Count = Document_.Resources.size();
        std::map<CProjectResourceReference, size_t> _Indices;
        for (size_t i = 0; i < _Count; ++i)
        {
            _Indices.emplace(
                Document_.Resources[i].Reference,
                i);
        }

        std::vector<std::set<size_t>> _Outgoing(_Count);
        std::vector<size_t> _InDegree(_Count, 0);
        const auto _AddEdge =
            [&_Outgoing, &_InDegree](size_t From_, size_t To_)
            {
                if (_Outgoing[From_].insert(To_).second)
                {
                    ++_InDegree[To_];
                }
            };
        for (size_t i = 0; i < _Count; ++i)
        {
            for (const auto& _Dependency :
                Document_.Resources[i].Dependencies)
            {
                _AddEdge(_Indices.at(_Dependency), i);
            }
        }

        std::map<std::string, std::vector<size_t>> _ByURL;
        for (size_t i = 0; i < _Count; ++i)
        {
            _ByURL[Document_.Resources[i].Reference.URL]
                .push_back(i);
        }
        for (auto& [_, _Versions] : _ByURL)
        {
            std::sort(
                _Versions.begin(),
                _Versions.end(),
                [&Document_](size_t Left_, size_t Right_)
                {
                    return Document_.Resources[Left_]
                        .Reference.nVersion <
                        Document_.Resources[Right_]
                        .Reference.nVersion;
                });
            for (size_t i = 1; i < _Versions.size(); ++i)
            {
                _AddEdge(_Versions[i - 1], _Versions[i]);
            }
        }

        std::set<std::pair<CProjectResourceReference, size_t>> _Ready;
        for (size_t i = 0; i < _Count; ++i)
        {
            if (_InDegree[i] == 0)
            {
                _Ready.emplace(
                    Document_.Resources[i].Reference,
                    i);
            }
        }

        std::vector<size_t> _Order;
        while (!_Ready.empty())
        {
            const auto i = _Ready.begin()->second;
            _Ready.erase(_Ready.begin());
            _Order.push_back(i);
            for (const auto _Next : _Outgoing[i])
            {
                if (--_InDegree[_Next] == 0)
                {
                    _Ready.emplace(
                        Document_.Resources[_Next].Reference,
                        _Next);
                }
            }
        }
        if (_Order.size() != _Count)
        {
            throw std::invalid_argument(
                "Resource dependencies conflict with version order");
        }
        return _Order;
    }

    void RequireEmptyTargets(
        IN const iCAX::Database::IRepository& Database_,
        IN const iCAX::Resource::CResourceLibrary& Resources_)
    {
        const auto _EntityIDs = Database_.GetEntityIDs();
        const auto _pMetaEntity =
            Database_.GetEntity(Database_.GetID());
        if (_EntityIDs.size() != 1 ||
            _EntityIDs.front() != Database_.GetID() ||
            !_pMetaEntity ||
            _pMetaEntity->ComponentsCount() != 0)
        {
            throw std::invalid_argument(
                "Project file Open requires an empty Database");
        }
        if (Resources_.Count() != 0)
        {
            throw std::invalid_argument(
                "Project file Open requires an empty ResourceLibrary");
        }
    }

    void RestoreDatabase(
        IN const CProjectDocument& Document_,
        IN OUT iCAX::Database::IRepository& Database_)
    {
        bool _bLoading = false;
        try
        {
            Database_.BeginLoadBaseline();
            _bLoading = true;
            for (const auto& _Entity : Document_.Entities)
            {
                if (_Entity.EntityID != Database_.GetID())
                {
                    Database_.CreateEntity(_Entity.EntityID);
                }
            }
            for (const auto& _Component : Document_.Components)
            {
                const auto _pEntity =
                    Database_.GetEntity(_Component.EntityID);
                if (!_pEntity)
                {
                    throw std::runtime_error(
                        "Project component entity is unavailable during load");
                }
                auto _pRuntimeComponent =
                    _pEntity->AddComponent(
                        _Component.ComponentClass);
                std::string _strError;
                if (!_pRuntimeComponent->SetProperties(
                        _Component.Properties,
                        _strError))
                {
                    throw std::runtime_error(
                        _strError.empty()
                        ? "Project component properties could not be restored"
                        : _strError);
                }
                if (!_Component.bEnabled)
                {
                    _pRuntimeComponent->Disable();
                }
            }
            Database_.EndLoadBaseline();
            _bLoading = false;
        }
        catch (...)
        {
            if (_bLoading)
            {
                try
                {
                    Database_.CancelLoadBaseline();
                }
                catch (...)
                {
                }
            }
            throw;
        }
    }
}

iCAX::ProjectFile::CProjectFile::CProjectFile(
    IN CProjectFileDefinition Definition_)
    : m_Definition(std::move(Definition_))
    , m_pMigrations(
        std::make_unique<CProjectMigrationRegistry>())
{
    RequireValidDefinition(m_Definition);
    if (m_Definition.MigrationModulePaths.empty())
    {
        CProjectMigrationRegistrationCatalog::ReplayAll(
            *m_pMigrations);
    }
    else
    {
        CProjectMigrationRegistrationCatalog::ReplayByModulePaths(
            *m_pMigrations,
            m_Definition.MigrationModulePaths);
    }
}

iCAX::ProjectFile::CProjectFile::~CProjectFile() = default;

iCAX::ProjectFile::CProjectMigrationRegistry&
iCAX::ProjectFile::CProjectFile::Migrations() noexcept
{
    return *m_pMigrations;
}

iCAX::ProjectFile::CPreparedProjectOpen::CPreparedProjectOpen() = default;
iCAX::ProjectFile::CPreparedProjectOpen::~CPreparedProjectOpen() = default;
iCAX::ProjectFile::CPreparedProjectOpen::CPreparedProjectOpen(
    IN CPreparedProjectOpen&&) noexcept = default;
iCAX::ProjectFile::CPreparedProjectOpen&
iCAX::ProjectFile::CPreparedProjectOpen::operator=(
    IN CPreparedProjectOpen&&) noexcept = default;

const iCAX::ProjectFile::CProjectOpenResult&
iCAX::ProjectFile::CPreparedProjectOpen::Result() const noexcept
{
    return m_Result;
}

iCAX::ProjectFile::CPreparedProjectOpen
iCAX::ProjectFile::CProjectFile::PrepareOpen(
    IN const std::filesystem::path& Path_) const
{
    const auto _Read = CProjectFileCodec::Read(Path_);
    if (_Read.Document.Info.Magic != m_Definition.Magic)
    {
        throw std::invalid_argument(
            "Project file magic does not match this product");
    }
    if (_Read.Document.Info.ProductID != m_Definition.ProductID)
    {
        throw std::invalid_argument(
            "Project file belongs to another product");
    }

    CProjectMigrationContext _MigrationContext;
    auto _Document = m_pMigrations->UpgradeToCurrent(
        _Read.Document,
        m_Definition.nCurrentFormatRevision,
        m_Definition.CurrentFormatVersion,
        &_MigrationContext);

    CPreparedProjectOpen _Prepared;
    _Prepared.m_Result.Info = _Document.Info;
    _Prepared.m_Result.SourceEncoding = _Read.Encoding;
    _Prepared.m_Result.MigrationDiagnostics =
        std::move(_MigrationContext.Diagnostics);
    _Prepared.m_pDocument =
        std::make_unique<CProjectDocument>(std::move(_Document));
    return _Prepared;
}

void iCAX::ProjectFile::CProjectFile::Restore(
    IN const CPreparedProjectOpen& Prepared_,
    IN OUT iCAX::Database::IRepository& Database_,
    IN OUT iCAX::Resource::CResourceLibrary& Resources_) const
{
    if (!Prepared_.m_pDocument)
    {
        throw std::invalid_argument(
            "Prepared project open session is empty");
    }
    const auto& _Document = *Prepared_.m_pDocument;
    if (_Document.Info.Magic != m_Definition.Magic ||
        _Document.Info.ProductID != m_Definition.ProductID ||
        _Document.Info.nFormatRevision !=
            m_Definition.nCurrentFormatRevision)
    {
        throw std::invalid_argument(
            "Prepared project open session does not match this project file definition");
    }
    if (_Document.Info.MainSceneID != Database_.GetID())
    {
        throw std::invalid_argument(
            "Project main scene id does not match Database id");
    }
    RequireEmptyTargets(Database_, Resources_);

    try
    {
        for (const auto i : ResourceRestoreOrder(_Document))
        {
            Resources_.RestorePersistentVersion(
                ToRuntimeResource(_Document.Resources[i]));
        }
        RestoreDatabase(_Document, Database_);
    }
    catch (...)
    {
        Resources_.Clear();
        throw;
    }
}

void iCAX::ProjectFile::CProjectFile::Save(
    IN const std::filesystem::path& Path_,
    IN CProjectDocumentInfo Info_,
    IN const iCAX::Database::IRepository& Database_,
    IN const iCAX::Resource::CResourceLibrary& Resources_) const
{
    Save(
        Path_,
        std::move(Info_),
        Database_,
        Resources_,
        m_Definition.DefaultEncoding);
}

void iCAX::ProjectFile::CProjectFile::Save(
    IN const std::filesystem::path& Path_,
    IN CProjectDocumentInfo Info_,
    IN const iCAX::Database::IRepository& Database_,
    IN const iCAX::Resource::CResourceLibrary& Resources_,
    IN const EProjectFileEncoding Encoding_) const
{
    CProjectFileCodec::WriteAtomic(
        Path_,
        CollectDocument(
            std::move(Info_),
            m_Definition,
            Database_,
            Resources_),
        Encoding_);
}

iCAX::ProjectFile::CProjectOpenResult
iCAX::ProjectFile::CProjectFile::Open(
    IN const std::filesystem::path& Path_,
    IN OUT iCAX::Database::IRepository& Database_,
    IN OUT iCAX::Resource::CResourceLibrary& Resources_) const
{
    auto _Prepared = PrepareOpen(Path_);
    Restore(_Prepared, Database_, Resources_);
    return _Prepared.Result();
}
