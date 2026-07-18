#include "pch.h"
#include "IntentToolpathRelations.h"
#include "ComponentValueHelpers.h"
#include "IntentToolpathComponents.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include <algorithm>
#include <unordered_set>

namespace
{
using namespace iCAX::CAM::Intent;
template<class T> std::shared_ptr<T> Component(const std::shared_ptr<iCAX::Database::IEntity>& Entity_)
{ return Entity_ ? std::dynamic_pointer_cast<T>(Entity_->GetComponent(T::S_ClassName)) : nullptr; }
std::vector<iCAX::Data::uuid> ReadIDs(const iCAX::Data::VariantArray& Values_)
{
    std::vector<iCAX::Data::uuid> _Result;
    for (const auto& _Value : Values_) { const auto _ID = FromUuidVariant(_Value); if (!_ID.is_nil() && std::find(_Result.begin(), _Result.end(), _ID) == _Result.end()) _Result.push_back(_ID); }
    return _Result;
}
iCAX::Data::VariantArray WriteIDs(const std::vector<iCAX::Data::uuid>& IDs_)
{ iCAX::Data::VariantArray _Result; for (const auto& _ID : IDs_) if (!_ID.is_nil()) _Result.emplace_back(_ID); return _Result; }
bool WriteHierarchy(const std::shared_ptr<CIntentHierarchyComponent>& Hierarchy_, const iCAX::Data::uuid& Parent_, const std::vector<iCAX::Data::uuid>& Children_, std::string& Error_)
{
    if (!Hierarchy_->SetProperty(CIntentHierarchyComponent::PropertyName_ParentEntityID, iCAX::Data::PropertyValue(Parent_), Error_)) return false;
    if (!Hierarchy_->SetProperty(CIntentHierarchyComponent::PropertyName_ChildEntityIDs, iCAX::Data::PropertyValue(WriteIDs(Children_)), Error_)) return false;
    return Hierarchy_->SetProperty(CIntentHierarchyComponent::PropertyName_Revision, iCAX::Data::PropertyValue(Hierarchy_->GetRevision() + 1), Error_);
}
bool WouldCycle(const iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& Child_, iCAX::Data::uuid Cursor_)
{
    while (!Cursor_.is_nil()) { if (Cursor_ == Child_) return true; const auto _H = Component<CIntentHierarchyComponent>(Repository_.GetEntity(Cursor_)); if (!_H) return false; Cursor_ = _H->GetParentEntityID(); }
    return false;
}
void Collect(const iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& ID_, std::unordered_set<iCAX::Data::uuid>& Visiting_, std::vector<iCAX::Data::uuid>& Result_)
{
    if (ID_.is_nil() || Visiting_.contains(ID_) || iCAX::CAM::Intent::IsIntentSuperseded(Repository_, ID_)) return;
    const auto _Intent = Component<CIntentToolpathComponent>(Repository_.GetEntity(ID_));
    if (!_Intent || !_Intent->GetEnabled() || _Intent->GetSourceState() != "Current") return;
    Visiting_.insert(ID_);
    const auto _Children = iCAX::CAM::Intent::GetIntentChildEntityIDs(Repository_, ID_);
    if (_Children.empty()) Result_.push_back(ID_); else for (const auto& _Child : _Children) Collect(Repository_, _Child, Visiting_, Result_);
    Visiting_.erase(ID_);
}
}

bool iCAX::CAM::Intent::SetIntentParent(iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& ChildID_, const iCAX::Data::uuid& ParentID_, std::string& Error_)
{
    Error_.clear();
    if (ChildID_.is_nil() || ChildID_ == ParentID_) { Error_ = "Intent hierarchy requires a distinct child"; return false; }
    const auto _Child = Component<CIntentHierarchyComponent>(Repository_.GetEntity(ChildID_));
    const auto _Parent = ParentID_.is_nil() ? nullptr : Component<CIntentHierarchyComponent>(Repository_.GetEntity(ParentID_));
    if (!_Child || (!ParentID_.is_nil() && !_Parent)) { Error_ = "Intent hierarchy component is missing"; return false; }
    if (!ParentID_.is_nil() && WouldCycle(Repository_, ChildID_, ParentID_)) { Error_ = "Intent hierarchy cycle is not allowed"; return false; }
    const auto _OldID = _Child->GetParentEntityID();
    if (_OldID == ParentID_) return true;
    if (!_OldID.is_nil()) if (const auto _Old = Component<CIntentHierarchyComponent>(Repository_.GetEntity(_OldID))) { auto _IDs = ReadIDs(_Old->GetChildEntityIDs()); std::erase(_IDs, ChildID_); if (!WriteHierarchy(_Old, _Old->GetParentEntityID(), _IDs, Error_)) return false; }
    if (_Parent) { auto _IDs = ReadIDs(_Parent->GetChildEntityIDs()); if (std::find(_IDs.begin(), _IDs.end(), ChildID_) == _IDs.end()) _IDs.push_back(ChildID_); if (!WriteHierarchy(_Parent, _Parent->GetParentEntityID(), _IDs, Error_)) return false; }
    return WriteHierarchy(_Child, ParentID_, ReadIDs(_Child->GetChildEntityIDs()), Error_);
}

std::vector<iCAX::Data::uuid> iCAX::CAM::Intent::GetIntentChildEntityIDs(const iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& ParentID_)
{ const auto _H = Component<CIntentHierarchyComponent>(Repository_.GetEntity(ParentID_)); return _H ? ReadIDs(_H->GetChildEntityIDs()) : std::vector<iCAX::Data::uuid>(); }

std::vector<iCAX::Data::uuid> iCAX::CAM::Intent::GetActiveSupersedingDerivationIDs(const iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& IntentID_)
{
    std::vector<iCAX::Data::uuid> _Result;
    for (const auto& _Entity : Repository_.FilterEntities([](const auto& E_) { return Component<CIntentDerivationComponent>(E_) != nullptr; }))
    { const auto _D = Component<CIntentDerivationComponent>(_Entity); const auto _Inputs = ReadIDs(_D->GetInputIntentEntityIDs()); if (_D->GetActive() && std::find(_Inputs.begin(), _Inputs.end(), IntentID_) != _Inputs.end()) _Result.push_back(_Entity->GetID()); }
    return _Result;
}
bool iCAX::CAM::Intent::IsIntentSuperseded(const iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& IntentID_)
{ return !GetActiveSupersedingDerivationIDs(Repository_, IntentID_).empty(); }
std::vector<iCAX::Data::uuid> iCAX::CAM::Intent::CollectEffectiveLeafIntentIDs(const iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& RootID_)
{ std::vector<iCAX::Data::uuid> _Result; std::unordered_set<iCAX::Data::uuid> _Visiting; Collect(Repository_, RootID_, _Visiting, _Result); return _Result; }
