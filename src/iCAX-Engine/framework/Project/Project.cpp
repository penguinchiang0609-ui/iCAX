#include "pch.h"
#include "Project.h"
#include "Data/VariantSerializer.h"
#include "Database/OperationLog.h"
#include "ProjectFile/ProjectFileStore.h"
#include <fstream>
#include <tuple>


namespace
{
    std::shared_ptr<const iCAX::Application::IApplicationContext> RequireApplicationContext(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pApplicationContext)
        {
            throw std::invalid_argument("Project ApplicationContext cannot be null");
        }
        return CreateInfo_.pApplicationContext;
    }

    std::shared_ptr<iCAX::Product::IProductContext> RequireProductContext(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pProductContext)
        {
            throw std::invalid_argument("Project ProductContext cannot be null");
        }
        return CreateInfo_.pProductContext;
    }

    std::shared_ptr<iCAX::Services::CServiceProvider> RequireServiceProvider(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pServiceProvider)
        {
            throw std::invalid_argument("Project ServiceProvider cannot be null");
        }
        return CreateInfo_.pServiceProvider;
    }

    std::shared_ptr<iCAX::Database::IMetaRegistry> RequireMetaRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pMetaRegistry)
        {
            throw std::invalid_argument("Project MetaRegistry cannot be null");
        }
        return CreateInfo_.pMetaRegistry;
    }

    std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> RequireBehaviourRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pBehaviourRegistry)
        {
            throw std::invalid_argument("Project BehaviourRegistry cannot be null");
        }
        return CreateInfo_.pBehaviourRegistry;
    }

    std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> RequireResourceLoaderRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pResourceLoaderRegistry)
        {
            throw std::invalid_argument("Project ResourceLoaderRegistry cannot be null");
        }
        return CreateInfo_.pResourceLoaderRegistry;
    }

    std::shared_ptr<iCAX::Interaction::CSDOChannelRegistry> RequireSDOChannelRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pSDOChannelRegistry)
        {
            throw std::invalid_argument("Project SDOChannelRegistry cannot be null");
        }
        return CreateInfo_.pSDOChannelRegistry;
    }

    bool IsExternalPath(IN const std::string& strPath_)
    {
        return strPath_.find("://") != std::string::npos;
    }

    std::string MakeDefaultQuickSaveLogPath(IN const std::string& strProjectPath_)
    {
        if (strProjectPath_.empty() || IsExternalPath(strProjectPath_))
        {
            return {};
        }
        return strProjectPath_ + ".log";
    }

    std::filesystem::path PathFromUTF8(IN const std::string& strPath_)
    {
        std::u8string _Text(strPath_.begin(), strPath_.end());
        return std::filesystem::path(_Text);
    }

    bool SameLocalFile(IN const std::string& Left_, IN const std::string& Right_)
    {
        if (Left_.empty() || Right_.empty()) return Left_ == Right_;
        const auto _LeftPath = PathFromUTF8(Left_);
        const auto _RightPath = PathFromUTF8(Right_);
        std::error_code _Error;
        if (std::filesystem::equivalent(_LeftPath, _RightPath, _Error)) return true;
        const auto _Left = std::filesystem::weakly_canonical(_LeftPath).wstring();
        const auto _Right = std::filesystem::weakly_canonical(_RightPath).wstring();
        return CompareStringOrdinal(_Left.c_str(), static_cast<int>(_Left.size()),
            _Right.c_str(), static_cast<int>(_Right.size()), TRUE) == CSTR_EQUAL;
    }

    void EnsureParentDirectory(IN const std::string& strPath_)
    {
        const auto _Path = PathFromUTF8(strPath_);
        const auto _Parent = _Path.parent_path();
        if (!_Parent.empty())
        {
            std::filesystem::create_directories(_Parent);
        }
    }

    bool SameResourceDescriptions(const std::vector<iCAX::Resource::CResourceInfo>& left,
        const std::vector<iCAX::Resource::CResourceInfo>& right)
    {
        if (left.size() != right.size()) return false;
        const auto fields=[](const iCAX::Resource::CResourceInfo& info){return std::tie(info.Key.Source,info.ResourceID,info.ResourceTypeID,
            info.nVersion,info.Persistence,info.Name,info.Source,info.MediaType,info.FlatBufferIdentifier,info.ContentHash,info.nSize,
            info.nSchemaVersion,info.nMinimumReaderVersion,info.nFlags,info.Metadata,info.Dependencies);};
        for (size_t i=0;i<left.size();++i) if (fields(left[i]) != fields(right[i])) return false;
        return true;
    }

    void RestoreJournalInPlace(const std::filesystem::path& backup, const std::filesystem::path& target)
    {
        std::ifstream input(backup, std::ios::binary);
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!input || !output) throw std::runtime_error("Cannot restore aliased operation journal");
        output << input.rdbuf(); output.flush();
        if (input.bad() || !output) throw std::runtime_error("Cannot restore aliased operation journal bytes");
    }

    std::vector<iCAX::Resource::CResourceInfo> PersistentJournalResources(const iCAX::Resource::CResourceLibrary& resources)
    {
        std::vector<iCAX::Resource::CResourceReference> roots;
        for (const auto& info : resources.GetManifest(true))
            for (const auto version : resources.GetVersions(info.Key.Source))
            {
                const auto entry = resources.GetInfo(info.Key.Source, version);
                if (entry && entry->IsPersistent()) roots.push_back({info.Key.Source, version});
            }
        auto reachable = resources.CollectReachable(roots);
        if (!reachable.IsComplete()) throw std::runtime_error("Journal persistent resource dependency is missing");
        return std::move(reachable.Resources);
    }
}

