#include "pch.h"
#include "UserDataStore.h"

#include "Data/VariantSerializer.h"

#include <algorithm>
#include <optional>
#include <set>
#include <winsqlite/winsqlite3.h>

namespace
{
    using iCAX::Application::CUserDataLink;
    using iCAX::Application::CUserDataRecord;

    class CStatement final
    {
    public:
        CStatement(IN sqlite3* pDatabase_, IN const char* pSql_)
            : m_pDatabase(pDatabase_)
        {
            const auto _Result = sqlite3_prepare_v2(pDatabase_, pSql_, -1, &m_pStatement, nullptr);
            if (_Result != SQLITE_OK)
            {
                throw std::runtime_error(std::string("Failed to prepare user data statement: ")
                    + sqlite3_errmsg(pDatabase_));
            }
        }

        ~CStatement()
        {
            if (m_pStatement) sqlite3_finalize(m_pStatement);
        }

        CStatement(IN const CStatement&) = delete;
        CStatement& operator=(IN const CStatement&) = delete;

        sqlite3_stmt* Get() const noexcept { return m_pStatement; }

        void BindText(IN const int nIndex_, IN const std::string& strValue_) const
        {
            if (sqlite3_bind_text(
                m_pStatement,
                nIndex_,
                strValue_.c_str(),
                static_cast<int>(strValue_.size()),
                SQLITE_TRANSIENT) != SQLITE_OK)
            {
                throw std::runtime_error(std::string("Failed to bind user data value: ")
                    + sqlite3_errmsg(m_pDatabase));
            }
        }

        void BindInt64(IN const int nIndex_, IN const std::uint64_t nValue_) const
        {
            if (sqlite3_bind_int64(m_pStatement, nIndex_, static_cast<sqlite3_int64>(nValue_)) != SQLITE_OK)
            {
                throw std::runtime_error(std::string("Failed to bind user data number: ")
                    + sqlite3_errmsg(m_pDatabase));
            }
        }

    private:
        sqlite3* m_pDatabase = nullptr;
        sqlite3_stmt* m_pStatement = nullptr;
    };

    void Execute(IN sqlite3* pDatabase_, IN const char* pSql_)
    {
        char* _pError = nullptr;
        const auto _Result = sqlite3_exec(pDatabase_, pSql_, nullptr, nullptr, &_pError);
        if (_Result == SQLITE_OK) return;
        const std::string _Message = _pError
            ? std::string(_pError)
            : std::string(sqlite3_errmsg(pDatabase_));
        if (_pError) sqlite3_free(_pError);
        throw std::runtime_error("User data database operation failed: " + _Message);
    }

    std::string ColumnText(IN sqlite3_stmt* pStatement_, IN const int nColumn_)
    {
        const auto* _pText = sqlite3_column_text(pStatement_, nColumn_);
        if (!_pText) return {};
        const auto _Size = sqlite3_column_bytes(pStatement_, nColumn_);
        return std::string(reinterpret_cast<const char*>(_pText), static_cast<std::size_t>(_Size));
    }

    CUserDataRecord ReadRecordBase(IN sqlite3_stmt* pStatement_)
    {
        CUserDataRecord _Record;
        _Record.ProductID = ColumnText(pStatement_, 0);
        _Record.FeatureID = ColumnText(pStatement_, 1);
        _Record.RecordType = ColumnText(pStatement_, 2);
        _Record.SubjectType = ColumnText(pStatement_, 3);
        _Record.SubjectID = ColumnText(pStatement_, 4);
        _Record.RecordID = ColumnText(pStatement_, 5);
        _Record.OwnerScope = ColumnText(pStatement_, 6);
        _Record.SchemaVersion = static_cast<std::uint32_t>(sqlite3_column_int64(pStatement_, 7));
        _Record.Payload = iCAX::Data::VariantSerializer::Deserialize(ColumnText(pStatement_, 8));
        _Record.Revision = static_cast<std::uint64_t>(sqlite3_column_int64(pStatement_, 9));
        _Record.CreatedAt = ColumnText(pStatement_, 10);
        _Record.UpdatedAt = ColumnText(pStatement_, 11);
        _Record.DeletedAt = ColumnText(pStatement_, 12);
        return _Record;
    }

    void ValidateKey(IN const std::string& strValue_, IN const char* pName_)
    {
        if (strValue_.empty())
            throw std::invalid_argument(std::string("User data ") + pName_ + " cannot be empty");
        if (strValue_.size() > 240)
            throw std::invalid_argument(std::string("User data ") + pName_ + " is too long");
        for (const auto _Character : strValue_)
        {
            if (static_cast<unsigned char>(_Character) < 0x20)
            {
                throw std::invalid_argument(
                    std::string("User data ") + pName_ + " contains control characters");
            }
        }
    }

