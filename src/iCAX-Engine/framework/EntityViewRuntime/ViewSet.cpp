#include "pch.h"

#include "ViewSet.h"

#include "Data/VariantSerializer.h"
#include "Database/ComponentBase.h"
#include "Database/IEntity.h"
#include "Database/IEntityView.h"
#include "Database/IRepository.h"
#include "Resources/ResourceFlatBuffer.h"
#include "Resources/ResourceLibrary.h"

namespace
{
    constexpr const char* kViewSnapshotIdentifier = "ICVW";
    constexpr const char* kFlatBufferMediaType =
        "application/vnd.icax.flatbuffer";

    struct SViewSourceState final
    {
        std::string SourceID;
        std::string Role;
        std::shared_ptr<iCAX::Database::IEntityView> pEntityView;
        std::vector<iCAX::View::SViewProjectionField> Projection;
    };

    struct SViewState final
    {
        iCAX::View::ViewID ID;
        std::vector<SViewSourceState> Sources;
        std::string SnapshotURL;
        std::string LastFingerprint;
        uint64_t nSnapshotVersion = 0;
        uint32_t nUseCount = 1;
    };

    struct SComponentSnapshot final
    {
        std::string ComponentClass;
        bool bPresent = false;
        bool bEnabled = false;
        std::string DataJSON;
    };

    struct SRowSnapshot final
    {
        std::string EntityID;
        std::vector<std::string> SourceIDs;
        std::vector<SComponentSnapshot> Components;
    };

    struct SRowBuild final
    {
        std::set<std::string> SourceIDs;
        std::map<
            std::string,
            std::map<std::string, iCAX::View::SViewProjectionField>>
            FieldsByComponent;
    };

    void ValidateDefinition(IN const iCAX::View::SViewDefinition& Definition_)
    {
        if (Definition_.Sources.empty())
        {
            throw std::invalid_argument("View requires at least one source");
        }

        std::set<std::string> _SourceIDs;
        std::map<std::string, iCAX::View::SViewProjectionField> _AliasContract;
        for (const auto& _Source : Definition_.Sources)
        {
            if (_Source.SourceID.empty())
            {
                throw std::invalid_argument("View sourceId cannot be empty");
            }
            if (!_SourceIDs.insert(_Source.SourceID).second)
            {
                throw std::invalid_argument(
                    "View sourceId must be unique: " + _Source.SourceID);
            }
            for (const auto& _Field : _Source.Projection)
            {
                if (_Field.Alias.empty()
                    || _Field.ComponentClass.empty()
                    || _Field.PropertyName.empty())
                {
                    throw std::invalid_argument(
                        "View projection requires alias, component, and property");
                }
                const auto [_Iter, _bInserted] =
                    _AliasContract.emplace(_Field.Alias, _Field);
                if (!_bInserted && _Iter->second != _Field)
                {
                    throw std::invalid_argument(
                        "View projection alias has conflicting definitions: "
                        + _Field.Alias);
                }
            }
        }
    }

    iCAX::Data::Variant MakeProjectedValue(
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN const iCAX::Data::PropertyValue& Value_,
        IN const bool bResourceReference_,
        IN OUT std::vector<iCAX::Resource::CResourceReference>& Dependencies_)
    {
        if (!bResourceReference_ || !Value_.Is<std::string>())
        {
            return iCAX::Data::Variant(Value_);
        }
        const auto _URL = Value_.To<std::string>();
        const auto _Version = Resources_.GetVersion(_URL);
        iCAX::Data::ObjectMap _Reference;
        _Reference["url"] = _URL;
        _Reference["version"] = static_cast<unsigned long long>(_Version);
        if (!_URL.empty() && _Version != 0)
        {
            Dependencies_.push_back({ _URL, _Version });
        }
        return iCAX::Data::Variant(std::move(_Reference));
    }

    SComponentSnapshot MakeComponentSnapshot(
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::string& strComponentClass_,
        IN const std::map<
            std::string,
            iCAX::View::SViewProjectionField>& Fields_,
        IN OUT std::vector<iCAX::Resource::CResourceReference>& Dependencies_)
    {
        SComponentSnapshot _Snapshot;
        _Snapshot.ComponentClass = strComponentClass_;
        iCAX::Data::ObjectMap _Data;
        const auto _pComponent = pEntity_
            ? pEntity_->GetComponent(strComponentClass_)
            : nullptr;
        if (_pComponent)
        {
            _Snapshot.bPresent = true;
            _Snapshot.bEnabled = _pComponent->IsEnable();
            for (const auto& [_Alias, _Field] : Fields_)
            {
                try
                {
                    _Data[_Alias] = MakeProjectedValue(
                        Resources_,
                        _pComponent->GetProperty(_Field.PropertyName),
                        _Field.bResourceReference,
                        Dependencies_);
                }
                catch (...)
                {
                    // 单个可选投影字段失效不应破坏整个 View。
                }
            }
        }
        _Snapshot.DataJSON =
            iCAX::Data::VariantSerializer::Serialize(
                iCAX::Data::Variant(std::move(_Data)));
        return _Snapshot;
    }