iCAX::Project::CProject::CProject(IN const CProjectCreateInfo& CreateInfo_)
    : m_ProjectID(CreateInfo_.ProjectID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.ProjectID)
    , m_ProjectName(CreateInfo_.ProjectName)
    , m_ProjectPath(CreateInfo_.ProjectPath)
    , m_Settings(CreateInfo_.Settings)
    , m_QuickSaveLogPath(CreateInfo_.QuickSaveLogPath)
    , m_StartupComponent(CreateInfo_.StartupComponent)
    , m_pApplicationContext(RequireApplicationContext(CreateInfo_))
    , m_pProductContext(RequireProductContext(CreateInfo_))
    , m_pServiceProvider(RequireServiceProvider(CreateInfo_))
    , m_pMetaRegistry(RequireMetaRegistry(CreateInfo_))
    , m_pBehaviourRegistry(RequireBehaviourRegistry(CreateInfo_))
    , m_pResourceLoaderRegistry(RequireResourceLoaderRegistry(CreateInfo_))
    , m_Resources(m_pResourceLoaderRegistry)
    , m_pSDOChannelRegistry(RequireSDOChannelRegistry(CreateInfo_))
    , m_SceneFrameHandler(CreateInfo_.OnSceneFrame)
    , m_ResourceLibraryInitializer(CreateInfo_.ResourceLibraryInitializer)
{
    if (m_ResourceLibraryInitializer)
    {
        m_ResourceLibraryInitializer(m_Resources);
    }
    if (m_ProjectName.empty())
    {
        m_ProjectName = "Project";
    }
    m_Resources.SetScope(
        iCAX::Resource::MakeProjectResourceScope(
            m_pApplicationContext->GetDescriptor().AppID,
            m_pProductContext->GetProductID(),
            m_ProjectID));

    auto _SceneInfo = MakeMainSceneCreateInfo(CreateInfo_);
    auto _pMainScene = std::make_shared<CProjectScene>(*this, _SceneInfo);
    m_MainSceneID = _pMainScene->GetSceneID();
    m_Scenes.emplace(m_MainSceneID, _pMainScene);
}

iCAX::Project::CProject::~CProject()
{
    Close();
}

const iCAX::Data::uuid& iCAX::Project::CProject::GetProjectID() const
{
    return m_ProjectID;
}

const std::string& iCAX::Project::CProject::GetProjectName() const
{
    return m_ProjectName;
}

void iCAX::Project::CProject::SetProjectName(IN const std::string& strName_)
{
    m_ProjectName = strName_;
}

const std::string& iCAX::Project::CProject::GetProjectPath() const
{
    return m_ProjectPath;
}

void iCAX::Project::CProject::SetProjectPath(IN const std::string& strPath_)
{
    m_ProjectPath = strPath_;
}

iCAX::Data::PropertyBag& iCAX::Project::CProject::Settings()
{
    return m_Settings;
}

const iCAX::Data::PropertyBag& iCAX::Project::CProject::Settings() const
{
    return m_Settings;
}

std::string iCAX::Project::CProject::GetQuickSaveLogPath() const
{
    return !m_QuickSaveLogPath.empty()
        ? m_QuickSaveLogPath
        : MakeDefaultQuickSaveLogPath(m_ProjectPath);
}

void iCAX::Project::CProject::SetQuickSaveLogPath(IN const std::string& strPath_)
{
    m_QuickSaveLogPath = strPath_;
}

void iCAX::Project::CProject::ReplayQuickSaveLog(IN const std::string& strMagic_, IN uint32_t nVersion_)
{
    auto _LogPath = GetQuickSaveLogPath();
    const auto _FilePath = PathFromUTF8(_LogPath);
    if (_LogPath.empty() || !std::filesystem::exists(_FilePath) || std::filesystem::file_size(_FilePath) == 0)
    {
        return;
    }

    auto& _Repository = GetMainScene().Database();
    const auto _bWasLogOpen = _Repository.HasOperationLog();
    if (_bWasLogOpen)
    {
        _Repository.CloseOperationLog();
    }

    try
    {
        m_ActiveQuickSaveLogPath = _LogPath;
        m_QuickSaveMagic = strMagic_;
        m_nQuickSaveVersion = nVersion_;
        m_QuickSaveJournalID = iCAX::Database::COperationBatchJournal::ReadIdentity(_LogPath, strMagic_, nVersion_);
        const auto checkpoint = PathFromUTF8(_LogPath + ".resources");
        if (std::filesystem::exists(checkpoint))
        {
            iCAX::ProjectFile::CProjectResourceCheckpoint::Restore(checkpoint, QuickSaveResourceIdentity(), MainSceneResources());
            m_bQuickSaveResourceCheckpoint = true;
        }
        _Repository.ReplayOperationLog(_LogPath, strMagic_, nVersion_);
        if (_bWasLogOpen)
        {
            _Repository.OpenOperationLog(_LogPath, false, strMagic_, nVersion_);
        }
    }
    catch (...)
    {
        if (_bWasLogOpen)
        {
            try
            {
                _Repository.OpenOperationLog(_LogPath, false, strMagic_, nVersion_);
            }
            catch (...)
            {
            }
        }
        throw;
    }
}