    void ValidateLink(IN const CUserDataLink& Link_)
    {
        ValidateKey(Link_.RelationType, "relation type");
        ValidateKey(Link_.TargetKind, "link target kind");
        ValidateKey(Link_.TargetType, "link target type");
        ValidateKey(Link_.TargetID, "link target ID");
        if (Link_.TargetKind != "definition"
            && Link_.TargetKind != "user-record"
            && Link_.TargetKind != "external")
        {
            throw std::invalid_argument(
                "User data link target kind must be definition, user-record or external");
        }
        if (Link_.TargetKind == "user-record")
            ValidateKey(Link_.TargetFeatureID, "link target feature ID");
        else if (!Link_.TargetFeatureID.empty())
            ValidateKey(Link_.TargetFeatureID, "link target feature ID");
    }

    void ValidateRecord(IN const CUserDataRecord& Record_)
    {
        ValidateKey(Record_.ProductID, "product ID");
        ValidateKey(Record_.FeatureID, "feature ID");
        ValidateKey(Record_.RecordType, "record type");
        ValidateKey(Record_.SubjectType, "subject type");
        ValidateKey(Record_.SubjectID, "subject ID");
        ValidateKey(Record_.RecordID, "record ID");
        ValidateKey(Record_.OwnerScope, "owner scope");
        if (Record_.SchemaVersion == 0)
            throw std::invalid_argument("User data schema version must be greater than zero");
        for (const auto& _Link : Record_.Links) ValidateLink(_Link);
    }

    void CheckStepDone(IN sqlite3* pDatabase_, IN sqlite3_stmt* pStatement_)
    {
        const auto _Result = sqlite3_step(pStatement_);
        if (_Result != SQLITE_DONE)
        {
            throw std::runtime_error(std::string("User data write failed: ")
                + sqlite3_errmsg(pDatabase_));
        }
    }

    int ReadUserVersion(IN sqlite3* pDatabase_)
    {
        CStatement _Statement(pDatabase_, "PRAGMA user_version;");
        if (sqlite3_step(_Statement.Get()) != SQLITE_ROW)
        {
            throw std::runtime_error(std::string("Failed to read user data schema version: ")
                + sqlite3_errmsg(pDatabase_));
        }
        return sqlite3_column_int(_Statement.Get(), 0);
    }

    class CTransaction final
    {
    public:
        explicit CTransaction(IN sqlite3* pDatabase_)
            : m_pDatabase(pDatabase_)
        {
            Execute(m_pDatabase, "BEGIN IMMEDIATE;");
        }

        ~CTransaction()
        {
            if (!m_bCommitted)
                (void)sqlite3_exec(m_pDatabase, "ROLLBACK;", nullptr, nullptr, nullptr);
        }

        void Commit()
        {
            Execute(m_pDatabase, "COMMIT;");
            m_bCommitted = true;
        }

    private:
        sqlite3* m_pDatabase = nullptr;
        bool m_bCommitted = false;
    };

    void CreateVersion2Schema(IN sqlite3* pDatabase_)
    {
        Execute(pDatabase_,
            "CREATE TABLE IF NOT EXISTS user_records ("
            "product_id TEXT NOT NULL,"
            "feature_id TEXT NOT NULL,"
            "record_type TEXT NOT NULL,"
            "record_id TEXT NOT NULL,"
            "subject_type TEXT NOT NULL,"
            "subject_id TEXT NOT NULL,"
            "owner_scope TEXT NOT NULL,"
            "schema_version INTEGER NOT NULL,"
            "payload TEXT NOT NULL,"
            "revision INTEGER NOT NULL,"
            "created_at TEXT NOT NULL,"
            "updated_at TEXT NOT NULL,"
            "deleted_at TEXT,"
            "PRIMARY KEY(product_id, feature_id, record_type, record_id)"
            ");"
            "CREATE INDEX IF NOT EXISTS user_records_subject_lookup "
            "ON user_records(product_id, feature_id, record_type, subject_type, subject_id, "
            "owner_scope, deleted_at, updated_at);"
            "CREATE TABLE IF NOT EXISTS user_record_links ("
            "product_id TEXT NOT NULL,"
            "source_feature_id TEXT NOT NULL,"
            "source_record_type TEXT NOT NULL,"
            "source_record_id TEXT NOT NULL,"
            "relation_type TEXT NOT NULL,"
            "target_kind TEXT NOT NULL,"
            "target_feature_id TEXT NOT NULL,"
            "target_type TEXT NOT NULL,"
            "target_id TEXT NOT NULL,"
            "created_at TEXT NOT NULL,"
            "PRIMARY KEY(product_id, source_feature_id, source_record_type, source_record_id, "
            "relation_type, target_kind, target_feature_id, target_type, target_id),"
            "FOREIGN KEY(product_id, source_feature_id, source_record_type, source_record_id) "
            "REFERENCES user_records(product_id, feature_id, record_type, record_id) ON DELETE CASCADE"
            ");"
            "CREATE INDEX IF NOT EXISTS user_record_links_target_lookup "
            "ON user_record_links(product_id, target_kind, target_feature_id, target_type, target_id);");
    }
}