    iCAX::Resource::CFlatBufferResource BuildSnapshot(
        IN const iCAX::View::ViewID& ViewID_,
        IN const uint64_t nRevision_,
        IN const std::vector<SViewSourceState>& Sources_,
        IN const std::vector<SRowSnapshot>& Rows_)
    {
        flatbuffers::FlatBufferBuilder _Builder;

        std::vector<flatbuffers::Offset<void>> _SourceTables;
        _SourceTables.reserve(Sources_.size());
        for (const auto& _Source : Sources_)
        {
            const auto _SourceID = _Builder.CreateString(_Source.SourceID);
            const auto _Role = _Builder.CreateString(_Source.Role);
            const auto _Start = _Builder.StartTable();
            _Builder.AddOffset(4, _SourceID);
            _Builder.AddOffset(6, _Role);
            _SourceTables.emplace_back(_Builder.EndTable(_Start));
        }
        const auto _SourceVector = _Builder.CreateVector(_SourceTables);

        std::vector<flatbuffers::Offset<void>> _RowTables;
        _RowTables.reserve(Rows_.size());
        for (const auto& _Row : Rows_)
        {
            std::vector<flatbuffers::Offset<flatbuffers::String>> _SourceIDs;
            _SourceIDs.reserve(_Row.SourceIDs.size());
            for (const auto& _SourceID : _Row.SourceIDs)
            {
                _SourceIDs.push_back(_Builder.CreateString(_SourceID));
            }
            const auto _SourceIDVector = _Builder.CreateVector(_SourceIDs);

            std::vector<flatbuffers::Offset<void>> _Components;
            _Components.reserve(_Row.Components.size());
            for (const auto& _Component : _Row.Components)
            {
                const auto _Class =
                    _Builder.CreateString(_Component.ComponentClass);
                const auto _Data = _Builder.CreateString(_Component.DataJSON);
                const auto _Start = _Builder.StartTable();
                _Builder.AddOffset(4, _Class);
                _Builder.AddElement<uint8_t>(
                    6,
                    _Component.bPresent ? 1 : 0,
                    0);
                _Builder.AddElement<uint8_t>(
                    8,
                    _Component.bEnabled ? 1 : 0,
                    0);
                _Builder.AddOffset(10, _Data);
                _Components.emplace_back(_Builder.EndTable(_Start));
            }
            const auto _ComponentVector = _Builder.CreateVector(_Components);
            const auto _EntityID = _Builder.CreateString(_Row.EntityID);
            const auto _Start = _Builder.StartTable();
            _Builder.AddOffset(4, _EntityID);
            _Builder.AddOffset(6, _SourceIDVector);
            _Builder.AddOffset(8, _ComponentVector);
            _RowTables.emplace_back(_Builder.EndTable(_Start));
        }
        const auto _RowVector = _Builder.CreateVector(_RowTables);
        const auto _View = _Builder.CreateString(iCAX::Data::to_string(ViewID_));
        const auto _Start = _Builder.StartTable();
        _Builder.AddElement<uint32_t>(4, 2, 0);
        _Builder.AddOffset(6, _View);
        _Builder.AddElement<uint64_t>(8, nRevision_, 0);
        _Builder.AddOffset(10, _SourceVector);
        _Builder.AddOffset(12, _RowVector);
        const auto _Root = _Builder.EndTable(_Start);
        _Builder.Finish(
            flatbuffers::Offset<void>(_Root),
            kViewSnapshotIdentifier);
        return iCAX::Resource::MakeFlatBufferResource(_Builder);
    }

    bool SourcesEquivalent(
        IN const std::vector<SViewSourceState>& Left_,
        IN const std::vector<SViewSourceState>& Right_)
    {
        if (Left_.size() != Right_.size())
        {
            return false;
        }
        for (size_t _Index = 0; _Index < Left_.size(); ++_Index)
        {
            const auto& _Left = Left_[_Index];
            const auto& _Right = Right_[_Index];
            if (_Left.SourceID != _Right.SourceID
                || _Left.Role != _Right.Role
                || _Left.pEntityView != _Right.pEntityView
                || _Left.Projection != _Right.Projection)
            {
                return false;
            }
        }
        return true;
    }

