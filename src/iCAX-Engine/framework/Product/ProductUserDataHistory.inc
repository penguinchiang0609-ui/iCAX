// The product store owns persistence; Database owns undo/redo and branching.
// A feature is one history scope, independent of every open Scene.
#include <set>
#include "Data/VariantSerializer.h"
namespace
{
    using UserRecord = iCAX::Application::CProductUserDataRecord;
    using UserMap = iCAX::Data::ObjectMap;
    using UserArray = iCAX::Data::VariantArray;

    UserMap UserRecordFields(const UserRecord& r)
    {
        UserArray links;
        for (const auto& l : r.Links) links.emplace_back(UserMap{{"relation",l.RelationType},
            {"kind",l.TargetKind},{"feature",l.TargetFeatureID},{"type",l.TargetType},{"id",l.TargetID}});
        return {{"feature",r.FeatureID},{"type",r.RecordType},{"id",r.RecordID},
            {"subjectType",r.SubjectType},{"subjectId",r.SubjectID},{"owner",r.OwnerScope},
            {"schema",r.SchemaVersion},{"payload",r.Payload},{"links",links}};
    }
    UserRecord UserRecordFromFields(const UserMap& f)
    {
        UserRecord r;
        r.FeatureID=f.at("feature").To<std::string>();r.RecordType=f.at("type").To<std::string>();
        r.RecordID=f.at("id").To<std::string>();r.SubjectType=f.at("subjectType").To<std::string>();
        r.SubjectID=f.at("subjectId").To<std::string>();r.OwnerScope=f.at("owner").To<std::string>();
        r.SchemaVersion=f.at("schema").To<uint32_t>();r.Payload=f.at("payload");
        for (const auto& value:f.at("links").To<UserArray>()) {
            const auto l=value.To<UserMap>();iCAX::Application::CUserDataLink link;
            link.RelationType=l.at("relation").To<std::string>();link.TargetKind=l.at("kind").To<std::string>();
            link.TargetFeatureID=l.at("feature").To<std::string>();link.TargetType=l.at("type").To<std::string>();
            link.TargetID=l.at("id").To<std::string>();r.Links.push_back(std::move(link));
        }
        return r;
    }
    std::string UserRecordKey(const std::string& type,const std::string& id)
    { return iCAX::Data::VariantSerializer::Serialize(UserArray{type,id}); }