struct iCAX::Application::CSqliteUserDataStore::CImpl final
{
    explicit CImpl(IN std::string strDatabasePath_)
        : DatabasePath(std::move(strDatabasePath_))
    {
        if (DatabasePath.empty()) throw std::invalid_argument("User data database path cannot be empty");
    }

    ~CImpl()
    {
        if (pDatabase) sqlite3_close(pDatabase);
    }

    void EnsureOpen()
    {
        if (pDatabase) return;
        const auto _FilePath = std::filesystem::path(std::u8string(
            DatabasePath.begin(), DatabasePath.end()));
        if (!_FilePath.parent_path().empty()) std::filesystem::create_directories(_FilePath.parent_path());
        const auto _Result = sqlite3_open_v2(
            DatabasePath.c_str(),
            &pDatabase,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr);
        if (_Result != SQLITE_OK)
        {
            const std::string _Message = pDatabase ? sqlite3_errmsg(pDatabase) : "unknown error";
            if (pDatabase)
            {
                sqlite3_close(pDatabase);
                pDatabase = nullptr;
            }
            throw std::runtime_error("Failed to open user data database: " + _Message);
        }
        try
        {
            sqlite3_busy_timeout(pDatabase, 5000);
            Execute(pDatabase, "PRAGMA journal_mode=WAL;");
            Execute(pDatabase, "PRAGMA synchronous=FULL;");
            Execute(pDatabase, "PRAGMA foreign_keys=ON;");
            const auto _SchemaVersion = ReadUserVersion(pDatabase);
            if (_SchemaVersion > 2)
                throw std::runtime_error("User data database was created by a newer iCAX version");

            if (_SchemaVersion == 1)
            {
                CTransaction _Transaction(pDatabase);
                Execute(pDatabase, "ALTER TABLE user_records RENAME TO user_records_v1;");
                CreateVersion2Schema(pDatabase);
                Execute(pDatabase,
                    "INSERT INTO user_records(product_id, feature_id, record_type, record_id, "
                    "subject_type, subject_id, owner_scope, schema_version, payload, revision, "
                    "created_at, updated_at, deleted_at) "
                    "SELECT data_namespace, '_legacy', collection_name, record_id, 'product', "
                    "data_namespace, owner_scope, 1, payload, revision, created_at, updated_at, deleted_at "
                    "FROM user_records_v1;"
                    "DROP TABLE user_records_v1;"
                    "PRAGMA user_version=2;");
                _Transaction.Commit();
            }
            else
            {
                CreateVersion2Schema(pDatabase);
                if (_SchemaVersion == 0) Execute(pDatabase, "PRAGMA user_version=2;");
            }
        }
        catch (...)
        {
            sqlite3_close(pDatabase);
            pDatabase = nullptr;
            throw;
        }
    }

    std::vector<CUserDataLink> ReadLinksUnlocked(IN const CUserDataRecord& Record_)
    {
        CStatement _Statement(
            pDatabase,
            "SELECT relation_type, target_kind, target_feature_id, target_type, target_id "
            "FROM user_record_links WHERE product_id=?1 AND source_feature_id=?2 "
            "AND source_record_type=?3 AND source_record_id=?4 "
            "ORDER BY relation_type, target_kind, target_feature_id, target_type, target_id;");
        _Statement.BindText(1, Record_.ProductID);
        _Statement.BindText(2, Record_.FeatureID);
        _Statement.BindText(3, Record_.RecordType);
        _Statement.BindText(4, Record_.RecordID);
        std::vector<CUserDataLink> _Links;
        while (true)
        {
            const auto _Result = sqlite3_step(_Statement.Get());
            if (_Result == SQLITE_DONE) break;
            if (_Result != SQLITE_ROW)
                throw std::runtime_error(std::string("User data link read failed: ")
                    + sqlite3_errmsg(pDatabase));
            CUserDataLink _Link;
            _Link.RelationType = ColumnText(_Statement.Get(), 0);
            _Link.TargetKind = ColumnText(_Statement.Get(), 1);
            _Link.TargetFeatureID = ColumnText(_Statement.Get(), 2);
            _Link.TargetType = ColumnText(_Statement.Get(), 3);
            _Link.TargetID = ColumnText(_Statement.Get(), 4);
            _Links.emplace_back(std::move(_Link));
        }
        return _Links;
    }

