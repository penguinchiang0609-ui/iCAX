#include "pch.h"

#include "EntityViewSet.h"

#include "Database/IEntityView.h"
#include "Database/IRepository.h"
#include "PDO/IPDOHub.h"

namespace
{
    struct SEntityViewState final
    {
        iCAX::View::EntityViewID ViewID;
        std::shared_ptr<iCAX::Database::IEntityView> pEntityView;
        iCAX::PDO::PDOID nPDOID = 0;
        uint32_t nUseCount = 1;
    };

    void ReleaseState(
        IN iCAX::Database::IRepository& Repository_,
        IN iCAX::PDO::IPDOHub& PDOHub_,
        IN SEntityViewState& State_)
    {
        if (State_.nPDOID != 0)
        {
            PDOHub_.FreeSlot(State_.nPDOID);
        }
        if (State_.pEntityView)
        {
            for (uint32_t _nIndex = 0; _nIndex < State_.nUseCount; ++_nIndex)
            {
                Repository_.ReleaseEntityView(State_.pEntityView);
            }
        }
        State_.nUseCount = 0;
        State_.pEntityView.reset();
        State_.nPDOID = 0;
    }

    void PublishState(
        IN iCAX::PDO::IPDOHub& PDOHub_,
        IN SEntityViewState& State_)
    {
        if (!State_.pEntityView || State_.nPDOID == 0)
        {
            return;
        }
        iCAX::View::WriteEntityViewPDO(
            PDOHub_.GetSlot(State_.nPDOID),
            State_.pEntityView->GetRevision(),
            State_.pEntityView->GetEntityIDs());
    }
}

struct iCAX::View::CEntityViewSet::SImpl final
{
    SImpl(
        IN iCAX::Database::IRepository& Repository_,
        IN iCAX::PDO::IPDOHub& PDOHub_)
        : Repository(Repository_)
        , PDOHub(PDOHub_)
    {
    }

    iCAX::Database::IRepository& Repository;
    iCAX::PDO::IPDOHub& PDOHub;
    std::map<EntityViewID, SEntityViewState> States;
};

iCAX::View::CEntityViewSet::CEntityViewSet(
    IN iCAX::Database::IRepository& Repository_,
    IN iCAX::PDO::IPDOHub& PDOHub_)
    : m_pImpl(std::make_unique<SImpl>(Repository_, PDOHub_))
{
}

iCAX::View::CEntityViewSet::~CEntityViewSet()
{
    Clear();
}

void iCAX::View::CEntityViewSet::Clear() noexcept
{
    std::map<EntityViewID, SEntityViewState> _States;
    _States.swap(m_pImpl->States);
    for (auto& [_, _State] : _States)
    {
        try
        {
            ReleaseState(m_pImpl->Repository, m_pImpl->PDOHub, _State);
        }
        catch (...)
        {
            // Scene teardown must continue even if one stale entry is corrupt.
        }
    }
}

void iCAX::View::CEntityViewSet::Publish()
{
    for (auto& [_, _State] : m_pImpl->States)
    {
        PublishState(m_pImpl->PDOHub, _State);
    }
}

iCAX::View::SEntityViewHandle iCAX::View::CEntityViewSet::GetOrCreate(
    IN const iCAX::Database::SEntityWhere& Where_,
    IN const iCAX::Data::ObjectMap& Parameters_)
{
    SEntityViewState _State;
    _State.ViewID = iCAX::Data::GenerateNewUUID();
    _State.pEntityView =
        m_pImpl->Repository.CreateEntityView(Where_, Parameters_);

    try
    {
        const auto _Iter = std::find_if(
            m_pImpl->States.begin(),
            m_pImpl->States.end(),
            [&](const auto& Entry_)
            {
                return Entry_.second.pEntityView == _State.pEntityView;
            });
        if (_Iter != m_pImpl->States.end())
        {
            ++_Iter->second.nUseCount;
            return SEntityViewHandle{
                _Iter->second.ViewID,
                _Iter->second.pEntityView->GetRevision(),
                _Iter->second.nPDOID,
                kEntityViewPDOLayoutVersion,
                GetEntityViewPDOPayloadSize(kDefaultEntityViewCapacity),
                kDefaultEntityViewCapacity,
            };
        }

        const auto _Decl = MakeEntityViewPDODecl(
            _State.ViewID,
            kDefaultEntityViewCapacity);
        _State.nPDOID = _Decl.nID;
        m_pImpl->PDOHub.AllocateSlot(_Decl);

        SEntityViewHandle _Handle{
            _State.ViewID,
            _State.pEntityView->GetRevision(),
            _Decl.nID,
            _Decl.nVersion,
            static_cast<uint64_t>(_Decl.nPayloadSize),
            kDefaultEntityViewCapacity,
        };

        const auto [_InsertedIter, _bInserted] =
            m_pImpl->States.emplace(_State.ViewID, std::move(_State));
        if (!_bInserted)
        {
            throw std::logic_error("Generated duplicate EntityView id");
        }
        return _Handle;
    }
    catch (...)
    {
        ReleaseState(m_pImpl->Repository, m_pImpl->PDOHub, _State);
        throw;
    }
}

bool iCAX::View::CEntityViewSet::Release(IN const EntityViewID& ViewID_)
{
    SEntityViewState _State;
    const auto _Iter = m_pImpl->States.find(ViewID_);
    if (_Iter == m_pImpl->States.end())
    {
        return false;
    }
    if (_Iter->second.nUseCount > 1)
    {
        m_pImpl->Repository.ReleaseEntityView(
            _Iter->second.pEntityView);
        --_Iter->second.nUseCount;
        return true;
    }
    _State = std::move(_Iter->second);
    m_pImpl->States.erase(_Iter);
    ReleaseState(m_pImpl->Repository, m_pImpl->PDOHub, _State);
    return true;
}

size_t iCAX::View::CEntityViewSet::Size() const noexcept
{
    return m_pImpl->States.size();
}
