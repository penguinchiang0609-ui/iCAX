#include "pch.h"

#include "ProjectDocument.h"

namespace
{
    using namespace iCAX::ProjectFile;

    constexpr const char* kValueTypeKey = "__project_file_type";
    constexpr const char* kValueURLKey = "url";
    constexpr const char* kValueVersionKey = "version";

    void RewriteVariantReference(
        IN OUT iCAX::Data::Variant& Value_,
        IN const CProjectResourceReference& Previous_,
        IN const CProjectResourceReference& Current_)
    {
        const auto _Reference = TryGetResourceReferenceValue(Value_);
        if (_Reference && *_Reference == Previous_)
        {
            Value_ = MakeResourceReferenceValue(Current_);
            return;
        }

        if (Value_.Is<iCAX::Data::ObjectMap>())
        {
            auto _Object = Value_.To<iCAX::Data::ObjectMap>();
            for (auto& [_, _Child] : _Object)
            {
                RewriteVariantReference(
                    _Child,
                    Previous_,
                    Current_);
            }
            Value_ = iCAX::Data::Variant(_Object);
            return;
        }
        if (Value_.Is<iCAX::Data::VariantArray>())
        {
            auto _Array = Value_.To<iCAX::Data::VariantArray>();
            for (auto& _Child : _Array)
            {
                RewriteVariantReference(
                    _Child,
                    Previous_,
                    Current_);
            }
            Value_ = iCAX::Data::Variant(_Array);
        }
    }

    void RewriteObjectReferences(
        IN OUT iCAX::Data::ObjectMap& Object_,
        IN const CProjectResourceReference& Previous_,
        IN const CProjectResourceReference& Current_)
    {
        for (auto& [_, _Value] : Object_)
        {
            RewriteVariantReference(_Value, Previous_, Current_);
        }
    }

    void CollectVariantReferences(
        IN const iCAX::Data::Variant& Value_,
        OUT std::vector<CProjectResourceReference>& References_)
    {
        const auto _Reference = TryGetResourceReferenceValue(Value_);
        if (_Reference)
        {
            References_.push_back(*_Reference);
            return;
        }
        if (Value_.Is<iCAX::Data::ObjectMap>())
        {
            const auto _Object = Value_.To<iCAX::Data::ObjectMap>();
            for (const auto& [_, _Child] : _Object)
            {
                CollectVariantReferences(_Child, References_);
            }
            return;
        }
        if (Value_.Is<iCAX::Data::VariantArray>())
        {
            const auto _Array = Value_.To<iCAX::Data::VariantArray>();
            for (const auto& _Child : _Array)
            {
                CollectVariantReferences(_Child, References_);
            }
        }
    }

    void CollectObjectReferences(
        IN const iCAX::Data::ObjectMap& Object_,
        OUT std::vector<CProjectResourceReference>& References_)
    {
        for (const auto& [_, _Value] : Object_)
        {
            CollectVariantReferences(_Value, References_);
        }
    }

    std::string DescribeReference(
        IN const CProjectResourceReference& Reference_)
    {
        return Reference_.URL + "@" +
            std::to_string(Reference_.nVersion);
    }
}

bool iCAX::ProjectFile::CProjectResourceReference::IsValid() const noexcept
{
    return !URL.empty() && nVersion != 0;
}

bool iCAX::ProjectFile::CProjectResourceReference::operator<(
    IN const CProjectResourceReference& Right_) const noexcept
{
    return URL != Right_.URL
        ? URL < Right_.URL
        : nVersion < Right_.nVersion;
}

void iCAX::ProjectFile::CProjectDocument::Canonicalize()
{
    std::sort(
        Entities.begin(),
        Entities.end(),
        [](const auto& Left_, const auto& Right_)
        {
            return Left_.EntityID < Right_.EntityID;
        });
    std::sort(
        Components.begin(),
        Components.end(),
        [](const auto& Left_, const auto& Right_)
        {
            return std::tie(
                Left_.EntityID,
                Left_.ComponentClass) <
                std::tie(
                    Right_.EntityID,
                    Right_.ComponentClass);
        });
    for (auto& _Resource : Resources)
    {
        std::sort(
            _Resource.Dependencies.begin(),
            _Resource.Dependencies.end());
    }
    std::sort(
        Resources.begin(),
        Resources.end(),
        [](const auto& Left_, const auto& Right_)
        {
            return Left_.Reference < Right_.Reference;
        });
}

iCAX::ProjectFile::CProjectResourceRecord*
iCAX::ProjectFile::CProjectDocument::FindResource(
    IN const CProjectResourceReference& Reference_)
{
    const auto _Iter = std::find_if(
        Resources.begin(),
        Resources.end(),
        [&](const auto& Resource_)
        {
            return Resource_.Reference == Reference_;
        });
    return _Iter == Resources.end() ? nullptr : &*_Iter;
}

