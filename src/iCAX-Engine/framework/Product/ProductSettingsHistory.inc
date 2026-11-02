// Product-level persistence adapter. Undo grouping, inverse operations, history
// limits and redo invalidation are supplied entirely by Database::IRepository.
#include "Database/ComponentBase.h"

namespace
{
    class CProductSettingsComponent final : public iCAX::Database::CComponentBase
    {
    public:
        static constexpr const char* Class = "CProductSettingsComponent";
        explicit CProductSettingsComponent(std::shared_ptr<iCAX::Database::IEntity> Entity_)
            : CComponentBase(Entity_) {}
        iCAX::Data::ObjectMap Fields;
        std::string GetComponentClass() const override { return Class; }
        std::vector<std::string> GetPropertyNameArray() const override { return {"Fields"}; }
        iCAX::Data::PropertyValue GetProperty(const std::string& Name_) const override
        {
            if (Name_ != "Fields") throw std::invalid_argument("Unknown product settings field");
            return Fields;
        }
        void OnSetProperty(const std::string& Name_, const iCAX::Data::PropertyValue& Value_) override
        {
            if (Name_ != "Fields") throw std::invalid_argument("Unknown product settings field");
            Fields = Value_.To<iCAX::Data::ObjectMap>();
        }
    };

    iCAX::Data::ObjectMap SettingsFields(const iCAX::Database::IRepository& Repository_)
    {
        return Repository_.GetEntity(Repository_.GetID())->GetComponent(CProductSettingsComponent::Class)
            ->GetProperty("Fields").To<iCAX::Data::ObjectMap>();
    }

    std::shared_ptr<iCAX::Database::IRepository> MakeSettingsRepository(const iCAX::Data::ObjectMap& Fields_)
    {
        auto _Meta = iCAX::Database::CreateMetaRegistry();
        _Meta->RegistType(CProductSettingsComponent::Class, iCAX::Database::CComponentBase::S_ClassName);
        _Meta->RegistCreatorByName(CProductSettingsComponent::Class, [](auto Entity_) {
            return std::make_shared<CProductSettingsComponent>(Entity_);
        });
        _Meta->RegistPropertyByName(CProductSettingsComponent::Class, "Fields",
            [](const void* Value_) -> iCAX::Data::PropertyValue {
                return static_cast<const CProductSettingsComponent*>(Value_)->Fields;
            }, [](void* Value_, const iCAX::Data::PropertyValue& Fields_) {
                static_cast<CProductSettingsComponent*>(Value_)->Fields = Fields_.To<iCAX::Data::ObjectMap>();
            }, iCAX::Database::EPropertyPersistence::Persistent, iCAX::Database::EPropertyChangePolicy::Transactional);
        auto _Repository = iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID(), _Meta);
        _Repository->BeginLoadBaseline();
        try {
            auto _Entity = _Repository->CreateEntity(_Repository->GetID());
            auto _Component = _Entity->AddComponent(CProductSettingsComponent::Class);
            std::string _Error;
            if (!_Component->SetProperties({{"Fields", Fields_}}, _Error)) throw std::runtime_error(_Error);
            _Repository->EndLoadBaseline();
        } catch (...) { _Repository->CancelLoadBaseline(); throw; }
        return _Repository;
    }

    void CommitSettingsFields(iCAX::Database::IRepository& Repository_, const iCAX::Data::ObjectMap& Fields_)
    {
        auto& _Transaction = Repository_.BeginTransaction();
        _Transaction.ModifyComponent(Repository_.GetID(), CProductSettingsComponent::Class, {{"Fields", Fields_}});
        std::string _Error;
        if (!Repository_.CommitTransaction(_Transaction, _Error)) throw std::runtime_error(_Error);
    }
}

void iCAX::Product::CProductRuntime::UpdateSettingsWithHistory(const std::string& Scope_,
    const iCAX::Data::PropertyBag& Fields_, const std::string& CommandName_)
{
    if (Scope_.empty() || CommandName_.empty() || Fields_.GetProperties().empty())
        throw std::invalid_argument("Settings history requires scope, fields and command name");
    std::lock_guard<std::mutex> _Lock(m_ProductDataMutex);
    const auto& _Fields = Fields_.GetProperties();
    auto _Repository = m_SettingsRepositories.find(Scope_);
    if (_Repository == m_SettingsRepositories.end()) {
        iCAX::Data::ObjectMap _Baseline;
        for (const auto& [_Key, _Value] : _Fields) {
            for (const auto& [_OtherScope, _Other] : m_SettingsRepositories)
                if (SettingsFields(*_Other).contains(_Key)) throw std::invalid_argument("Settings history scopes cannot overlap");
            _Baseline[_Key] = m_ProductData.Settings.Get(_Key);
        }
        _Repository = m_SettingsRepositories.emplace(Scope_, MakeSettingsRepository(_Baseline)).first;
    }
    const auto _Previous = SettingsFields(*_Repository->second);
    if (_Previous.size() != _Fields.size()) throw std::invalid_argument("Settings history keys must remain fixed");
    for (const auto& [_Key, _Value] : _Fields)
        if (!_Previous.contains(_Key)) throw std::invalid_argument("Settings history keys must remain fixed");
    if (_Previous == _Fields) return; // No-op must not consume history or invalidate redo.
    auto _Updated = m_ProductData;
    for (const auto& [_Key, _Value] : _Fields) _Updated.Settings.Set(_Key, _Value);
    // Persist before committing the native command: storage failure leaves both
    // repository state and its redo branch untouched.
    SaveProductData(_Updated);
    try {
        auto _Undo = _Repository->second->BeginUndoCommand(CommandName_);
        CommitSettingsFields(*_Repository->second, _Fields);
    } catch (...) { SaveProductData(m_ProductData); throw; }
    m_ProductData = std::move(_Updated);
}

iCAX::Data::Variant iCAX::Product::CProductRuntime::SettingsHistory(
    const std::string& Scope_, const std::string& Direction_)
{
    if (Scope_.empty()) throw std::invalid_argument("Settings history scope is required");
    std::lock_guard<std::mutex> _Lock(m_ProductDataMutex);
    const auto _Found = m_SettingsRepositories.find(Scope_);
    if (_Found == m_SettingsRepositories.end()) return iCAX::Data::ObjectMap{
        {"canUndo", false}, {"canRedo", false}, {"applied", false},
        {"undoSteps", iCAX::Data::VariantArray{}}, {"redoSteps", iCAX::Data::VariantArray{}}};
    auto& _Repository = *_Found->second;
    const bool _Applied = Direction_ == "undo" ? _Repository.Undo() : Direction_ == "redo" ? _Repository.Redo() : false;
    if (_Applied) {
        auto _Updated = m_ProductData;
        for (const auto& [_Key, _Value] : SettingsFields(_Repository)) _Updated.Settings.Set(_Key, _Value);
        try { SaveProductData(_Updated); }
        catch (...) {
            // Roll back the failed persistence using the same native history.
            if (Direction_ == "undo") _Repository.Redo(); else _Repository.Undo();
            throw;
        }
        m_ProductData = std::move(_Updated);
    }
    auto _State = _MakeUndoRedoStatePayload(_Repository).To<iCAX::Data::ObjectMap>();
    _State["applied"] = _Applied;
    return _State;
}
