#include "pch.h"
#include "SceneObjectRegistry.h"

#include <format>
#include <stdexcept>

iCAX::Project::SceneObjectID iCAX::Project::CSceneObjectRegistry::GetOrCreateEntityObject(
    IN const iCAX::Data::uuid& EntityID_)
{
    if (EntityID_.is_nil())
    {
        throw std::invalid_argument("SceneObjectRegistry requires a non-nil EntityID");
    }

    auto _Iter = m_ObjectByEntity.find(EntityID_);
    if (_Iter != m_ObjectByEntity.end())
    {
        return _Iter->second;
    }

    const auto _ObjectID = static_cast<SceneObjectID>(AllocateID());
    CSceneObjectBinding _Binding;
    _Binding.nObjectID = _ObjectID;
    _Binding.nTransformID = static_cast<TransformID>(_ObjectID);
    _Binding.EntityID = EntityID_;

    m_ObjectByEntity.emplace(EntityID_, _ObjectID);
    m_ObjectByTransform.emplace(_Binding.nTransformID, _ObjectID);
    m_Objects.emplace(_ObjectID, std::move(_Binding));
    return _ObjectID;
}

iCAX::Project::SceneObjectID iCAX::Project::CSceneObjectRegistry::GetOrCreateAliasObject(
    IN const std::string& strAliasNamespace_,
    IN const std::string& strAliasKey_,
    IN const std::string& strName_)
{
    if (strAliasNamespace_.empty())
    {
        throw std::invalid_argument("Scene object alias namespace cannot be empty");
    }
    if (strAliasKey_.empty())
    {
        throw std::invalid_argument("Scene object alias key cannot be empty");
    }

    const auto _Key = MakeAliasKey(strAliasNamespace_, strAliasKey_);
    auto _Iter = m_ObjectByAlias.find(_Key);
    if (_Iter != m_ObjectByAlias.end())
    {
        return _Iter->second;
    }

    const auto _ObjectID = static_cast<SceneObjectID>(AllocateID());
    CSceneObjectBinding _Binding;
    _Binding.nObjectID = _ObjectID;
    _Binding.nTransformID = static_cast<TransformID>(_ObjectID);
    _Binding.Aliases.push_back(CSceneObjectAlias{ strAliasNamespace_, strAliasKey_ });
    _Binding.Name = strName_;

    m_ObjectByAlias.emplace(_Key, _ObjectID);
    m_ObjectByTransform.emplace(_Binding.nTransformID, _ObjectID);
    m_Objects.emplace(_ObjectID, std::move(_Binding));
    return _ObjectID;
}

const iCAX::Project::CSceneObjectBinding* iCAX::Project::CSceneObjectRegistry::FindObject(
    IN SceneObjectID nObjectID_) const
{
    const auto _Iter = m_Objects.find(nObjectID_);
    return _Iter == m_Objects.end() ? nullptr : &_Iter->second;
}

std::optional<iCAX::Project::SceneObjectID> iCAX::Project::CSceneObjectRegistry::FindObjectByEntity(
    IN const iCAX::Data::uuid& EntityID_) const
{
    const auto _Iter = m_ObjectByEntity.find(EntityID_);
    if (_Iter == m_ObjectByEntity.end())
    {
        return std::nullopt;
    }
    return _Iter->second;
}

bool iCAX::Project::CSceneObjectRegistry::BindAlias(
    IN SceneObjectID nObjectID_,
    IN const std::string& strAliasNamespace_,
    IN const std::string& strAliasKey_)
{
    auto _ObjectIter = m_Objects.find(nObjectID_);
    if (_ObjectIter == m_Objects.end())
    {
        throw std::out_of_range(std::format("Scene object does not exist: {}", nObjectID_));
    }
    if (strAliasNamespace_.empty())
    {
        throw std::invalid_argument("Scene object alias namespace cannot be empty");
    }
    if (strAliasKey_.empty())
    {
        throw std::invalid_argument("Scene object alias key cannot be empty");
    }

    const auto _Key = MakeAliasKey(strAliasNamespace_, strAliasKey_);
    auto _AliasIter = m_ObjectByAlias.find(_Key);
    if (_AliasIter != m_ObjectByAlias.end())
    {
        if (_AliasIter->second == nObjectID_)
        {
            return true;
        }
        throw std::invalid_argument("Scene object alias is already bound to another object");
    }

    _ObjectIter->second.Aliases.push_back(CSceneObjectAlias{ strAliasNamespace_, strAliasKey_ });
    m_ObjectByAlias.emplace(_Key, nObjectID_);
    return true;
}