void iCAX::Project::CProject::OpenQuickSaveLog(
    IN bool bTruncate_,
    IN const std::string& strMagic_,
    IN uint32_t nVersion_)
{
    auto _LogPath = GetQuickSaveLogPath();
    if (_LogPath.empty())
    {
        throw std::invalid_argument("Project quick save log path is empty");
    }

    EnsureParentDirectory(_LogPath);
    if (!std::filesystem::exists(PathFromUTF8(_LogPath)))
    {
        const auto orphan = PathFromUTF8(_LogPath + ".resources");
        if (std::filesystem::exists(orphan))
        {
            const auto info = iCAX::ProjectFile::CProjectFileCodec::Read(orphan).Document.Info;
            if (info.Magic != "ICAX_RESOURCE_JOURNAL" || info.ProductID != m_pProductContext->GetProductID()
                || info.ProjectID != GetProjectID() || info.MainSceneID != GetMainSceneID())
                throw std::runtime_error("Orphan resource checkpoint belongs to another project");
            // Preserve a retired generation as evidence; it cannot be
            // replayed or merged with the new append journal.
            std::filesystem::rename(orphan, PathFromUTF8(_LogPath + ".resources.orphan-"
                + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())));
        }
    }
    if (bTruncate_) ResetQuickSaveLog(_LogPath, strMagic_, nVersion_);
    else
    {
        GetMainScene().Database().OpenOperationLog(_LogPath, false, strMagic_, nVersion_);
        BindQuickSaveCheckpoint(_LogPath, strMagic_, nVersion_);
    }
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        m_bQuickSaveLogOpen = true;
    }
}

void iCAX::Project::CProject::MarkProjectFileSaved(
    IN const std::string& strProjectPath_,
    IN const std::string& strMagic_,
    IN uint32_t nVersion_)
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    const auto _PreviousLogPath = GetQuickSaveLogPath();
    auto _NewProjectPath = !strProjectPath_.empty() ? strProjectPath_ : m_ProjectPath;
    const auto _LogPath = !m_QuickSaveLogPath.empty()
        ? m_QuickSaveLogPath : MakeDefaultQuickSaveLogPath(_NewProjectPath);
    if (_LogPath.empty())
    {
        throw std::invalid_argument("Project quick save log path is empty");
    }

    auto& _Repository = GetMainScene().Database();
    const bool _WasLogOpen = _Repository.HasOperationLog();
    const bool _OwnsPreviousLog = m_bQuickSaveLogOpen && _WasLogOpen;
    const auto _PreviousIdentity = QuickSaveResourceIdentity();
    const auto _PreviousJournalID = m_QuickSaveJournalID;
    // The caller has already written and validated the complete new archive.
    // Keep the old path and journal intact until its replacement is writable.
    EnsureParentDirectory(_LogPath);
    const bool _SameLog = SameLocalFile(_PreviousLogPath, _LogPath);
    if (_OwnsPreviousLog)
    {
        if (iCAX::Database::COperationBatchJournal::ReadIdentity(_PreviousLogPath, strMagic_, nVersion_) != _PreviousJournalID)
            throw std::runtime_error("Cannot reset another project journal generation");
        const auto sidecar = PathFromUTF8(_PreviousLogPath + ".resources");
        if (std::filesystem::exists(sidecar) && iCAX::ProjectFile::CProjectFileCodec::Read(sidecar).Document.Info != _PreviousIdentity)
            throw std::runtime_error("Cannot reset another resource checkpoint generation");
    }
    _Repository.CloseOperationLog();
    m_bQuickSaveLogOpen = false;
    bool movedAliasBundle = false;
    try
    {
        // Hard-linked journal names still share one generation, but sidecar
        // names do not. Move the owned sidecar beside the selected alias
        // before resetting both; a failed reset moves it back below.
        if (_OwnsPreviousLog && _SameLog && !SameLocalFile(_PreviousLogPath + ".resources", _LogPath + ".resources")
            && std::filesystem::exists(PathFromUTF8(_PreviousLogPath + ".resources")))
        {
            if (std::filesystem::exists(PathFromUTF8(_LogPath + ".resources")))
                throw std::runtime_error("Journal aliases have conflicting resource checkpoints");
            std::filesystem::rename(PathFromUTF8(_PreviousLogPath + ".resources"), PathFromUTF8(_LogPath + ".resources"));
            movedAliasBundle = true;
        }
        ResetQuickSaveLog(_LogPath, strMagic_, nVersion_);
        if (_OwnsPreviousLog && !_SameLog)
        {
            // A successful Save As leaves the original archive unchanged.
            // Its old session journal must not replay the new file's edits.
            const auto oldLog = PathFromUTF8(_PreviousLogPath), oldBundle = PathFromUTF8(_PreviousLogPath + ".resources");
            const auto suffix = ".retired-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
            const auto retiredLog = PathFromUTF8(_PreviousLogPath + suffix);
            const auto retiredBundle = PathFromUTF8(_PreviousLogPath + ".resources" + suffix);
            bool movedBundle = false;
            if (std::filesystem::exists(oldBundle)) { std::filesystem::rename(oldBundle, retiredBundle); movedBundle = true; }
            try { std::filesystem::rename(oldLog, retiredLog); }
            catch (...)
            {
                if (movedBundle) std::filesystem::rename(retiredBundle, oldBundle);
                throw;
            }
            std::error_code ignored;
            std::filesystem::remove(retiredLog, ignored);
            if (movedBundle) std::filesystem::remove(retiredBundle, ignored);
        }
        m_ProjectPath.swap(_NewProjectPath);
        m_bQuickSaveLogOpen = true;
    }
    catch (...)
    {
        const auto _Failure = std::current_exception();
        _Repository.CloseOperationLog();
        if (movedAliasBundle && std::filesystem::exists(PathFromUTF8(_LogPath + ".resources")))
            std::filesystem::rename(PathFromUTF8(_LogPath + ".resources"), PathFromUTF8(_PreviousLogPath + ".resources"));
        if (_WasLogOpen)
        {
            _Repository.OpenOperationLog(_PreviousLogPath, false, strMagic_, nVersion_);
            BindQuickSaveCheckpoint(_PreviousLogPath, strMagic_, nVersion_);
            m_bQuickSaveLogOpen = true;
        }
        std::rethrow_exception(_Failure);
    }
}