const iCAX::ProjectFile::CProjectResourceRecord*
iCAX::ProjectFile::CProjectDocument::FindResource(
    IN const CProjectResourceReference& Reference_) const
{
    const auto _Iter = std::find_if(
        Resources.begin(),
        Resources.end(),
        [&](const auto& Resource_)
        {
            return Resource_.Reference == Reference_;
        });
    return _Iter == Resources.end() ? nullptr : &*_Iter;
}

void iCAX::ProjectFile::CProjectDocument::RewriteResourceReference(
    IN const CProjectResourceReference& Previous_,
    IN const CProjectResourceReference& Current_)
{
    if (!Previous_.IsValid() || !Current_.IsValid())
    {
        throw std::invalid_argument(
            "Resource reference rewrite requires valid references");
    }

    RewriteObjectReferences(
        Info.ProjectSettings,
        Previous_,
        Current_);
    RewriteObjectReferences(
        Info.MainSceneSettings,
        Previous_,
        Current_);
    for (auto& _Component : Components)
    {
        RewriteObjectReferences(
            _Component.Properties,
            Previous_,
            Current_);
    }
    for (auto& _Resource : Resources)
    {
        for (auto& _Dependency : _Resource.Dependencies)
        {
            if (_Dependency == Previous_)
            {
                _Dependency = Current_;
            }
        }
    }
}

iCAX::Data::Variant iCAX::ProjectFile::MakeResourceReferenceValue(
    IN const CProjectResourceReference& Reference_)
{
    if (!Reference_.IsValid())
    {
        throw std::invalid_argument(
            "Cannot encode an invalid resource reference");
    }
    iCAX::Data::ObjectMap _Object;
    _Object[kValueTypeKey] =
        std::string(kResourceReferenceValueTag);
    _Object[kValueURLKey] = Reference_.URL;
    _Object[kValueVersionKey] =
        static_cast<unsigned long long>(Reference_.nVersion);
    return iCAX::Data::Variant(_Object);
}

std::optional<iCAX::ProjectFile::CProjectResourceReference>
iCAX::ProjectFile::TryGetResourceReferenceValue(
    IN const iCAX::Data::Variant& Value_)
{
    if (!Value_.Is<iCAX::Data::ObjectMap>())
    {
        return std::nullopt;
    }
    const auto _Object = Value_.To<iCAX::Data::ObjectMap>();
    const auto _Type = _Object.find(kValueTypeKey);
    const auto _URL = _Object.find(kValueURLKey);
    const auto _Version = _Object.find(kValueVersionKey);
    if (_Type == _Object.end() ||
        !_Type->second.Is<std::string>() ||
        _Type->second.To<std::string>() !=
            kResourceReferenceValueTag)
    {
        return std::nullopt;
    }
    if (_URL == _Object.end() ||
        !_URL->second.Is<std::string>() ||
        _Version == _Object.end() ||
        !_Version->second.Is<unsigned long long>())
    {
        throw std::invalid_argument(
            "Malformed project resource reference value");
    }
    CProjectResourceReference _Reference;
    _Reference.URL = _URL->second.To<std::string>();
    _Reference.nVersion = static_cast<uint64_t>(
        _Version->second.To<unsigned long long>());
    if (!_Reference.IsValid())
    {
        throw std::invalid_argument(
            "Malformed project resource reference identity");
    }
    return _Reference;
}

