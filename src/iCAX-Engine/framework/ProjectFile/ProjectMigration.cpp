#include "pch.h"

#include "ProjectMigration.h"

namespace
{
    using namespace iCAX::ProjectFile;

    std::string DescribeReference(
        IN const CProjectResourceReference& Reference_)
    {
        return Reference_.URL + "@" +
            std::to_string(Reference_.nVersion);
    }

    std::vector<size_t> DependencyFirstOrder(
        IN const CProjectDocument& Document_)
    {
        std::map<CProjectResourceReference, size_t> _Indices;
        for (size_t i = 0; i < Document_.Resources.size(); ++i)
        {
            _Indices.emplace(
                Document_.Resources[i].Reference,
                i);
        }

        std::vector<uint8_t> _State(
            Document_.Resources.size(),
            0);
        std::vector<size_t> _Order;
        std::function<void(size_t)> _Visit;
        _Visit = [&](const size_t nIndex_)
        {
            if (_State[nIndex_] == 2)
            {
                return;
            }
            if (_State[nIndex_] == 1)
            {
                throw std::invalid_argument(
                    "Resource dependency graph contains a cycle");
            }
            _State[nIndex_] = 1;
            for (const auto& _Dependency :
                Document_.Resources[nIndex_].Dependencies)
            {
                const auto _Iter = _Indices.find(_Dependency);
                if (_Iter == _Indices.end())
                {
                    throw std::invalid_argument(
                        "Resource dependency is missing: " +
                        DescribeReference(_Dependency));
                }
                _Visit(_Iter->second);
            }
            _State[nIndex_] = 2;
            _Order.push_back(nIndex_);
        };

        for (size_t i = 0; i < Document_.Resources.size(); ++i)
        {
            _Visit(i);
        }
        return _Order;
    }
}

struct iCAX::ProjectFile::CProjectMigrationRegistry::SImpl final
{
    using CDocumentKey = std::pair<std::string, uint32_t>;
    using CResourceKey = std::pair<std::string, uint32_t>;

    std::map<CDocumentKey,
        std::shared_ptr<IProjectDocumentMigration>>
        DocumentMigrations;
    std::map<CResourceKey,
        std::shared_ptr<IProjectResourceMigration>>
        ResourceMigrations;
    std::map<std::string, uint32_t> CurrentResourceSchemas;
};

void iCAX::ProjectFile::CProjectMigrationContext::Note(
    IN std::string Message_)
{
    Diagnostics.push_back(std::move(Message_));
}

iCAX::ProjectFile::CProjectMigrationRegistry::
CProjectMigrationRegistry()
    : m_pImpl(std::make_unique<SImpl>())
{
}

iCAX::ProjectFile::CProjectMigrationRegistry::
~CProjectMigrationRegistry() = default;

void iCAX::ProjectFile::CProjectMigrationRegistry::
RegisterDocumentMigration(
    IN std::shared_ptr<IProjectDocumentMigration> pMigration_)
{
    if (!pMigration_)
    {
        throw std::invalid_argument(
            "Document migration is null");
    }
    const auto _ProductID = pMigration_->ProductID();
    const auto _From = pMigration_->FromRevision();
    const auto _To = pMigration_->ToRevision();
    if (_ProductID.empty() || _From == 0 || _To <= _From ||
        pMigration_->ToFormatVersion().empty())
    {
        throw std::invalid_argument(
            "Document migration descriptor is invalid");
    }
    if (!m_pImpl->DocumentMigrations.emplace(
        SImpl::CDocumentKey(_ProductID, _From),
        std::move(pMigration_)).second)
    {
        throw std::invalid_argument(
            "Duplicate document migration: " +
            _ProductID + "@" + std::to_string(_From));
    }
}

void iCAX::ProjectFile::CProjectMigrationRegistry::
RegisterResourceMigration(
    IN std::shared_ptr<IProjectResourceMigration> pMigration_)
{
    if (!pMigration_)
    {
        throw std::invalid_argument(
            "Resource migration is null");
    }
    const auto _Type = pMigration_->ResourceTypeID();
    const auto _From = pMigration_->FromSchemaVersion();
    const auto _To = pMigration_->ToSchemaVersion();
    if (_Type.empty() || _From == 0 || _To <= _From)
    {
        throw std::invalid_argument(
            "Resource migration descriptor is invalid");
    }
    if (!m_pImpl->ResourceMigrations.emplace(
        SImpl::CResourceKey(_Type, _From),
        std::move(pMigration_)).second)
    {
        throw std::invalid_argument(
            "Duplicate resource migration: " +
            _Type + "@" + std::to_string(_From));
    }
}

void iCAX::ProjectFile::CProjectMigrationRegistry::
SetCurrentResourceSchema(
    IN const std::string& ResourceTypeID_,
    IN const uint32_t nSchemaVersion_)
{
    if (ResourceTypeID_.empty() || nSchemaVersion_ == 0)
    {
        throw std::invalid_argument(
            "Current resource schema descriptor is invalid");
    }
    m_pImpl->CurrentResourceSchemas[ResourceTypeID_] =
        nSchemaVersion_;
}