void iCAX::Project::CProject::ValidateProjectFileSave(const std::string& projectPath,
    const std::string& magic, uint32_t version) const
{
    const auto logPath = !m_QuickSaveLogPath.empty() ? m_QuickSaveLogPath : MakeDefaultQuickSaveLogPath(projectPath);
    const auto log = PathFromUTF8(logPath), sidecar = PathFromUTF8(logPath + ".resources");
    if (std::filesystem::exists(log))
    {
        if (!SameLocalFile(logPath, m_ActiveQuickSaveLogPath) || m_QuickSaveJournalID.empty()
            || iCAX::Database::COperationBatchJournal::ReadIdentity(logPath, magic, version) != m_QuickSaveJournalID)
            throw std::runtime_error("Save target has another project journal generation");
    }
    if (std::filesystem::exists(sidecar)
        && (!SameLocalFile(logPath, m_ActiveQuickSaveLogPath)
            || iCAX::ProjectFile::CProjectFileCodec::Read(sidecar).Document.Info != QuickSaveResourceIdentity()))
        throw std::runtime_error("Save target has another resource checkpoint generation");
    // Also validate the source pair before Save As writes any target bytes.
    if (!m_ActiveQuickSaveLogPath.empty() && logPath != m_ActiveQuickSaveLogPath)
    {
        const auto current = PathFromUTF8(m_ActiveQuickSaveLogPath);
        if (std::filesystem::exists(current)
            && iCAX::Database::COperationBatchJournal::ReadIdentity(m_ActiveQuickSaveLogPath, magic, version) != m_QuickSaveJournalID)
            throw std::runtime_error("Source journal generation changed before Save As");
        const auto currentBundle = PathFromUTF8(m_ActiveQuickSaveLogPath + ".resources");
        if (std::filesystem::exists(currentBundle)
            && iCAX::ProjectFile::CProjectFileCodec::Read(currentBundle).Document.Info != QuickSaveResourceIdentity())
            throw std::runtime_error("Source resource checkpoint changed before Save As");
    }
}

void iCAX::Project::CProject::ResetQuickSaveLog(const std::string& path,
    const std::string& magic, uint32_t version)
{
    auto& db = MainSceneDatabase();
    const auto log = PathFromUTF8(path), sidecar = PathFromUTF8(path + ".resources");
    const bool hadLog = std::filesystem::exists(log);
    const bool hadSidecar = std::filesystem::exists(sidecar);
    if (hadLog)
    {
        if (!SameLocalFile(path, m_ActiveQuickSaveLogPath) || m_QuickSaveJournalID.empty()
            || iCAX::Database::COperationBatchJournal::ReadIdentity(path, magic, version) != m_QuickSaveJournalID)
            throw std::runtime_error("Cannot truncate another project journal generation");
    }
    if (hadSidecar && (!hadLog || iCAX::ProjectFile::CProjectFileCodec::Read(sidecar).Document.Info != QuickSaveResourceIdentity()))
        throw std::runtime_error("Cannot truncate another resource checkpoint generation");
    const auto suffix = ".retired-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    const auto oldLog = PathFromUTF8(path + suffix), oldSidecar = PathFromUTF8(path + ".resources" + suffix);
    const auto oldActive = m_ActiveQuickSaveLogPath, oldID = m_QuickSaveJournalID, oldMagic = m_QuickSaveMagic;
    const auto oldVersion = m_nQuickSaveVersion;
    db.CloseOperationLog();
    const bool aliasedLog = hadLog && std::filesystem::hard_link_count(log) > 1;
    bool movedLog = false, copiedLog = false, movedSidecar = false, createdNew = false;
    try
    {
        if (hadSidecar) { std::filesystem::rename(sidecar, oldSidecar); movedSidecar = true; }
        if (hadLog)
        {
            if (aliasedLog) { std::filesystem::copy_file(log, oldLog); copiedLog = true; }
            else { std::filesystem::rename(log, oldLog); movedLog = true; }
        }
        createdNew = true;
        db.OpenOperationLog(path, true, magic, version);
        BindQuickSaveCheckpoint(path, magic, version);
    }
    catch (...)
    {
        const auto error = std::current_exception();
        db.CloseOperationLog();
        if (createdNew && !copiedLog) std::filesystem::remove(log);
        if (copiedLog) RestoreJournalInPlace(oldLog, log);
        if (movedLog) std::filesystem::rename(oldLog, log);
        if (movedSidecar) std::filesystem::rename(oldSidecar, sidecar);
        m_ActiveQuickSaveLogPath = oldActive; m_QuickSaveJournalID = oldID;
        m_QuickSaveMagic = oldMagic; m_nQuickSaveVersion = oldVersion;
        if (copiedLog) { std::error_code ignored; std::filesystem::remove(oldLog, ignored); }
        std::rethrow_exception(error);
    }
    // These paths were renamed from this generation after identity checks.
    // A cleanup error cannot undo a successful new journal installation.
    std::error_code ignored;
    if (movedLog || copiedLog) std::filesystem::remove(oldLog, ignored);
    if (movedSidecar) std::filesystem::remove(oldSidecar, ignored);
}

void iCAX::Project::CProject::CloseQuickSaveLog()
{
    auto _pMainScene = GetScene(m_MainSceneID);
    if (_pMainScene)
    {
        _pMainScene->Database().CloseOperationLog();
        _pMainScene->Database().SetOperationLogCheckpoint({});
    }
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    m_bQuickSaveLogOpen = false;
}

