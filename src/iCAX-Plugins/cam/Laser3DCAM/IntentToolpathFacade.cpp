#include "pch.h"
#include "FacadeSupport.h"
#include "SelectionComponents.h"
#include "ToolpathFacadeImplement.h"
#include "WorkpieceComponents.h"
#include "IntentToolpath.h"
#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"

namespace iCAX::CAM::Facades
{
using namespace Internal;
using namespace iCAX::CAM::Intent;

namespace
{
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<CIntentDocumentComponent>> EnsureDocument(
    iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& WorkpieceID_, const std::string& Name_)
{
    for (const auto& [Entity_, Document_] : _CollectEntitiesWithComponent<CIntentDocumentComponent>(Repository_))
        if (Document_ && Document_->GetWorkpieceEntityID() == WorkpieceID_) return { Entity_, Document_ };
    auto [Entity_, Document_] = _CreateEntityWithComponent<CIntentDocumentComponent>(Repository_);
    _SetUuidProperty(Document_, CIntentDocumentComponent::PropertyName_WorkpieceEntityID, WorkpieceID_);
    _SetStringProperty(Document_, CIntentDocumentComponent::PropertyName_Name, Name_ + " 加工意图");
    (void)_GetOrAddEntityComponent<CIntentHierarchyComponent>(Entity_);
    return { Entity_, Document_ };
}

ObjectMap MakeIntentNode(iCAX::Database::IRepository& Repository_, const iCAX::Data::uuid& ID_)
{
    ObjectMap _Node;
    const auto _Entity = Repository_.GetEntity(ID_);
    const auto _Intent = _GetComponent<CIntentToolpathComponent>(_Entity);
    if (!_Entity || !_Intent) return _Node;
    _Node["entityId"] = iCAX::Data::to_string(ID_);
    _Node["name"] = _Intent->GetName();
    _Node["enabled"] = _Intent->GetEnabled();
    _Node["sourceState"] = _Intent->GetSourceState();
    _Node["superseded"] = IsIntentSuperseded(Repository_, ID_);
    _Node["kind"] = std::string(_GetComponent<CMicroJointIntentComponent>(_Entity) ? "MicroJoint"
        : _GetComponent<CBridgeIntentComponent>(_Entity) ? "Bridge"
        : _GetComponent<CLeadIntentComponent>(_Entity) ? "Lead" : "Cut");
    VariantArray _Children;
    for (const auto& _ChildID : GetIntentChildEntityIDs(Repository_, ID_))
    {
        auto _Child = MakeIntentNode(Repository_, _ChildID);
        if (!_Child.empty()) _Children.emplace_back(_Child);
    }
    _Node["children"] = _Children;
    return _Node;
}

Interaction::CInvocationResult MakeListResponse(Project::ISceneContext& Scene_)
{
    auto& _Repository = Scene_.Database();
    auto [WorkpieceEntity_, Workpiece_] = _GetActiveWorkpiece(_Repository);
    ObjectMap _Result;
    VariantArray _Roots;
    if (WorkpieceEntity_ && Workpiece_)
    {
        std::shared_ptr<iCAX::Database::IEntity> _DocumentEntity;
        for (const auto& [_Entity, _Document] : _CollectEntitiesWithComponent<CIntentDocumentComponent>(_Repository))
            if (_Document && _Document->GetWorkpieceEntityID() == WorkpieceEntity_->GetID()) { _DocumentEntity = _Entity; break; }
        if (_DocumentEntity)
        {
            _Result["intentDocumentId"] = iCAX::Data::to_string(_DocumentEntity->GetID());
            for (const auto& _ID : GetIntentChildEntityIDs(_Repository, _DocumentEntity->GetID()))
            {
                auto _Node = MakeIntentNode(_Repository, _ID);
                if (!_Node.empty()) _Roots.emplace_back(_Node);
            }
        }
    }
    _Result["intentToolpaths"] = _Roots;
    return _MakeResponse(Variant(_Result));
}
}

Interaction::CInvocationResult HandleListIntentToolpaths(const Interaction::CInvocation&, const Application::IApplicationContext&, Product::IProductContext*, Project::IProjectContext*, Project::ISceneContext* Scene_)
{
    return MakeListResponse(_RequireSceneContext(Scene_));
}

Interaction::CInvocationResult HandleCreateIntentFromSelection(const Interaction::CInvocation&, const Application::IApplicationContext&, Product::IProductContext*, Project::IProjectContext*, Project::ISceneContext* Scene_)
{
    auto& _Scene = _RequireSceneContext(Scene_);
    auto& _Repository = _Scene.Database();
    auto [WorkpieceEntity_, Workpiece_] = _GetActiveWorkpiece(_Repository);
    if (!WorkpieceEntity_ || !Workpiece_) throw std::invalid_argument("IntentToolpath requires an active workpiece");
    auto _Selection = _GetOrCreateComponent<CSelectionComponent>(_Repository);
    if (_Selection->GetSelectedKind().empty() || _Selection->GetSelectedID() == 0) throw std::invalid_argument("IntentToolpath requires a topology selection");
    auto [DocumentEntity_, Document_] = EnsureDocument(_Repository, WorkpieceEntity_->GetID(), Workpiece_->GetName());
    auto _Undo = _Repository.BeginUndoCommand("Create intent toolpath from CAD selection");
    auto [IntentEntity_, Intent_] = _CreateEntityWithComponent<CIntentToolpathComponent>(_Repository);
    (void)_GetOrAddEntityComponent<CIntentHierarchyComponent>(IntentEntity_);
    auto _CAD = _GetOrAddEntityComponent<CCADIntentSourceComponent>(IntentEntity_);
    (void)_GetOrAddEntityComponent<CCutIntentComponent>(IntentEntity_);
    _SetStringProperty(Intent_, CIntentToolpathComponent::PropertyName_Name, _Selection->GetSelectedLabel().empty() ? "CAD切割意图" : _Selection->GetSelectedLabel());
    _SetUuidProperty(_CAD, CCADIntentSourceComponent::PropertyName_WorkpieceEntityID, WorkpieceEntity_->GetID());
    _SetStringProperty(_CAD, CCADIntentSourceComponent::PropertyName_FeatureCode, _Selection->GetSelectedKind() + ":" + std::to_string(_Selection->GetSelectedID()));
    _SetStringProperty(_CAD, CCADIntentSourceComponent::PropertyName_TopologyKind, _Selection->GetSelectedKind());
    _SetUInt64Property(_CAD, CCADIntentSourceComponent::PropertyName_TopologyID, _Selection->GetSelectedID());
    std::string _Error;
    if (!SetIntentParent(_Repository, IntentEntity_->GetID(), DocumentEntity_->GetID(), _Error)) throw std::runtime_error(_Error);
    _Undo->End();
    return MakeListResponse(_Scene);
}
}

namespace
{
class CIntentToolpathFacade final : public iCAX::Interaction::CFacade
{
public:
    CIntentToolpathFacade() : CFacade("IntentToolpath")
    {
        ExposeMethod("List", &iCAX::CAM::Facades::HandleListIntentToolpaths);
        ExposeMethod("CreateFromSelection", &iCAX::CAM::Facades::HandleCreateIntentFromSelection);
    }
};
static_assert(iCAX::Interaction::IsStatelessFacadeType<CIntentToolpathFacade>);
}
ICAX_REGISTER_FACADE(CIntentToolpathFacade)