iCAX::ProjectFile::CProjectDocument
iCAX::ProjectFile::CProjectMigrationRegistry::UpgradeToCurrent(
    IN const CProjectDocument& Source_,
    IN const uint32_t nTargetRevision_,
    IN const std::string& strTargetFormatVersion_,
    OUT CProjectMigrationContext* pContext_) const
{
    if (nTargetRevision_ == 0 ||
        strTargetFormatVersion_.empty())
    {
        throw std::invalid_argument(
            "Target document version is invalid");
    }
    RequireValidProjectDocument(Source_);
    if (Source_.Info.nFormatRevision > nTargetRevision_)
    {
        throw std::invalid_argument(
            "Newer project documents cannot be downgraded");
    }

    // The caller-visible source and context are not touched until every
    // migration and the final validation have succeeded.
    CProjectDocument _Working = Source_;
    CProjectMigrationContext _Context;

    while (_Working.Info.nFormatRevision < nTargetRevision_)
    {
        const auto _From = _Working.Info.nFormatRevision;
        const auto _Iter = m_pImpl->DocumentMigrations.find(
            SImpl::CDocumentKey(
                _Working.Info.ProductID,
                _From));
        if (_Iter == m_pImpl->DocumentMigrations.end())
        {
            throw std::invalid_argument(
                "Missing document migration: " +
                _Working.Info.ProductID + "@" +
                std::to_string(_From));
        }
        const auto& _Migration = *_Iter->second;
        if (_Migration.ToRevision() > nTargetRevision_)
        {
            throw std::invalid_argument(
                "Document migration overshoots target revision");
        }

        _Migration.Upgrade(_Working, _Context);
        if (_Working.Info.ProductID !=
            _Migration.ProductID())
        {
            throw std::invalid_argument(
                "Document migration changed product id");
        }
        _Working.Info.nFormatRevision =
            _Migration.ToRevision();
        _Working.Info.FormatVersion =
            _Migration.ToFormatVersion();
        RequireValidProjectDocument(_Working);
    }

    std::map<std::string, uint64_t> _NextVersions;
    for (const auto& _Resource : _Working.Resources)
    {
        auto& _Next = _NextVersions[_Resource.Reference.URL];
        _Next = std::max(
            _Next,
            _Resource.Reference.nVersion);
    }

    const auto _Order = DependencyFirstOrder(_Working);
    std::map<CProjectResourceReference,
        CProjectResourceReference> _Rewrites;
    for (const auto nIndex : _Order)
    {
        auto& _Resource = _Working.Resources[nIndex];
        const auto _PreviousReference = _Resource.Reference;
        bool _Changed = false;

        for (auto& _Dependency : _Resource.Dependencies)
        {
            const auto _Rewrite = _Rewrites.find(_Dependency);
            if (_Rewrite != _Rewrites.end())
            {
                _Dependency = _Rewrite->second;
                _Changed = true;
            }
        }

        const auto _TargetIter =
            m_pImpl->CurrentResourceSchemas.find(
                _Resource.ResourceTypeID);
        if (_TargetIter !=
            m_pImpl->CurrentResourceSchemas.end())
        {
            const auto _TargetSchema = _TargetIter->second;
            if (_Resource.nSchemaVersion > _TargetSchema)
            {
                throw std::invalid_argument(
                    "Newer resource schema cannot be downgraded: " +
                    DescribeReference(_Resource.Reference));
            }
            while (_Resource.nSchemaVersion < _TargetSchema)
            {
                const auto _From = _Resource.nSchemaVersion;
                const auto _MigrationIter =
                    m_pImpl->ResourceMigrations.find(
                        SImpl::CResourceKey(
                            _Resource.ResourceTypeID,
                            _From));
                if (_MigrationIter ==
                    m_pImpl->ResourceMigrations.end())
                {
                    throw std::invalid_argument(
                        "Missing resource migration: " +
                        _Resource.ResourceTypeID + "@" +
                        std::to_string(_From));
                }
                const auto& _Migration =
                    *_MigrationIter->second;
                if (_Migration.ToSchemaVersion() >
                    _TargetSchema)
                {
                    throw std::invalid_argument(
                        "Resource migration overshoots target schema");
                }

                const auto _IdentityBefore =
                    _Resource.Reference;
                const auto _TypeBefore =
                    _Resource.ResourceTypeID;
                _Migration.Upgrade(_Resource, _Context);
                if (_Resource.Reference != _IdentityBefore ||
                    _Resource.ResourceTypeID != _TypeBefore)
                {
                    throw std::invalid_argument(
                        "Resource migration changed identity or type");
                }
                _Resource.nSchemaVersion =
                    _Migration.ToSchemaVersion();
                _Changed = true;
            }
        }

        if (_Changed)
        {
            auto& _Next =
                _NextVersions[_PreviousReference.URL];
            if (_Next ==
                std::numeric_limits<uint64_t>::max())
            {
                throw std::overflow_error(
                    "Resource version space is exhausted");
            }
            _Resource.Reference.nVersion = ++_Next;
            _Rewrites.emplace(
                _PreviousReference,
                _Resource.Reference);
        }
    }

    for (const auto& [_Previous, _Current] : _Rewrites)
    {
        _Working.RewriteResourceReference(
            _Previous,
            _Current);
    }

    _Working.Info.nFormatRevision = nTargetRevision_;
    _Working.Info.FormatVersion = strTargetFormatVersion_;
    _Working.Canonicalize();
    RequireValidProjectDocument(_Working);

    if (pContext_)
    {
        *pContext_ = std::move(_Context);
    }
    return _Working;
}