    std::optional<CUserDataRecord> GetUnlocked(
        IN const std::string& strProductID_,
        IN const std::string& strFeatureID_,
        IN const std::string& strRecordType_,
        IN const std::string& strRecordID_,
        IN const bool bIncludeDeleted_)
    {
        EnsureOpen();
        CStatement _Statement(
            pDatabase,
            "SELECT product_id, feature_id, record_type, subject_type, subject_id, record_id, "
            "owner_scope, schema_version, payload, revision, created_at, updated_at, "
            "COALESCE(deleted_at, '') FROM user_records "
            "WHERE product_id=?1 AND feature_id=?2 AND record_type=?3 AND record_id=?4 "
            "AND (?5=1 OR deleted_at IS NULL);");
        _Statement.BindText(1, strProductID_);
        _Statement.BindText(2, strFeatureID_);
        _Statement.BindText(3, strRecordType_);
        _Statement.BindText(4, strRecordID_);
        sqlite3_bind_int(_Statement.Get(), 5, bIncludeDeleted_ ? 1 : 0);
        const auto _Result = sqlite3_step(_Statement.Get());
        if (_Result == SQLITE_DONE) return std::nullopt;
        if (_Result != SQLITE_ROW)
            throw std::runtime_error(std::string("User data read failed: ") + sqlite3_errmsg(pDatabase));
        auto _Record = ReadRecordBase(_Statement.Get());
        _Record.Links = ReadLinksUnlocked(_Record);
        return _Record;
    }

    std::string DatabasePath;
    sqlite3* pDatabase = nullptr;
    std::mutex Mutex;
};

iCAX::Application::CSqliteUserDataStore::CSqliteUserDataStore(IN std::string strDatabasePath_)
    : m_pImpl(std::make_unique<CImpl>(std::move(strDatabasePath_)))
{
}

iCAX::Application::CSqliteUserDataStore::~CSqliteUserDataStore() = default;

std::vector<iCAX::Application::CUserDataRecord> iCAX::Application::CSqliteUserDataStore::List(
    IN const CUserDataQuery& Query_)
{
    ValidateKey(Query_.ProductID, "product ID");
    ValidateKey(Query_.FeatureID, "feature ID");
    ValidateKey(Query_.RecordType, "record type");
    if (!Query_.SubjectType.empty()) ValidateKey(Query_.SubjectType, "subject type");
    if (!Query_.SubjectID.empty())
    {
        if (Query_.SubjectType.empty())
            throw std::invalid_argument("User data subject ID filter requires a subject type");
        ValidateKey(Query_.SubjectID, "subject ID");
    }
    if (!Query_.OwnerScope.empty()) ValidateKey(Query_.OwnerScope, "owner scope");

    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->EnsureOpen();
    CStatement _Statement(
        m_pImpl->pDatabase,
        "SELECT product_id, feature_id, record_type, subject_type, subject_id, record_id, "
        "owner_scope, schema_version, payload, revision, created_at, updated_at, "
        "COALESCE(deleted_at, '') FROM user_records "
        "WHERE product_id=?1 AND feature_id=?2 AND record_type=?3 "
        "AND (?4='' OR subject_type=?4) AND (?5='' OR subject_id=?5) "
        "AND (?6='' OR owner_scope=?6) AND (?7=1 OR deleted_at IS NULL) "
        "ORDER BY updated_at DESC, record_id ASC;");
    _Statement.BindText(1, Query_.ProductID);
    _Statement.BindText(2, Query_.FeatureID);
    _Statement.BindText(3, Query_.RecordType);
    _Statement.BindText(4, Query_.SubjectType);
    _Statement.BindText(5, Query_.SubjectID);
    _Statement.BindText(6, Query_.OwnerScope);
    sqlite3_bind_int(_Statement.Get(), 7, Query_.IncludeDeleted ? 1 : 0);

    std::vector<CUserDataRecord> _Records;
    while (true)
    {
        const auto _Result = sqlite3_step(_Statement.Get());
        if (_Result == SQLITE_DONE) break;
        if (_Result != SQLITE_ROW)
            throw std::runtime_error(std::string("User data list failed: ")
                + sqlite3_errmsg(m_pImpl->pDatabase));
        auto _Record = ReadRecordBase(_Statement.Get());
        _Record.Links = m_pImpl->ReadLinksUnlocked(_Record);
        _Records.emplace_back(std::move(_Record));
    }
    return _Records;
}