void iCAX::Project::CProject::BindQuickSaveCheckpoint(const std::string& path,
    const std::string& magic, uint32_t version)
{
    auto& db = MainSceneDatabase();
    db.FlushOperationLog();
    m_ActiveQuickSaveLogPath = path;
    m_QuickSaveMagic = magic;
    m_nQuickSaveVersion = version;
    m_QuickSaveJournalID = iCAX::Database::COperationBatchJournal::ReadIdentity(path, magic, version);
    m_bQuickSaveResourceCheckpoint = std::filesystem::exists(PathFromUTF8(path + ".resources"));
    m_QuickSaveResources = m_bQuickSaveResourceCheckpoint ? PersistentJournalResources(MainSceneResources()) : m_AutoSaveResources;
    db.SetOperationLogCheckpoint([this] { CheckpointQuickSaveResources(); });
}

iCAX::ProjectFile::CProjectDocumentInfo iCAX::Project::CProject::QuickSaveResourceIdentity() const
{
    using namespace iCAX::Data;
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.Magic = "ICAX_RESOURCE_JOURNAL";
    info.ProductID = m_pProductContext->GetProductID();
    info.FormatVersion = "1";
    info.nFormatRevision = 1;
    info.ProjectID = GetProjectID();
    info.MainSceneID = GetMainSceneID();
    info.ProjectSettings = {{"journalId", m_QuickSaveJournalID}, {"journalMagic", m_QuickSaveMagic},
        {"journalVersion", m_nQuickSaveVersion}, {"archiveSize", static_cast<unsigned long long>(m_nAutoSaveArchiveSize)},
        {"archiveWriteTime", static_cast<long long>(m_nAutoSaveArchiveWriteTime)}};
    return info;
}

void iCAX::Project::CProject::CheckpointQuickSaveResources()
{
    const auto resources = PersistentJournalResources(MainSceneResources());
    const auto path = PathFromUTF8(m_ActiveQuickSaveLogPath + ".resources");
    if (SameResourceDescriptions(resources, m_QuickSaveResources)
        && (!m_bQuickSaveResourceCheckpoint || std::filesystem::is_regular_file(path))) return;
    iCAX::ProjectFile::CProjectResourceCheckpoint::Save(path, QuickSaveResourceIdentity(), MainSceneResources());
    m_QuickSaveResources = resources;
    m_bQuickSaveResourceCheckpoint = true;
}

void iCAX::Project::CProject::DiscardQuickSaveLog()
{
    PrepareQuickSaveDiscard();
    CommitQuickSaveDiscard();
}

void iCAX::Project::CProject::PrepareQuickSaveDiscard()
{
    // Called after the scene was stopped. Never remove a generation
    // that another project/session has since installed at the same path.
    const auto path = PathFromUTF8(m_ActiveQuickSaveLogPath);
    if (m_ActiveQuickSaveLogPath.empty() || m_QuickSaveJournalID.empty() || !std::filesystem::exists(path)) return;
    if (iCAX::Database::COperationBatchJournal::ReadIdentity(m_ActiveQuickSaveLogPath, m_QuickSaveMagic, m_nQuickSaveVersion) != m_QuickSaveJournalID)
        throw std::runtime_error("Cannot discard another project journal generation");
    const auto checkpoint = PathFromUTF8(m_ActiveQuickSaveLogPath + ".resources");
    if (std::filesystem::exists(checkpoint) && iCAX::ProjectFile::CProjectFileCodec::Read(checkpoint).Document.Info != QuickSaveResourceIdentity())
        throw std::runtime_error("Cannot discard another resource checkpoint generation");
    if (!m_DiscardRetiredLog.empty()) throw std::runtime_error("Project discard is already staged");
    m_bDiscardLogWasOpen = IsQuickSaveLogOpen();
    CloseQuickSaveLog();
    const auto suffix = ".discard-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    try
    {
        if (std::filesystem::exists(checkpoint))
        {
            const auto retired = m_ActiveQuickSaveLogPath + ".resources" + suffix;
            std::filesystem::rename(checkpoint, PathFromUTF8(retired));
            m_DiscardRetiredResources = retired;
        }
        const auto retired = m_ActiveQuickSaveLogPath + suffix;
        std::filesystem::rename(path, PathFromUTF8(retired));
        m_DiscardRetiredLog = retired;
    }
    catch (...)
    {
        CancelQuickSaveDiscard();
        throw;
    }
}

void iCAX::Project::CProject::CommitQuickSaveDiscard() noexcept
{
    // Cleanup cannot turn a successful discard into a failed live project.
    // Any leftover unique retired files are never replayed or merged.
    std::error_code ignored;
    if (!m_DiscardRetiredLog.empty()) std::filesystem::remove(PathFromUTF8(m_DiscardRetiredLog), ignored);
    if (!m_DiscardRetiredResources.empty()) std::filesystem::remove(PathFromUTF8(m_DiscardRetiredResources), ignored);
    m_DiscardRetiredLog.clear(); m_DiscardRetiredResources.clear(); m_bDiscardLogWasOpen = false;
}

void iCAX::Project::CProject::CancelQuickSaveDiscard()
{
    if (!m_DiscardRetiredLog.empty())
    {
        std::filesystem::rename(PathFromUTF8(m_DiscardRetiredLog), PathFromUTF8(m_ActiveQuickSaveLogPath));
        m_DiscardRetiredLog.clear();
    }
    if (!m_DiscardRetiredResources.empty())
    {
        std::filesystem::rename(PathFromUTF8(m_DiscardRetiredResources), PathFromUTF8(m_ActiveQuickSaveLogPath + ".resources"));
        m_DiscardRetiredResources.clear();
    }
    if (m_bDiscardLogWasOpen) OpenQuickSaveLog(false, m_QuickSaveMagic, m_nQuickSaveVersion);
    m_bDiscardLogWasOpen = false;
}

bool iCAX::Project::CProject::IsQuickSaveLogOpen() const
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    return m_bQuickSaveLogOpen;
}