std::optional<iCAX::Project::SceneObjectID> iCAX::Project::CSceneObjectRegistry::FindObjectByAlias(
    IN const std::string& strAliasNamespace_,
    IN const std::string& strAliasKey_) const
{
    const auto _Iter = m_ObjectByAlias.find(MakeAliasKey(strAliasNamespace_, strAliasKey_));
    if (_Iter == m_ObjectByAlias.end())
    {
        return std::nullopt;
    }
    return _Iter->second;
}

std::optional<iCAX::Project::SceneObjectID> iCAX::Project::CSceneObjectRegistry::FindObjectByTransform(
    IN TransformID nTransformID_) const
{
    const auto _Iter = m_ObjectByTransform.find(nTransformID_);
    if (_Iter == m_ObjectByTransform.end())
    {
        return std::nullopt;
    }
    return _Iter->second;
}

iCAX::Project::TransformID iCAX::Project::CSceneObjectRegistry::GetTransformID(
    IN SceneObjectID nObjectID_) const
{
    const auto _pBinding = FindObject(nObjectID_);
    if (!_pBinding)
    {
        throw std::out_of_range(std::format("Scene object does not exist: {}", nObjectID_));
    }
    return _pBinding->nTransformID;
}

iCAX::Data::uuid iCAX::Project::CSceneObjectRegistry::GetEntityID(
    IN SceneObjectID nObjectID_) const
{
    const auto _pBinding = FindObject(nObjectID_);
    if (!_pBinding || _pBinding->EntityID.is_nil())
    {
        return iCAX::Data::GenerateNilUUID();
    }
    return _pBinding->EntityID;
}

iCAX::Project::SceneGeometryID iCAX::Project::CSceneObjectRegistry::GetOrCreateGeometry(
    IN const std::string& strResourceID_,
    IN const std::string& strPurpose_)
{
    if (strResourceID_.empty())
    {
        throw std::invalid_argument("Scene geometry resource ID cannot be empty");
    }
    if (strPurpose_.empty())
    {
        throw std::invalid_argument("Scene geometry purpose cannot be empty");
    }

    const auto _Key = MakeGeometryKey(strResourceID_, strPurpose_);
    auto _Iter = m_GeometryByKey.find(_Key);
    if (_Iter != m_GeometryByKey.end())
    {
        return _Iter->second;
    }

    const auto _GeometryID = static_cast<SceneGeometryID>(AllocateID());
    CSceneGeometryBinding _Binding;
    _Binding.nGeometryID = _GeometryID;
    _Binding.ResourceID = strResourceID_;
    _Binding.Purpose = strPurpose_;

    m_GeometryByKey.emplace(_Key, _GeometryID);
    m_Geometries.emplace(_GeometryID, std::move(_Binding));
    return _GeometryID;
}

std::optional<iCAX::Project::SceneGeometryID> iCAX::Project::CSceneObjectRegistry::FindGeometry(
    IN const std::string& strResourceID_,
    IN const std::string& strPurpose_) const
{
    const auto _Iter = m_GeometryByKey.find(MakeGeometryKey(strResourceID_, strPurpose_));
    if (_Iter == m_GeometryByKey.end())
    {
        return std::nullopt;
    }
    return _Iter->second;
}

const iCAX::Project::CSceneGeometryBinding* iCAX::Project::CSceneObjectRegistry::FindGeometry(
    IN SceneGeometryID nGeometryID_) const
{
    const auto _Iter = m_Geometries.find(nGeometryID_);
    return _Iter == m_Geometries.end() ? nullptr : &_Iter->second;
}

iCAX::Project::SceneColliderID iCAX::Project::CSceneObjectRegistry::GetOrCreateCollider(
    IN SceneObjectID nObjectID_,
    IN const std::string& strShapeResourceID_,
    IN const std::string& strPurpose_)
{
    if (!FindObject(nObjectID_))
    {
        throw std::out_of_range(std::format("Collider owner scene object does not exist: {}", nObjectID_));
    }
    if (strShapeResourceID_.empty())
    {
        throw std::invalid_argument("Collider shape resource ID cannot be empty");
    }
    if (strPurpose_.empty())
    {
        throw std::invalid_argument("Collider purpose cannot be empty");
    }

    const auto _Key = MakeColliderKey(nObjectID_, strShapeResourceID_, strPurpose_);
    auto _Iter = m_ColliderByKey.find(_Key);
    if (_Iter != m_ColliderByKey.end())
    {
        return _Iter->second;
    }

    const auto _ColliderID = static_cast<SceneColliderID>(AllocateID());
    CSceneColliderBinding _Binding;
    _Binding.nColliderID = _ColliderID;
    _Binding.nObjectID = nObjectID_;
    _Binding.ShapeResourceID = strShapeResourceID_;
    _Binding.Purpose = strPurpose_;

    m_ColliderByKey.emplace(_Key, _ColliderID);
    m_Colliders.emplace(_ColliderID, std::move(_Binding));
    return _ColliderID;
}