std::optional<iCAX::Application::CUserDataRecord> iCAX::Application::CSqliteUserDataStore::Get(
    IN const std::string& strProductID_,
    IN const std::string& strFeatureID_,
    IN const std::string& strRecordType_,
    IN const std::string& strRecordID_,
    IN const bool bIncludeDeleted_)
{
    ValidateKey(strProductID_, "product ID");
    ValidateKey(strFeatureID_, "feature ID");
    ValidateKey(strRecordType_, "record type");
    ValidateKey(strRecordID_, "record ID");
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    return m_pImpl->GetUnlocked(
        strProductID_, strFeatureID_, strRecordType_, strRecordID_, bIncludeDeleted_);
}

iCAX::Application::CUserDataRecord iCAX::Application::CSqliteUserDataStore::Put(
    IN const CUserDataRecord& Record_,
    IN std::optional<std::uint64_t> ExpectedRevision_)
{
    ValidateRecord(Record_);
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->EnsureOpen();
    CTransaction _Transaction(m_pImpl->pDatabase);
    const auto _Existing = m_pImpl->GetUnlocked(
        Record_.ProductID, Record_.FeatureID, Record_.RecordType, Record_.RecordID, true);
    const auto _ActualRevision = _Existing ? _Existing->Revision : 0;
    if (ExpectedRevision_ && *ExpectedRevision_ != _ActualRevision)
        throw std::runtime_error("User data revision conflict");
    const auto _NextRevision = _ActualRevision + 1;
    const auto _Payload = iCAX::Data::VariantSerializer::Serialize(Record_.Payload);
    CStatement _Statement(
        m_pImpl->pDatabase,
        "INSERT INTO user_records(product_id, feature_id, record_type, record_id, subject_type, "
        "subject_id, owner_scope, schema_version, payload, revision, created_at, updated_at, deleted_at) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, "
        "strftime('%Y-%m-%dT%H:%M:%fZ','now'), strftime('%Y-%m-%dT%H:%M:%fZ','now'), NULL) "
        "ON CONFLICT(product_id, feature_id, record_type, record_id) DO UPDATE SET "
        "subject_type=excluded.subject_type, subject_id=excluded.subject_id, "
        "owner_scope=excluded.owner_scope, schema_version=excluded.schema_version, "
        "payload=excluded.payload, revision=excluded.revision, updated_at=excluded.updated_at, "
        "deleted_at=NULL;");
    _Statement.BindText(1, Record_.ProductID);
    _Statement.BindText(2, Record_.FeatureID);
    _Statement.BindText(3, Record_.RecordType);
    _Statement.BindText(4, Record_.RecordID);
    _Statement.BindText(5, Record_.SubjectType);
    _Statement.BindText(6, Record_.SubjectID);
    _Statement.BindText(7, Record_.OwnerScope);
    _Statement.BindInt64(8, Record_.SchemaVersion);
    _Statement.BindText(9, _Payload);
    _Statement.BindInt64(10, _NextRevision);
    CheckStepDone(m_pImpl->pDatabase, _Statement.Get());

    CStatement _DeleteLinks(
        m_pImpl->pDatabase,
        "DELETE FROM user_record_links WHERE product_id=?1 AND source_feature_id=?2 "
        "AND source_record_type=?3 AND source_record_id=?4;");
    _DeleteLinks.BindText(1, Record_.ProductID);
    _DeleteLinks.BindText(2, Record_.FeatureID);
    _DeleteLinks.BindText(3, Record_.RecordType);
    _DeleteLinks.BindText(4, Record_.RecordID);
    CheckStepDone(m_pImpl->pDatabase, _DeleteLinks.Get());

    for (const auto& _Link : Record_.Links)
    {
        if (_Link.TargetKind == "user-record"
            && !m_pImpl->GetUnlocked(
                Record_.ProductID,
                _Link.TargetFeatureID,
                _Link.TargetType,
                _Link.TargetID,
                false))
        {
            throw std::invalid_argument("User data relation target record does not exist");
        }
        CStatement _InsertLink(
            m_pImpl->pDatabase,
            "INSERT INTO user_record_links(product_id, source_feature_id, source_record_type, "
            "source_record_id, relation_type, target_kind, target_feature_id, target_type, "
            "target_id, created_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, "
            "strftime('%Y-%m-%dT%H:%M:%fZ','now'));");
        _InsertLink.BindText(1, Record_.ProductID);
        _InsertLink.BindText(2, Record_.FeatureID);
        _InsertLink.BindText(3, Record_.RecordType);
        _InsertLink.BindText(4, Record_.RecordID);
        _InsertLink.BindText(5, _Link.RelationType);
        _InsertLink.BindText(6, _Link.TargetKind);
        _InsertLink.BindText(7, _Link.TargetFeatureID);
        _InsertLink.BindText(8, _Link.TargetType);
        _InsertLink.BindText(9, _Link.TargetID);
        CheckStepDone(m_pImpl->pDatabase, _InsertLink.Get());
    }

    const auto _Saved = m_pImpl->GetUnlocked(
        Record_.ProductID, Record_.FeatureID, Record_.RecordType, Record_.RecordID, false);
    if (!_Saved) throw std::runtime_error("Saved user data record cannot be read back");
    _Transaction.Commit();
    return *_Saved;
}