std::vector<iCAX::Resource::CResourceInfo> iCAX::Project::CProject::AutoSavePersistentResources() const
{
    const auto& resources=MainSceneResources();
    std::vector<iCAX::Resource::CResourceReference> roots;
    for(const auto& info:resources.GetManifest(false)) roots.push_back({info.Key.Source,info.nVersion});
    auto reachable=resources.CollectReachable(roots);
    if(!reachable.IsComplete())throw std::runtime_error("Auto-save persistent resource dependency is missing");
    return std::move(reachable.Resources);
}

std::string iCAX::Project::CProject::AutoSaveSettingsSignature() const
{
    using namespace iCAX::Data;
    return VariantSerializer::Serialize(ObjectMap{{"projectId",GetProjectID()},{"sceneId",GetMainSceneID()},
        {"name",GetProjectName()},{"settings",Settings().GetProperties()},
        {"sceneSettings",GetMainScene().SceneSettings().GetProperties()}});
}

void iCAX::Project::CProject::CaptureAutoSaveBaseline(const std::string& path)
{
    const auto file=PathFromUTF8(path);
    const auto signature=AutoSaveSettingsSignature();
    auto resources=AutoSavePersistentResources();
    const auto size=std::filesystem::file_size(file);
    const auto stamp=std::filesystem::last_write_time(file).time_since_epoch().count();
    const auto normalized=std::filesystem::weakly_canonical(file).u8string();
    m_AutoSaveArchivePath.assign(reinterpret_cast<const char*>(normalized.data()),normalized.size());
    m_AutoSaveSettingsSignature=signature;m_AutoSaveResources=std::move(resources);m_nAutoSaveArchiveSize=size;m_nAutoSaveArchiveWriteTime=stamp;
    if (IsQuickSaveLogOpen()) m_QuickSaveResources = m_AutoSaveResources;
}

iCAX::Project::EAutoSaveCheckpoint iCAX::Project::CProject::TryAutoSaveJournal(const std::string& path)
{
    auto& db=MainSceneDatabase();
    if(db.IsWriteInProgress())return EAutoSaveCheckpoint::Deferred;
    if(m_AutoSaveArchivePath.empty()||!IsQuickSaveLogOpen()||!db.HasOperationLog())return EAutoSaveCheckpoint::SnapshotRequired;
    const auto file=PathFromUTF8(path);std::error_code error;
    if(!std::filesystem::is_regular_file(file,error)||error)return EAutoSaveCheckpoint::SnapshotRequired;
    const auto normalized=std::filesystem::weakly_canonical(file,error).u8string();
    if(error||std::string(reinterpret_cast<const char*>(normalized.data()),normalized.size())!=m_AutoSaveArchivePath)return EAutoSaveCheckpoint::SnapshotRequired;
    if(std::filesystem::file_size(file)!=m_nAutoSaveArchiveSize||std::filesystem::last_write_time(file).time_since_epoch().count()!=m_nAutoSaveArchiveWriteTime)return EAutoSaveCheckpoint::SnapshotRequired;
    if(AutoSaveSettingsSignature()!=m_AutoSaveSettingsSignature)return EAutoSaveCheckpoint::SnapshotRequired;
    const auto resources=AutoSavePersistentResources();
    if(resources.size()!=m_AutoSaveResources.size())return EAutoSaveCheckpoint::SnapshotRequired;
    // The resource pool orders roots by key and normalizes dependency order.
    // Compare the complete immutable descriptors without rebuilding Variant
    // maps or serializing a large tagged JSON string on every checkpoint.
    const auto fields=[](const iCAX::Resource::CResourceInfo& info){return std::tie(info.Key.Source,info.ResourceID,info.ResourceTypeID,
        info.nVersion,info.Persistence,info.Name,info.Source,info.MediaType,info.FlatBufferIdentifier,info.ContentHash,info.nSize,
        info.nSchemaVersion,info.nMinimumReaderVersion,info.nFlags,info.Metadata,info.Dependencies);};
    for(std::size_t i=0;i<resources.size();++i)if(fields(resources[i])!=fields(m_AutoSaveResources[i]))return EAutoSaveCheckpoint::SnapshotRequired;
    return db.FlushOperationLog()?EAutoSaveCheckpoint::Journal:EAutoSaveCheckpoint::SnapshotRequired;
}

const std::string& iCAX::Project::CProject::GetStartupComponent() const
{
    return m_StartupComponent;
}

const iCAX::Data::uuid& iCAX::Project::CProject::GetMainSceneID() const
{
    return m_MainSceneID;
}

iCAX::Project::CProjectScene& iCAX::Project::CProject::GetMainScene()
{
    return *RequireMainScene();
}

const iCAX::Project::CProjectScene& iCAX::Project::CProject::GetMainScene() const
{
    return *RequireMainScene();
}

std::shared_ptr<iCAX::Project::CProjectScene> iCAX::Project::CProject::GetScene(
    IN const iCAX::Data::uuid& SceneID_) const
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    auto _Iter = m_Scenes.find(SceneID_);
    return _Iter == m_Scenes.end() ? nullptr : _Iter->second;
}

std::vector<std::shared_ptr<iCAX::Project::CProjectScene>> iCAX::Project::CProject::GetScenes() const
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    std::vector<std::shared_ptr<CProjectScene>> _Scenes;
    _Scenes.reserve(m_Scenes.size());
    for (const auto& [_ID, _pScene] : m_Scenes)
    {
        _Scenes.push_back(_pScene);
    }
    return _Scenes;
}