    void ReleaseState(
        IN iCAX::Database::IRepository& Repository_,
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN OUT SViewState& State_)
    {
        for (auto& _Source : State_.Sources)
        {
            if (!_Source.pEntityView)
            {
                continue;
            }
            for (uint32_t _Index = 0; _Index < State_.nUseCount; ++_Index)
            {
                Repository_.ReleaseEntityView(_Source.pEntityView);
            }
            _Source.pEntityView.reset();
        }
        if (!State_.SnapshotURL.empty())
        {
            (void)Resources_.Delete(State_.SnapshotURL);
        }
        State_.nUseCount = 0;
    }

    void PublishState(
        IN iCAX::Database::IRepository& Repository_,
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN OUT SViewState& State_)
    {
        std::map<std::string, SRowBuild> _BuildRows;
        for (const auto& _Source : State_.Sources)
        {
            if (!_Source.pEntityView)
            {
                continue;
            }
            for (const auto& _EntityID : _Source.pEntityView->GetEntityIDs())
            {
                auto& _Row = _BuildRows[iCAX::Data::to_string(_EntityID)];
                _Row.SourceIDs.insert(_Source.SourceID);
                for (const auto& _Field : _Source.Projection)
                {
                    _Row.FieldsByComponent[_Field.ComponentClass]
                        .emplace(_Field.Alias, _Field);
                }
            }
        }

        std::vector<iCAX::Resource::CResourceReference> _Dependencies;
        std::vector<SRowSnapshot> _Rows;
        _Rows.reserve(_BuildRows.size());
        std::string _Fingerprint;
        for (const auto& [_EntityID, _Build] : _BuildRows)
        {
            SRowSnapshot _Row;
            _Row.EntityID = _EntityID;
            _Row.SourceIDs.assign(
                _Build.SourceIDs.begin(),
                _Build.SourceIDs.end());
            const auto _ParsedID = iCAX::Data::uuid::from_string(_EntityID);
            const auto _pEntity = _ParsedID
                ? Repository_.GetEntity(*_ParsedID)
                : nullptr;
            for (const auto& [_ComponentClass, _Fields] :
                _Build.FieldsByComponent)
            {
                _Row.Components.push_back(MakeComponentSnapshot(
                    Resources_,
                    _pEntity,
                    _ComponentClass,
                    _Fields,
                    _Dependencies));
            }

            _Fingerprint += _Row.EntityID;
            _Fingerprint.push_back('\0');
            for (const auto& _SourceID : _Row.SourceIDs)
            {
                _Fingerprint += _SourceID;
                _Fingerprint.push_back('\0');
            }
            for (const auto& _Component : _Row.Components)
            {
                _Fingerprint += _Component.ComponentClass;
                _Fingerprint.push_back(_Component.bPresent ? '\1' : '\0');
                _Fingerprint.push_back(_Component.bEnabled ? '\1' : '\0');
                _Fingerprint += _Component.DataJSON;
                _Fingerprint.push_back('\0');
            }
            _Rows.push_back(std::move(_Row));
        }

        if (State_.nSnapshotVersion != 0
            && _Fingerprint == State_.LastFingerprint)
        {
            return;
        }

        const auto _Version = State_.nSnapshotVersion + 1;
        auto _Resource = BuildSnapshot(
            State_.ID,
            _Version,
            State_.Sources,
            _Rows);
        iCAX::Resource::CResourceInfo _Info;
        _Info.Name = "View " + iCAX::Data::to_string(State_.ID);
        _Info.Source = State_.SnapshotURL;
        _Info.MediaType = kFlatBufferMediaType;
        _Info.ResourceTypeID =
            iCAX::Resource::CFlatBufferResource::kResourceTypeName;
        _Info.FlatBufferIdentifier = kViewSnapshotIdentifier;
        _Info.nSchemaVersion = 2;
        _Info.nMinimumReaderVersion = 2;
        _Info.nVersion = _Version;
        _Info.nSize = _Resource.Size();
        _Info.Persistence =
            iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        _Info.Metadata["kind"] = "view.snapshot";
        _Info.Metadata["viewId"] = iCAX::Data::to_string(State_.ID);
        std::sort(_Dependencies.begin(), _Dependencies.end());
        _Dependencies.erase(
            std::unique(_Dependencies.begin(), _Dependencies.end()),
            _Dependencies.end());
        _Info.Dependencies = std::move(_Dependencies);
        Resources_.Set<iCAX::Resource::CFlatBufferResource>(
            State_.SnapshotURL,
            std::make_shared<iCAX::Resource::CFlatBufferResource>(
                std::move(_Resource)),
            _Info);
        State_.LastFingerprint = std::move(_Fingerprint);
        State_.nSnapshotVersion = _Version;
    }
}