bool iCAX::Application::CSqliteUserDataStore::Delete(
    IN const std::string& strProductID_,
    IN const std::string& strFeatureID_,
    IN const std::string& strRecordType_,
    IN const std::string& strRecordID_,
    IN std::optional<std::uint64_t> ExpectedRevision_)
{
    ValidateKey(strProductID_, "product ID");
    ValidateKey(strFeatureID_, "feature ID");
    ValidateKey(strRecordType_, "record type");
    ValidateKey(strRecordID_, "record ID");
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->EnsureOpen();
    CTransaction _Transaction(m_pImpl->pDatabase);
    const auto _Existing = m_pImpl->GetUnlocked(
        strProductID_, strFeatureID_, strRecordType_, strRecordID_, true);
    if (!_Existing || !_Existing->DeletedAt.empty())
    {
        _Transaction.Commit();
        return false;
    }
    if (ExpectedRevision_ && *ExpectedRevision_ != _Existing->Revision)
        throw std::runtime_error("User data revision conflict");

    CStatement _Incoming(
        m_pImpl->pDatabase,
        "SELECT 1 FROM user_record_links AS link "
        "JOIN user_records AS source ON source.product_id=link.product_id "
        "AND source.feature_id=link.source_feature_id "
        "AND source.record_type=link.source_record_type "
        "AND source.record_id=link.source_record_id "
        "WHERE link.product_id=?1 AND link.target_kind='user-record' "
        "AND link.target_feature_id=?2 AND link.target_type=?3 AND link.target_id=?4 "
        "AND source.deleted_at IS NULL LIMIT 1;");
    _Incoming.BindText(1, strProductID_);
    _Incoming.BindText(2, strFeatureID_);
    _Incoming.BindText(3, strRecordType_);
    _Incoming.BindText(4, strRecordID_);
    if (sqlite3_step(_Incoming.Get()) == SQLITE_ROW)
        throw std::runtime_error("User data record is still referenced by another active record");

    CStatement _Statement(
        m_pImpl->pDatabase,
        "UPDATE user_records SET revision=revision+1, "
        "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'), "
        "deleted_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') "
        "WHERE product_id=?1 AND feature_id=?2 AND record_type=?3 AND record_id=?4;");
    _Statement.BindText(1, strProductID_);
    _Statement.BindText(2, strFeatureID_);
    _Statement.BindText(3, strRecordType_);
    _Statement.BindText(4, strRecordID_);
    CheckStepDone(m_pImpl->pDatabase, _Statement.Get());
    const auto _Changed = sqlite3_changes(m_pImpl->pDatabase) > 0;
    _Transaction.Commit();
    return _Changed;
}

const std::string& iCAX::Application::CSqliteUserDataStore::GetDatabasePath() const noexcept
{
    return m_pImpl->DatabasePath;
}

namespace
{
    iCAX::Application::CProductUserDataRecord ToProductRecord(
        IN const iCAX::Application::CUserDataRecord& Record_)
    {
        iCAX::Application::CProductUserDataRecord _Result;
        _Result.FeatureID = Record_.FeatureID;
        _Result.RecordType = Record_.RecordType;
        _Result.SubjectType = Record_.SubjectType;
        _Result.SubjectID = Record_.SubjectID;
        _Result.RecordID = Record_.RecordID;
        _Result.OwnerScope = Record_.OwnerScope;
        _Result.SchemaVersion = Record_.SchemaVersion;
        _Result.Payload = Record_.Payload;
        _Result.Links = Record_.Links;
        _Result.Revision = Record_.Revision;
        _Result.CreatedAt = Record_.CreatedAt;
        _Result.UpdatedAt = Record_.UpdatedAt;
        _Result.DeletedAt = Record_.DeletedAt;
        return _Result;
    }
}