std::shared_ptr<iCAX::Project::CProjectScene> iCAX::Project::CProject::OpenChildScene(
    IN const iCAX::Data::uuid& ParentSceneID_,
    IN CProjectSceneCreateInfo CreateInfo_)
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    if (m_Scenes.find(ParentSceneID_) == m_Scenes.end())
    {
        throw std::invalid_argument("Parent scene does not exist");
    }

    CreateInfo_.SceneID = CreateInfo_.SceneID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.SceneID;
    CreateInfo_.SceneChannelID = CreateInfo_.SceneChannelID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.SceneChannelID;
    CreateInfo_.ParentSceneID = ParentSceneID_;
    CreateInfo_.Role = ESceneRole::Transient;
    if (CreateInfo_.SceneName.empty())
    {
        CreateInfo_.SceneName = "Transient Scene";
    }
    CreateInfo_.pApplicationContext = CreateInfo_.pApplicationContext ? CreateInfo_.pApplicationContext : m_pApplicationContext;
    CreateInfo_.pProductContext = CreateInfo_.pProductContext ? CreateInfo_.pProductContext : m_pProductContext;
    CreateInfo_.pServiceProvider = CreateInfo_.pServiceProvider ? CreateInfo_.pServiceProvider : m_pServiceProvider;
    CreateInfo_.pMetaRegistry = CreateInfo_.pMetaRegistry ? CreateInfo_.pMetaRegistry : m_pMetaRegistry;
    CreateInfo_.pBehaviourRegistry = CreateInfo_.pBehaviourRegistry ? CreateInfo_.pBehaviourRegistry : m_pBehaviourRegistry;
    CreateInfo_.pResourceLoaderRegistry = CreateInfo_.pResourceLoaderRegistry ? CreateInfo_.pResourceLoaderRegistry : m_pResourceLoaderRegistry;
    CreateInfo_.ResourceLibraryInitializer = CreateInfo_.ResourceLibraryInitializer
        ? std::move(CreateInfo_.ResourceLibraryInitializer)
        : m_ResourceLibraryInitializer;
    CreateInfo_.pSDOChannelRegistry = CreateInfo_.pSDOChannelRegistry ? CreateInfo_.pSDOChannelRegistry : m_pSDOChannelRegistry;
    if (!CreateInfo_.FrameHandler)
    {
        CreateInfo_.FrameHandler = [this](IN CProjectScene& Scene_, IN const iCAX::Interaction::CSDOEndpoint& Endpoint_) {
            SceneFrameHandler _Handler;
            {
                std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
                _Handler = m_SceneFrameHandler;
            }
            if (_Handler)
            {
                _Handler(Scene_, Endpoint_);
            }
        };
    }

    auto _pScene = std::make_shared<CProjectScene>(*this, CreateInfo_);
    if (!m_Scenes.emplace(_pScene->GetSceneID(), _pScene).second)
    {
        throw std::runtime_error("Scene already exists");
    }
    return _pScene;
}

bool iCAX::Project::CProject::CloseScene(IN const iCAX::Data::uuid& SceneID_)
{
    if (SceneID_ == m_MainSceneID)
    {
        throw std::invalid_argument("Main scene cannot be closed by CloseScene; close the project instead");
    }

    std::shared_ptr<CProjectScene> _pScene;
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        auto _Iter = m_Scenes.find(SceneID_);
        if (_Iter == m_Scenes.end())
        {
            return false;
        }
        _pScene = _Iter->second;
        m_Scenes.erase(_Iter);
    }
    _pScene->Close();
    return true;
}

const iCAX::Data::uuid& iCAX::Project::CProject::GetMainSceneChannelID() const
{
    return GetMainScene().GetSceneChannelID();
}

bool iCAX::Project::CProject::IsOpen() const
{
    return GetScene(m_MainSceneID) != nullptr;
}

bool iCAX::Project::CProject::IsRunning() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    return _pMainScene && _pMainScene->IsRunning();
}

iCAX::Project::EProjectState iCAX::Project::CProject::GetState() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    return _pMainScene ? ConvertSceneState(_pMainScene->GetState()) : EProjectState::Stopped;
}

std::optional<iCAX::Project::CProjectFault> iCAX::Project::CProject::GetLastFault() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    auto _Fault = _pMainScene ? _pMainScene->GetLastFault() : std::nullopt;
    return _Fault ? std::make_optional(ConvertSceneFault(*_Fault)) : std::nullopt;
}

void iCAX::Project::CProject::SetSceneFrameHandler(IN SceneFrameHandler Handler_)
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    m_SceneFrameHandler = std::move(Handler_);
}

iCAX::Database::IRepository& iCAX::Project::CProject::MainSceneDatabase()
{
    return GetMainScene().Database();
}

const iCAX::Database::IRepository& iCAX::Project::CProject::MainSceneDatabase() const
{
    return GetMainScene().Database();
}

iCAX::Behaviour::IUniverse& iCAX::Project::CProject::MainSceneUniverse()
{
    return GetMainScene().Universe();
}

const iCAX::Behaviour::IUniverse& iCAX::Project::CProject::MainSceneUniverse() const
{
    return GetMainScene().Universe();
}

iCAX::Resource::CResourceLibrary& iCAX::Project::CProject::MainSceneResources()
{
    return GetMainScene().Resources();
}

const iCAX::Resource::CResourceLibrary& iCAX::Project::CProject::MainSceneResources() const
{
    return GetMainScene().Resources();
}

iCAX::Resource::CResourceLibrary&
iCAX::Project::CProject::Resources()
{
    return m_Resources;
}

const iCAX::Resource::CResourceLibrary&
iCAX::Project::CProject::Resources() const
{
    return m_Resources;
}

bool iCAX::Project::CProject::MainSceneHasPDOHub() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    return _pMainScene ? _pMainScene->HasPDOHub() : false;
}

iCAX::Project::CMainScenePDODescriptor iCAX::Project::CProject::GetMainScenePDODescriptor() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    return _pMainScene ? _pMainScene->GetPDODescriptor() : CMainScenePDODescriptor{};
}