struct iCAX::View::CViewSet::SImpl final
{
    SImpl(
        IN iCAX::Database::IRepository& Repository_,
        IN iCAX::Resource::CResourceLibrary& Resources_)
        : Repository(Repository_)
        , Resources(Resources_)
    {
    }

    iCAX::Database::IRepository& Repository;
    iCAX::Resource::CResourceLibrary& Resources;
    mutable std::mutex Mutex;
    std::map<ViewID, SViewState> States;
};

iCAX::View::CViewSet::CViewSet(
    IN iCAX::Database::IRepository& Repository_,
    IN iCAX::Resource::CResourceLibrary& Resources_)
    : m_pImpl(std::make_unique<SImpl>(Repository_, Resources_))
{
}

iCAX::View::CViewSet::~CViewSet()
{
    Clear();
}

void iCAX::View::CViewSet::Clear() noexcept
{
    std::map<ViewID, SViewState> _States;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        _States.swap(m_pImpl->States);
    }
    for (auto& [_, _State] : _States)
    {
        try
        {
            ReleaseState(
                m_pImpl->Repository,
                m_pImpl->Resources,
                _State);
        }
        catch (...)
        {
        }
    }
}

void iCAX::View::CViewSet::Publish()
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    for (auto& [_, _State] : m_pImpl->States)
    {
        PublishState(m_pImpl->Repository, m_pImpl->Resources, _State);
    }
}

iCAX::View::SViewHandle iCAX::View::CViewSet::GetOrCreate(
    IN const SViewDefinition& Definition_)
{
    ValidateDefinition(Definition_);
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    SViewState _State;
    _State.ID = iCAX::Data::GenerateNewUUID();
    try
    {
        for (const auto& _Definition : Definition_.Sources)
        {
            SViewSourceState _Source;
            _Source.SourceID = _Definition.SourceID;
            _Source.Role = _Definition.Role;
            _Source.pEntityView = m_pImpl->Repository.CreateEntityView(
                _Definition.Where,
                _Definition.Parameters);
            _Source.Projection = _Definition.Projection;
            _State.Sources.push_back(std::move(_Source));
        }

        const auto _Existing = std::find_if(
            m_pImpl->States.begin(),
            m_pImpl->States.end(),
            [&](const auto& Entry_)
            {
                return SourcesEquivalent(
                    Entry_.second.Sources,
                    _State.Sources);
            });
        if (_Existing != m_pImpl->States.end())
        {
            ++_Existing->second.nUseCount;
            return {
                _Existing->second.ID,
                _Existing->second.nSnapshotVersion,
                {
                    _Existing->second.SnapshotURL,
                    _Existing->second.nSnapshotVersion,
                },
                kViewSnapshotIdentifier,
            };
        }

        _State.SnapshotURL =
            m_pImpl->Resources.MakeResourceURL(_State.ID);
        PublishState(m_pImpl->Repository, m_pImpl->Resources, _State);
        const SViewHandle _Handle{
            _State.ID,
            _State.nSnapshotVersion,
            { _State.SnapshotURL, _State.nSnapshotVersion },
            kViewSnapshotIdentifier,
        };
        const auto [_Inserted, _bInserted] =
            m_pImpl->States.emplace(_State.ID, std::move(_State));
        if (!_bInserted)
        {
            throw std::logic_error("Generated duplicate View id");
        }
        return _Handle;
    }
    catch (...)
    {
        ReleaseState(
            m_pImpl->Repository,
            m_pImpl->Resources,
            _State);
        throw;
    }
}

bool iCAX::View::CViewSet::Release(IN const ViewID& ViewID_)
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    SViewState _State;
    const auto _Iter = m_pImpl->States.find(ViewID_);
    if (_Iter == m_pImpl->States.end())
    {
        return false;
    }
    if (_Iter->second.nUseCount > 1)
    {
        for (const auto& _Source : _Iter->second.Sources)
        {
            m_pImpl->Repository.ReleaseEntityView(_Source.pEntityView);
        }
        --_Iter->second.nUseCount;
        return true;
    }
    _State = std::move(_Iter->second);
    m_pImpl->States.erase(_Iter);
    ReleaseState(
        m_pImpl->Repository,
        m_pImpl->Resources,
        _State);
    return true;
}

size_t iCAX::View::CViewSet::Size() const noexcept
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    return m_pImpl->States.size();
}