const iCAX::Project::CSceneColliderBinding* iCAX::Project::CSceneObjectRegistry::FindCollider(
    IN SceneColliderID nColliderID_) const
{
    const auto _Iter = m_Colliders.find(nColliderID_);
    return _Iter == m_Colliders.end() ? nullptr : &_Iter->second;
}

std::optional<iCAX::Project::SceneObjectID> iCAX::Project::CSceneObjectRegistry::FindObjectByCollider(
    IN SceneColliderID nColliderID_) const
{
    const auto _pCollider = FindCollider(nColliderID_);
    if (!_pCollider)
    {
        return std::nullopt;
    }
    return _pCollider->nObjectID;
}

bool iCAX::Project::CSceneObjectRegistry::RemoveObject(IN SceneObjectID nObjectID_)
{
    auto _Iter = m_Objects.find(nObjectID_);
    if (_Iter == m_Objects.end())
    {
        return false;
    }

    if (!_Iter->second.EntityID.is_nil())
    {
        m_ObjectByEntity.erase(_Iter->second.EntityID);
    }
    for (const auto& _Alias : _Iter->second.Aliases)
    {
        m_ObjectByAlias.erase(MakeAliasKey(_Alias.Namespace, _Alias.Key));
    }
    m_ObjectByTransform.erase(_Iter->second.nTransformID);
    m_Objects.erase(_Iter);

    std::vector<SceneColliderID> _CollidersToRemove;
    for (const auto& [_ColliderID, _Collider] : m_Colliders)
    {
        if (_Collider.nObjectID == nObjectID_)
        {
            _CollidersToRemove.push_back(_ColliderID);
        }
    }
    for (const auto _ColliderID : _CollidersToRemove)
    {
        (void)RemoveCollider(_ColliderID);
    }
    return true;
}

bool iCAX::Project::CSceneObjectRegistry::RemoveGeometry(IN SceneGeometryID nGeometryID_)
{
    auto _Iter = m_Geometries.find(nGeometryID_);
    if (_Iter == m_Geometries.end())
    {
        return false;
    }

    m_GeometryByKey.erase(MakeGeometryKey(_Iter->second.ResourceID, _Iter->second.Purpose));
    m_Geometries.erase(_Iter);
    return true;
}

bool iCAX::Project::CSceneObjectRegistry::RemoveCollider(IN SceneColliderID nColliderID_)
{
    auto _Iter = m_Colliders.find(nColliderID_);
    if (_Iter == m_Colliders.end())
    {
        return false;
    }

    m_ColliderByKey.erase(MakeColliderKey(_Iter->second.nObjectID, _Iter->second.ShapeResourceID, _Iter->second.Purpose));
    m_Colliders.erase(_Iter);
    return true;
}

void iCAX::Project::CSceneObjectRegistry::Clear()
{
    m_nNextID = 1;
    m_Objects.clear();
    m_ObjectByEntity.clear();
    m_ObjectByAlias.clear();
    m_ObjectByTransform.clear();
    m_Geometries.clear();
    m_GeometryByKey.clear();
    m_Colliders.clear();
    m_ColliderByKey.clear();
}

iCAX::Project::SceneRuntimeID iCAX::Project::CSceneObjectRegistry::AllocateID()
{
    const auto _ID = m_nNextID++;
    if (_ID == kInvalidSceneRuntimeID || m_nNextID == kInvalidSceneRuntimeID)
    {
        throw std::overflow_error("Scene runtime ID allocator overflowed");
    }
    return _ID;
}

std::string iCAX::Project::CSceneObjectRegistry::MakeAliasKey(
    IN const std::string& strAliasNamespace_,
    IN const std::string& strAliasKey_)
{
    return strAliasNamespace_ + "\n" + strAliasKey_;
}

std::string iCAX::Project::CSceneObjectRegistry::MakeGeometryKey(
    IN const std::string& strResourceID_,
    IN const std::string& strPurpose_)
{
    return strResourceID_ + "\n" + strPurpose_;
}

std::string iCAX::Project::CSceneObjectRegistry::MakeColliderKey(
    IN SceneObjectID nObjectID_,
    IN const std::string& strShapeResourceID_,
    IN const std::string& strPurpose_)
{
    return std::format("{}\n{}\n{}", nObjectID_, strShapeResourceID_, strPurpose_);
}
