#include "pch.h"
#include "Transform.h"

#include "Database/IEntity.h"
#include "Database/IRepository.h"

#include <algorithm>

uint32_t iCAX::Transform::GetTransformContractVersion() noexcept
{
    return 1;
}

namespace
{
    std::shared_ptr<iCAX::Transform::CTransformComponent> _GetTransform(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        if (!pEntity_)
        {
            return nullptr;
        }
        return std::dynamic_pointer_cast<iCAX::Transform::CTransformComponent>(
            pEntity_->GetComponent(iCAX::Transform::CTransformComponent::S_ClassName));
    }

    std::vector<iCAX::Data::uuid> _ReadChildIDs(
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_)
    {
        std::vector<iCAX::Data::uuid> _Result;
        if (!pTransform_)
        {
            return _Result;
        }

        for (const auto& _Value : pTransform_->GetChildEntityIDs())
        {
            auto _ID = iCAX::Transform::FromUuidVariant(_Value);
            if (!_ID.is_nil()
                && std::find(_Result.begin(), _Result.end(), _ID) == _Result.end())
            {
                _Result.push_back(_ID);
            }
        }
        return _Result;
    }

    iCAX::Data::VariantArray _MakeChildArray(IN const std::vector<iCAX::Data::uuid>& IDs_)
    {
        iCAX::Data::VariantArray _Result;
        _Result.reserve(IDs_.size());
        for (const auto& _ID : IDs_)
        {
            if (!_ID.is_nil())
            {
                _Result.emplace_back(_ID);
            }
        }
        return _Result;
    }

    bool _SetParentID(
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_,
        IN const iCAX::Data::uuid& ParentEntityID_,
        OUT std::string& strError_)
    {
        return pTransform_
            && pTransform_->SetProperty(
                iCAX::Transform::CTransformComponent::PropertyName_ParentEntityID,
                iCAX::Data::PropertyValue(ParentEntityID_),
                strError_);
    }

    bool _SetChildIDs(
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_,
        IN const std::vector<iCAX::Data::uuid>& IDs_,
        OUT std::string& strError_)
    {
        return pTransform_
            && pTransform_->SetProperty(
                iCAX::Transform::CTransformComponent::PropertyName_ChildEntityIDs,
                iCAX::Data::PropertyValue(_MakeChildArray(IDs_)),
                strError_);
    }

    bool _WouldCreateCycle(
        IN const iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& ChildEntityID_,
        IN const iCAX::Data::uuid& ParentEntityID_)
    {
        auto _Cursor = ParentEntityID_;
        while (!_Cursor.is_nil())
        {
            if (_Cursor == ChildEntityID_)
            {
                return true;
            }

            const auto _pEntity = Repository_.GetEntity(_Cursor);
            const auto _pTransform = _GetTransform(_pEntity);
            if (!_pTransform)
            {
                return false;
            }
            _Cursor = _pTransform->GetParentEntityID();
        }
        return false;
    }
}

bool iCAX::Transform::SetTransformParent(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& ChildEntityID_,
    IN const iCAX::Data::uuid& ParentEntityID_,
    OUT std::string& strError_)
{
    strError_.clear();
    if (ChildEntityID_.is_nil())
    {
        strError_ = "SetTransformParent requires child entity id";
        return false;
    }
    if (ChildEntityID_ == ParentEntityID_)
    {
        strError_ = "Transform cannot be parent of itself";
        return false;
    }

    const auto _pChildEntity = Repository_.GetEntity(ChildEntityID_);
    const auto _pChildTransform = _GetTransform(_pChildEntity);
    if (!_pChildEntity || !_pChildTransform)
    {
        strError_ = "Child entity has no Transform component";
        return false;
    }

    std::shared_ptr<CTransformComponent> _pNewParentTransform;
    if (!ParentEntityID_.is_nil())
    {
        const auto _pNewParentEntity = Repository_.GetEntity(ParentEntityID_);
        _pNewParentTransform = _GetTransform(_pNewParentEntity);
        if (!_pNewParentEntity || !_pNewParentTransform)
        {
            strError_ = "Parent entity has no Transform component";
            return false;
        }
        if (_WouldCreateCycle(Repository_, ChildEntityID_, ParentEntityID_))
        {
            strError_ = "Transform parent would create hierarchy cycle";
            return false;
        }
    }

    const auto _OldParentID = _pChildTransform->GetParentEntityID();
    if (_OldParentID == ParentEntityID_)
    {
        if (_pNewParentTransform)
        {
            auto _Children = _ReadChildIDs(_pNewParentTransform);
            if (std::find(_Children.begin(), _Children.end(), ChildEntityID_) == _Children.end())
            {
                _Children.push_back(ChildEntityID_);
                return _SetChildIDs(_pNewParentTransform, _Children, strError_);
            }
        }
        return true;
    }

    std::shared_ptr<CTransformComponent> _pOldParentTransform;
    if (!_OldParentID.is_nil())
    {
        _pOldParentTransform = _GetTransform(Repository_.GetEntity(_OldParentID));
    }

    if (_pOldParentTransform)
    {
        auto _OldChildren = _ReadChildIDs(_pOldParentTransform);
        std::erase(_OldChildren, ChildEntityID_);
        if (!_SetChildIDs(_pOldParentTransform, _OldChildren, strError_))
        {
            return false;
        }
    }

    if (_pNewParentTransform)
    {
        auto _NewChildren = _ReadChildIDs(_pNewParentTransform);
        if (std::find(_NewChildren.begin(), _NewChildren.end(), ChildEntityID_) == _NewChildren.end())
        {
            _NewChildren.push_back(ChildEntityID_);
            if (!_SetChildIDs(_pNewParentTransform, _NewChildren, strError_))
            {
                return false;
            }
        }
    }

    return _SetParentID(_pChildTransform, ParentEntityID_, strError_);
}

bool iCAX::Transform::SetTransformParent(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& ChildEntityID_,
    IN const iCAX::Data::uuid& ParentEntityID_)
{
    std::string _strError;
    return SetTransformParent(Repository_, ChildEntityID_, ParentEntityID_, _strError);
}

std::vector<iCAX::Data::uuid> iCAX::Transform::GetTransformChildEntityIDs(
    IN const iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& ParentEntityID_)
{
    if (ParentEntityID_.is_nil())
    {
        return {};
    }
    return _ReadChildIDs(_GetTransform(Repository_.GetEntity(ParentEntityID_)));
}

std::vector<std::shared_ptr<iCAX::Database::IEntity>> iCAX::Transform::GetTransformChildren(
    IN const iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& ParentEntityID_)
{
    std::vector<std::shared_ptr<iCAX::Database::IEntity>> _Result;
    for (const auto& _ChildID : GetTransformChildEntityIDs(Repository_, ParentEntityID_))
    {
        auto _pChild = Repository_.GetEntity(_ChildID);
        if (_pChild)
        {
            _Result.push_back(_pChild);
        }
    }
    return _Result;
}