iCAX::Application::CProductUserDataStore::CProductUserDataStore(
    IN std::shared_ptr<IUserDataStore> pStore_,
    IN std::string strProductID_,
    IN std::vector<CUserDataFeatureDescriptor> Descriptors_)
    : m_pStore(std::move(pStore_))
    , m_strProductID(std::move(strProductID_))
    , m_Descriptors(std::move(Descriptors_))
{
    if (!m_pStore) throw std::invalid_argument("User data store cannot be null");
    ValidateKey(m_strProductID, "product ID");

    std::set<std::string> _FeatureIDs;
    for (const auto& _Feature : m_Descriptors)
    {
        ValidateKey(_Feature.FeatureID, "feature descriptor ID");
        if (!_FeatureIDs.emplace(_Feature.FeatureID).second)
            throw std::invalid_argument("Duplicate user data feature descriptor");
        std::set<std::string> _RecordTypes;
        for (const auto& _RecordType : _Feature.RecordTypes)
        {
            ValidateKey(_RecordType.RecordType, "record type descriptor ID");
            if (!_RecordTypes.emplace(_RecordType.RecordType).second)
                throw std::invalid_argument("Duplicate user data record type descriptor");
            if (_RecordType.SchemaVersion == 0 || _RecordType.SubjectTypes.empty())
                throw std::invalid_argument("Invalid user data record type descriptor");
            std::set<std::string> _Subjects;
            for (const auto& _Subject : _RecordType.SubjectTypes)
            {
                ValidateKey(_Subject, "subject type descriptor ID");
                if (!_Subjects.emplace(_Subject).second)
                    throw std::invalid_argument("Duplicate user data subject type descriptor");
            }
            std::set<std::string> _Relations;
            for (const auto& _Relation : _RecordType.Relations)
            {
                ValidateKey(_Relation.RelationType, "relation descriptor ID");
                ValidateKey(_Relation.TargetKind, "relation target kind");
                ValidateKey(_Relation.TargetType, "relation target type");
                if (!_Relations.emplace(_Relation.RelationType).second)
                    throw std::invalid_argument("Duplicate user data relation descriptor");
                if (_Relation.TargetKind != "definition"
                    && _Relation.TargetKind != "user-record"
                    && _Relation.TargetKind != "external")
                {
                    throw std::invalid_argument("Invalid user data relation target kind descriptor");
                }
                if (_Relation.TargetKind == "user-record")
                    ValidateKey(_Relation.TargetFeatureID, "relation target feature ID");
            }
        }
    }
}

iCAX::Application::CProductUserDataStore::~CProductUserDataStore() = default;

const std::string& iCAX::Application::CProductUserDataStore::GetProductID() const noexcept
{
    return m_strProductID;
}

const iCAX::Application::CUserDataRecordTypeDescriptor&
iCAX::Application::CProductUserDataStore::RequireRecordType(
    IN const std::string& strFeatureID_,
    IN const std::string& strRecordType_) const
{
    const auto _Feature = std::find_if(
        m_Descriptors.begin(), m_Descriptors.end(),
        [&](const auto& Item_) { return Item_.FeatureID == strFeatureID_; });
    if (_Feature == m_Descriptors.end())
        throw std::invalid_argument("User data feature is not declared by the product manifest");
    const auto _RecordType = std::find_if(
        _Feature->RecordTypes.begin(), _Feature->RecordTypes.end(),
        [&](const auto& Item_) { return Item_.RecordType == strRecordType_; });
    if (_RecordType == _Feature->RecordTypes.end())
        throw std::invalid_argument("User data record type is not declared by the product manifest");
    return *_RecordType;
}