iCAX::PDO::IPDOHub& iCAX::Project::CProject::MainScenePDOHub()
{
    return GetMainScene().PDOHub();
}

const iCAX::PDO::IPDOHub& iCAX::Project::CProject::MainScenePDOHub() const
{
    return GetMainScene().PDOHub();
}

iCAX::Services::CServiceProvider& iCAX::Project::CProject::MainSceneServices() const
{
    return GetMainScene().Services();
}

iCAX::Interaction::CSDOEndpoint iCAX::Project::CProject::GetMainSceneBackendSDOEndpoint() const
{
    return GetMainScene().GetBackendSDOEndpoint();
}

void iCAX::Project::CProject::SendMainSceneFrontendEvent(
    IN uint64_t nMethodCode_,
    IN const std::string& strPayloadText_)
{
    GetMainScene().SendFrontendEvent(nMethodCode_, strPayloadText_);
}

iCAX::Interaction::CSDOEndpoint iCAX::Project::CProject::GetMainSceneFrontendSDOEndpoint() const
{
    return GetMainScene().GetFrontendSDOEndpoint();
}

void iCAX::Project::CProject::Start()
{
    GetMainScene().Start();
}

void iCAX::Project::CProject::Stop()
{
    auto _Scenes = GetScenes();
    for (auto& _pScene : _Scenes)
    {
        if (_pScene)
        {
            _pScene->Stop();
        }
    }
}

void iCAX::Project::CProject::BindStartup()
{
    GetMainScene().BindStartup();
}

void iCAX::Project::CProject::PreSwapMainScenePDO()
{
    GetMainScene().PreSwapPDO();
}

void iCAX::Project::CProject::TickMainScene()
{
    GetMainScene().Tick();
}

void iCAX::Project::CProject::PostSwapMainScenePDO()
{
    GetMainScene().PostSwapPDO();
}

void iCAX::Project::CProject::Close()
{
    std::vector<std::shared_ptr<CProjectScene>> _Scenes;
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        for (const auto& [_ID, _pScene] : m_Scenes)
        {
            if (_pScene && _ID != m_MainSceneID)
            {
                _Scenes.push_back(_pScene);
            }
        }
        auto _Iter = m_Scenes.find(m_MainSceneID);
        if (_Iter != m_Scenes.end() && _Iter->second)
        {
            _Scenes.push_back(_Iter->second);
        }
        m_Scenes.clear();
        m_bQuickSaveLogOpen = false;
    }

    for (auto& _pScene : _Scenes)
    {
        _pScene->Close();
    }
}

iCAX::Project::CProjectSceneCreateInfo iCAX::Project::CProject::MakeMainSceneCreateInfo(
    IN const CProjectCreateInfo& CreateInfo_) const
{
    CProjectSceneCreateInfo _SceneInfo;
    _SceneInfo.SceneID = m_ProjectID;
    _SceneInfo.SceneChannelID = CreateInfo_.MainSceneChannelID.is_nil()
        ? iCAX::Data::GenerateNewUUID()
        : CreateInfo_.MainSceneChannelID;
    _SceneInfo.ParentSceneID = iCAX::Data::GenerateNilUUID();
    _SceneInfo.Role = ESceneRole::Main;
    _SceneInfo.SceneName = m_ProjectName;
    _SceneInfo.StartupComponent = m_StartupComponent;
    _SceneInfo.pApplicationContext = m_pApplicationContext;
    _SceneInfo.pProductContext = m_pProductContext;
    _SceneInfo.pServiceProvider = m_pServiceProvider;
    _SceneInfo.pMetaRegistry = m_pMetaRegistry;
    _SceneInfo.pBehaviourRegistry = m_pBehaviourRegistry;
    _SceneInfo.pResourceLoaderRegistry = m_pResourceLoaderRegistry;
    _SceneInfo.ResourceLibraryInitializer = m_ResourceLibraryInitializer;
    _SceneInfo.pSDOChannelRegistry = m_pSDOChannelRegistry;
    _SceneInfo.bEnablePDOHub = CreateInfo_.bEnablePDOHub;
    _SceneInfo.PDOHubCreateInfo = CreateInfo_.PDOHubCreateInfo;
    _SceneInfo.nFrameIntervalMilliseconds = CreateInfo_.nFrameIntervalMilliseconds;
    _SceneInfo.FrameHandler = [this](IN CProjectScene& Scene_, IN const iCAX::Interaction::CSDOEndpoint& Endpoint_) {
        SceneFrameHandler _Handler;
        {
            std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
            _Handler = m_SceneFrameHandler;
        }
        if (_Handler)
        {
            _Handler(Scene_, Endpoint_);
        }
    };
    return _SceneInfo;
}

std::shared_ptr<iCAX::Project::CProjectScene> iCAX::Project::CProject::RequireMainScene() const
{
    auto _pScene = GetScene(m_MainSceneID);
    if (!_pScene)
    {
        throw std::logic_error("Project main scene is closed");
    }
    return _pScene;
}

iCAX::Project::EProjectState iCAX::Project::CProject::ConvertSceneState(IN ESceneState State_)
{
    switch (State_)
    {
    case ESceneState::Created:
        return EProjectState::Created;
    case ESceneState::Starting:
        return EProjectState::Starting;
    case ESceneState::Running:
        return EProjectState::Running;
    case ESceneState::Stopping:
        return EProjectState::Stopping;
    case ESceneState::Stopped:
        return EProjectState::Stopped;
    case ESceneState::Faulted:
        return EProjectState::Faulted;
    default:
        throw std::invalid_argument("Invalid scene state");
    }
}

iCAX::Project::CProjectFault iCAX::Project::CProject::ConvertSceneFault(IN const CSceneFault& Fault_)
{
    return CProjectFault{ Fault_.Message, Fault_.Exception };
}