std::vector<std::string> iCAX::ProjectFile::ValidateProjectDocument(
    IN const CProjectDocument& Document_)
{
    std::vector<std::string> _Errors;
    if (Document_.Info.Magic.empty())
    {
        _Errors.push_back("Document magic is empty");
    }
    if (Document_.Info.ProductID.empty())
    {
        _Errors.push_back("Document product id is empty");
    }
    if (Document_.Info.FormatVersion.empty())
    {
        _Errors.push_back("Document format version is empty");
    }
    if (Document_.Info.nFormatRevision == 0)
    {
        _Errors.push_back("Document format revision must be non-zero");
    }
    if (Document_.Info.ProjectID.is_nil())
    {
        _Errors.push_back("Document project id is nil");
    }
    if (Document_.Info.MainSceneID.is_nil())
    {
        _Errors.push_back("Document main scene id is nil");
    }

    std::set<iCAX::Data::uuid> _EntityIDs;
    for (const auto& _Entity : Document_.Entities)
    {
        if (_Entity.EntityID.is_nil())
        {
            _Errors.push_back("Entity id is nil");
            continue;
        }
        if (!_EntityIDs.insert(_Entity.EntityID).second)
        {
            _Errors.push_back(
                "Duplicate entity: " +
                iCAX::Data::to_string(_Entity.EntityID));
        }
    }

    std::set<std::pair<iCAX::Data::uuid, std::string>>
        _ComponentKeys;
    for (const auto& _Component : Document_.Components)
    {
        if (_Component.ComponentClass.empty())
        {
            _Errors.push_back("Component class is empty");
        }
        if (!_EntityIDs.contains(_Component.EntityID))
        {
            _Errors.push_back(
                "Component references missing entity: " +
                iCAX::Data::to_string(_Component.EntityID) +
                "/" + _Component.ComponentClass);
        }
        if (!_ComponentKeys.emplace(
            _Component.EntityID,
            _Component.ComponentClass).second)
        {
            _Errors.push_back(
                "Duplicate component: " +
                iCAX::Data::to_string(_Component.EntityID) +
                "/" + _Component.ComponentClass);
        }
    }

    std::map<CProjectResourceReference,
        const CProjectResourceRecord*> _Resources;
    for (const auto& _Resource : Document_.Resources)
    {
        if (!_Resource.Reference.IsValid())
        {
            _Errors.push_back("Resource identity is invalid");
            continue;
        }
        if (_Resource.ResourceTypeID.empty())
        {
            _Errors.push_back(
                "Resource type is empty: " +
                DescribeReference(_Resource.Reference));
        }
        if (_Resource.nSchemaVersion == 0)
        {
            _Errors.push_back(
                "Resource schema version must be non-zero: " +
                DescribeReference(_Resource.Reference));
        }
        if (_Resource.Persistence ==
                EProjectResourcePersistence::External &&
            _Resource.Source.empty())
        {
            _Errors.push_back(
                "External resource source is empty: " +
                DescribeReference(_Resource.Reference));
        }
        if (_Resource.Persistence ==
                EProjectResourcePersistence::External &&
            !_Resource.Body.empty())
        {
            _Errors.push_back(
                "External resource contains embedded body: " +
                DescribeReference(_Resource.Reference));
        }
        if (!_Resources.emplace(
            _Resource.Reference,
            &_Resource).second)
        {
            _Errors.push_back(
                "Duplicate resource: " +
                DescribeReference(_Resource.Reference));
        }
    }

    for (const auto& _Resource : Document_.Resources)
    {
        std::set<CProjectResourceReference> _Dependencies;
        for (const auto& _Dependency : _Resource.Dependencies)
        {
            if (!_Dependencies.insert(_Dependency).second)
            {
                _Errors.push_back(
                    "Duplicate resource dependency: " +
                    DescribeReference(_Resource.Reference) +
                    " -> " + DescribeReference(_Dependency));
            }
            if (_Dependency == _Resource.Reference)
            {
                _Errors.push_back(
                    "Resource depends on itself: " +
                    DescribeReference(_Resource.Reference));
                continue;
            }
            if (!_Resources.contains(_Dependency))
            {
                _Errors.push_back(
                    "Resource dependency is missing: " +
                    DescribeReference(_Resource.Reference) +
                    " -> " + DescribeReference(_Dependency));
            }
        }
    }

    std::map<CProjectResourceReference, uint8_t> _VisitState;
    std::function<void(const CProjectResourceReference&)> _Visit;
    _Visit = [&](const CProjectResourceReference& Reference_)
    {
        auto& _State = _VisitState[Reference_];
        if (_State == 2)
        {
            return;
        }
        if (_State == 1)
        {
            _Errors.push_back(
                "Resource dependency cycle contains: " +
                DescribeReference(Reference_));
            return;
        }
        _State = 1;
        const auto _Resource = _Resources.find(Reference_);
        if (_Resource != _Resources.end())
        {
            for (const auto& _Dependency :
                _Resource->second->Dependencies)
            {
                if (_Resources.contains(_Dependency))
                {
                    _Visit(_Dependency);
                }
            }
        }
        _State = 2;
    };
    for (const auto& [_Reference, _] : _Resources)
    {
        _Visit(_Reference);
    }

    std::vector<CProjectResourceReference> _RootReferences;
    CollectObjectReferences(
        Document_.Info.ProjectSettings,
        _RootReferences);
    CollectObjectReferences(
        Document_.Info.MainSceneSettings,
        _RootReferences);
    for (const auto& _Component : Document_.Components)
    {
        CollectObjectReferences(
            _Component.Properties,
            _RootReferences);
    }
    for (const auto& _Reference : _RootReferences)
    {
        if (!_Resources.contains(_Reference))
        {
            _Errors.push_back(
                "Document resource reference is missing: " +
                DescribeReference(_Reference));
        }
    }
    return _Errors;
}

void iCAX::ProjectFile::RequireValidProjectDocument(
    IN const CProjectDocument& Document_)
{
    const auto _Errors = ValidateProjectDocument(Document_);
    if (_Errors.empty())
    {
        return;
    }
    std::ostringstream _Message;
    _Message << "Project document validation failed";
    for (const auto& _Error : _Errors)
    {
        _Message << "\n- " << _Error;
    }
    throw std::invalid_argument(_Message.str());
}