void iCAX::Application::CProductUserDataStore::ValidateRecord(
    IN const CProductUserDataRecord& Record_) const
{
    CUserDataRecord _Raw;
    _Raw.ProductID = m_strProductID;
    _Raw.FeatureID = Record_.FeatureID;
    _Raw.RecordType = Record_.RecordType;
    _Raw.SubjectType = Record_.SubjectType;
    _Raw.SubjectID = Record_.SubjectID;
    _Raw.RecordID = Record_.RecordID;
    _Raw.OwnerScope = Record_.OwnerScope;
    _Raw.SchemaVersion = Record_.SchemaVersion;
    _Raw.Payload = Record_.Payload;
    _Raw.Links = Record_.Links;
    ::ValidateRecord(_Raw);

    const auto& _Descriptor = RequireRecordType(Record_.FeatureID, Record_.RecordType);
    if (Record_.SchemaVersion != _Descriptor.SchemaVersion)
        throw std::invalid_argument("User data payload schema version does not match the manifest");
    if (std::find(
        _Descriptor.SubjectTypes.begin(),
        _Descriptor.SubjectTypes.end(),
        Record_.SubjectType) == _Descriptor.SubjectTypes.end())
    {
        throw std::invalid_argument("User data subject type is not allowed by the manifest");
    }
    if (!_Descriptor.AllowMultiple && Record_.RecordID != Record_.SubjectID)
    {
        throw std::invalid_argument(
            "A single-cardinality user record must use its subject ID as the record ID");
    }

    std::set<std::string> _Links;
    for (const auto& _Link : Record_.Links)
    {
        const auto _Relation = std::find_if(
            _Descriptor.Relations.begin(),
            _Descriptor.Relations.end(),
            [&](const auto& Item_) { return Item_.RelationType == _Link.RelationType; });
        if (_Relation == _Descriptor.Relations.end())
            throw std::invalid_argument("User data relation is not declared by the product manifest");
        if (_Relation->TargetKind != _Link.TargetKind
            || _Relation->TargetType != _Link.TargetType
            || (!_Relation->TargetFeatureID.empty()
                && _Relation->TargetFeatureID != _Link.TargetFeatureID))
        {
            throw std::invalid_argument("User data relation target does not match the manifest");
        }
        const auto _Identity = _Link.RelationType + "\n" + _Link.TargetKind + "\n"
            + _Link.TargetFeatureID + "\n" + _Link.TargetType + "\n" + _Link.TargetID;
        if (!_Links.emplace(_Identity).second)
            throw std::invalid_argument("Duplicate user data relation");
    }
}

std::vector<iCAX::Application::CProductUserDataRecord>
iCAX::Application::CProductUserDataStore::List(IN const CProductUserDataQuery& Query_)
{
    const auto& _Descriptor = RequireRecordType(Query_.FeatureID, Query_.RecordType);
    if (!Query_.SubjectType.empty()
        && std::find(
            _Descriptor.SubjectTypes.begin(),
            _Descriptor.SubjectTypes.end(),
            Query_.SubjectType) == _Descriptor.SubjectTypes.end())
    {
        throw std::invalid_argument("User data query subject type is not allowed by the manifest");
    }
    CUserDataQuery _Query;
    _Query.ProductID = m_strProductID;
    _Query.FeatureID = Query_.FeatureID;
    _Query.RecordType = Query_.RecordType;
    _Query.SubjectType = Query_.SubjectType;
    _Query.SubjectID = Query_.SubjectID;
    _Query.OwnerScope = Query_.OwnerScope;
    _Query.IncludeDeleted = Query_.IncludeDeleted;
    const auto _Records = m_pStore->List(_Query);
    std::vector<CProductUserDataRecord> _Result;
    _Result.reserve(_Records.size());
    for (const auto& _Record : _Records) _Result.emplace_back(ToProductRecord(_Record));
    return _Result;
}

std::optional<iCAX::Application::CProductUserDataRecord>
iCAX::Application::CProductUserDataStore::Get(
    IN const std::string& strFeatureID_,
    IN const std::string& strRecordType_,
    IN const std::string& strRecordID_,
    IN const bool bIncludeDeleted_)
{
    (void)RequireRecordType(strFeatureID_, strRecordType_);
    const auto _Record = m_pStore->Get(
        m_strProductID, strFeatureID_, strRecordType_, strRecordID_, bIncludeDeleted_);
    return _Record
        ? std::optional<CProductUserDataRecord>(ToProductRecord(*_Record))
        : std::nullopt;
}

iCAX::Application::CProductUserDataRecord
iCAX::Application::CProductUserDataStore::Put(
    IN const CProductUserDataRecord& Record_,
    IN std::optional<std::uint64_t> ExpectedRevision_)
{
    ValidateRecord(Record_);
    CUserDataRecord _Record;
    _Record.ProductID = m_strProductID;
    _Record.FeatureID = Record_.FeatureID;
    _Record.RecordType = Record_.RecordType;
    _Record.SubjectType = Record_.SubjectType;
    _Record.SubjectID = Record_.SubjectID;
    _Record.RecordID = Record_.RecordID;
    _Record.OwnerScope = Record_.OwnerScope;
    _Record.SchemaVersion = Record_.SchemaVersion;
    _Record.Payload = Record_.Payload;
    _Record.Links = Record_.Links;
    return ToProductRecord(m_pStore->Put(_Record, ExpectedRevision_));
}

bool iCAX::Application::CProductUserDataStore::Delete(
    IN const std::string& strFeatureID_,
    IN const std::string& strRecordType_,
    IN const std::string& strRecordID_,
    IN std::optional<std::uint64_t> ExpectedRevision_)
{
    (void)RequireRecordType(strFeatureID_, strRecordType_);
    return m_pStore->Delete(
        m_strProductID, strFeatureID_, strRecordType_, strRecordID_, ExpectedRevision_);
}