    class CProductUserDataHistoryStore final : public iCAX::Application::IProductUserDataStore
    {
        std::shared_ptr<iCAX::Application::IProductUserDataStore> Store;
        std::vector<iCAX::Product::CProductUserDataFeatureDefinition> Features;
        std::map<std::string,std::shared_ptr<iCAX::Database::IRepository>> Repositories;
        std::mutex Mutex;
        UserMap ReadFeature(const std::string& feature)
        {
            const auto found=std::find_if(Features.begin(),Features.end(),[&](const auto& f){return f.FeatureID==feature;});
            if(found==Features.end())throw std::invalid_argument("Unknown user data history feature");
            UserMap records;
            for(const auto& type:found->RecordTypes){
                iCAX::Application::CProductUserDataQuery query;query.FeatureID=feature;query.RecordType=type.RecordType;
                for(const auto& r:Store->List(query))records[UserRecordKey(r.RecordType,r.RecordID)]=UserRecordFields(r);
            }
            return records;
        }
        iCAX::Database::IRepository& Prepare(const std::string& feature)
        {
            const auto records=ReadFeature(feature);auto& repository=Repositories[feature];
            // Writes from another runtime establish a new baseline; never replay
            // an obsolete product history over somebody else's resources.
            if(!repository||SettingsFields(*repository).at("records").To<UserMap>()!=records)
                repository=MakeSettingsRepository({{"records",records}});
            return *repository;
        }
        void Record(iCAX::Database::IRepository& repository,const UserMap& records,const char* name)
        {
            if(SettingsFields(repository).at("records").To<UserMap>()==records)return;
            auto undo=repository.BeginUndoCommand(name);CommitSettingsFields(repository,{{"records",records}});undo->End();
        }
        void Restore(const UserMap& fields)
        {
            auto r=UserRecordFromFields(fields);const auto current=Store->Get(r.FeatureID,r.RecordType,r.RecordID,true);
            Store->Put(r,current?current->Revision:0);
        }
    public:
        CProductUserDataHistoryStore(std::shared_ptr<IProductUserDataStore> store,
            std::vector<iCAX::Product::CProductUserDataFeatureDefinition> features):Store(std::move(store)),Features(std::move(features)){}
        const std::string& GetProductID() const noexcept override{return Store->GetProductID();}
        std::vector<UserRecord> List(const iCAX::Application::CProductUserDataQuery& query) override
        {std::lock_guard lock(Mutex);return Store->List(query);}
        std::optional<UserRecord> Get(const std::string& feature,const std::string& type,const std::string& id,bool deleted=false) override
        {std::lock_guard lock(Mutex);return Store->Get(feature,type,id,deleted);}
        UserRecord Put(const UserRecord& record,std::optional<uint64_t> expected=std::nullopt) override
        {
            std::lock_guard lock(Mutex);auto& repository=Prepare(record.FeatureID);
            auto records=SettingsFields(repository).at("records").To<UserMap>();const auto key=UserRecordKey(record.RecordType,record.RecordID);
            const auto previous=records.find(key);const auto old=previous==records.end()?std::optional<UserMap>{}:previous->second.To<UserMap>();
            auto saved=Store->Put(record,expected);records[key]=UserRecordFields(saved);
            try{Record(repository,records,old?"修改用户资源":"添加用户资源");}
            catch(...){if(old)Restore(*old);else Store->Delete(saved.FeatureID,saved.RecordType,saved.RecordID,saved.Revision);throw;}
            return saved;
        }
        bool Delete(const std::string& feature,const std::string& type,const std::string& id,std::optional<uint64_t> expected=std::nullopt) override
        {
            std::lock_guard lock(Mutex);auto& repository=Prepare(feature);auto records=SettingsFields(repository).at("records").To<UserMap>();
            const auto key=UserRecordKey(type,id);const auto found=records.find(key);
            if(!Store->Delete(feature,type,id,expected))return false;
            const auto old=found->second.To<UserMap>();records.erase(found);
            try{Record(repository,records,"删除用户资源");}catch(...){Restore(old);throw;}
            return true;
        }
        iCAX::Data::Variant History(const std::string& feature,const std::string& direction)
        {
            std::lock_guard lock(Mutex);auto& repository=Prepare(feature);
            const auto before=SettingsFields(repository).at("records").To<UserMap>();
            const bool applied=direction=="undo"?repository.Undo():direction=="redo"?repository.Redo():false;
            if(applied){
                const auto after=SettingsFields(repository).at("records").To<UserMap>();
                try{
                    // A store Put/Delete is one command, so exactly one record
                    // differs. Its native SQLite write is atomic and versioned.
                    std::set<std::string> keys;for(const auto& [key,value]:before)keys.insert(key);for(const auto& [key,value]:after)keys.insert(key);
                    std::vector<std::string> changed;for(const auto& key:keys)if(!before.contains(key)||!after.contains(key)||before.at(key)!=after.at(key))changed.push_back(key);
                    if(changed.size()!=1)throw std::runtime_error("Invalid user data history command");
                    const auto& key=changed[0];
                    const auto r=UserRecordFromFields((after.contains(key)?after.at(key):before.at(key)).To<UserMap>());
                    const auto current=Store->Get(r.FeatureID,r.RecordType,r.RecordID,true);
                    const bool active=current&&current->DeletedAt.empty();
                    // Another runtime can write after Prepare's snapshot. Check
                    // the exact old payload, then use this same revision for the
                    // atomic store write; never adopt an external newer revision.
                    if(before.contains(key)?!active||UserRecordFields(*current)!=before.at(key).To<UserMap>():active)
                        throw std::runtime_error("User data changed while applying history; refresh the resource list");
                    if(after.contains(key))Store->Put(r,current?current->Revision:0);
                    else if(!Store->Delete(r.FeatureID,r.RecordType,r.RecordID,current->Revision))
                        throw std::runtime_error("User data history record missing");
                }catch(...){if(direction=="undo")repository.Redo();else repository.Undo();throw;}
            }
            auto result=_MakeUndoRedoStatePayload(repository).To<UserMap>();result["applied"]=applied;return result;
        }
    };
    std::shared_ptr<iCAX::Application::IProductUserDataStore> MakeUserDataHistoryStore(
        std::shared_ptr<iCAX::Application::IProductUserDataStore> store,
        const std::vector<iCAX::Product::CProductUserDataFeatureDefinition>& features)
    {return std::make_shared<CProductUserDataHistoryStore>(std::move(store),features);}
}
