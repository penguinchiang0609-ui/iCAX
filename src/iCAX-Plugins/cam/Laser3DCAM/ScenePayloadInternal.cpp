#include "pch.h"
#include "CommandInternal.h"
#include <algorithm>

namespace iCAX::CAM::Commands::Internal
{
namespace
{
    constexpr size_t kMaxInlineTopologyPickItems = 10000;
}

    bool _IsTopologyResourceEmpty(IN const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_) noexcept
    {
        return !pTopology_ || (pTopology_->Faces.empty() && pTopology_->Loops.empty() && pTopology_->Edges.empty());
    }

    ObjectMap _MakeRootPayload(IN const std::shared_ptr<iCAX::CAM::CRootComponent>& pRoot_)
    {
        ObjectMap _Root;
        _Root["activeWorkpieceId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveWorkpieceID()) : std::string();
        _Root["activeMachineId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveMachineID()) : std::string();
        _Root["activeToolAssemblyId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveToolAssemblyID()) : std::string();
        _Root["defaultCuttingLayerId"] = pRoot_ ? _UuidToString(pRoot_->GetDefaultCuttingLayerID()) : std::string();
        _Root["defaultVisibleLayerId"] = pRoot_ ? _UuidToString(pRoot_->GetDefaultVisibleLayerID()) : std::string();
        _Root["activeOrderPlanId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveOrderPlanID()) : std::string();
        _Root["latestSafetyCheckId"] = pRoot_ ? _UuidToString(pRoot_->GetLatestSafetyCheckID()) : std::string();
        _Root["activeSimulationId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveSimulationID()) : std::string();
        _Root["programRootBlockId"] = pRoot_ ? _UuidToString(pRoot_->GetProgramRootBlockID()) : std::string();
        return _Root;
    }

    ObjectMap _MakeMachinePayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineComponent>& pMachine_)
    {
        ObjectMap _Machine;
        _Machine["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Machine["id"] = _Machine["entityId"];
        _Machine["name"] = pMachine_ ? pMachine_->GetName() : std::string();
        _Machine["sourcePath"] = pMachine_ ? pMachine_->GetSourcePath() : std::string();
        _Machine["descriptionFormat"] = pMachine_ ? pMachine_->GetDescriptionFormat() : std::string();
        _Machine["sdfVersion"] = pMachine_ ? pMachine_->GetSDFVersion() : std::string();
        _Machine["modelName"] = pMachine_ ? pMachine_->GetModelName() : std::string();
        _Machine["workstationName"] = pMachine_ ? pMachine_->GetWorkstationName() : std::string("默认工位");
        _Machine["staticModel"] = pMachine_ ? pMachine_->GetStaticModel() : false;
        _Machine["status"] = pMachine_ ? pMachine_->GetStatus() : std::string("NotLoaded");
        _Machine["jointNames"] = pMachine_ ? pMachine_->GetJointNames() : VariantArray{};
        _Machine["jointPositions"] = pMachine_ ? pMachine_->GetJointPositions() : VariantArray{};
        _Machine["jointLowerLimits"] = pMachine_ ? pMachine_->GetJointLowerLimits() : VariantArray{};
        _Machine["jointUpperLimits"] = pMachine_ ? pMachine_->GetJointUpperLimits() : VariantArray{};
        _Machine["tcp"] = pMachine_ ? pMachine_->GetTCP() : VariantArray{};
        _Machine["beamDirection"] = pMachine_ ? pMachine_->GetBeamDirection() : VariantArray{};
        _Machine["maxVelocity"] = pMachine_ ? pMachine_->GetMaxVelocity() : 0.0;
        _Machine["maxAcceleration"] = pMachine_ ? pMachine_->GetMaxAcceleration() : 0.0;
        _Machine["links"] = pMachine_ ? pMachine_->GetLinks() : VariantArray{};
        _Machine["joints"] = pMachine_ ? pMachine_->GetJoints() : VariantArray{};
        _Machine["visuals"] = pMachine_ ? pMachine_->GetVisuals() : VariantArray{};
        _Machine["collisions"] = pMachine_ ? pMachine_->GetCollisions() : VariantArray{};
        _Machine["materials"] = pMachine_ ? pMachine_->GetMaterials() : VariantArray{};
        _Machine["includes"] = pMachine_ ? pMachine_->GetIncludes() : VariantArray{};
        _Machine["frames"] = pMachine_ ? pMachine_->GetFrames() : VariantArray{};
        _Machine["plugins"] = pMachine_ ? pMachine_->GetPlugins() : VariantArray{};
        _Machine["lastCheckResult"] = pMachine_ ? pMachine_->GetLastCheckResult() : std::string();
        _Machine["isLoaded"] = pMachine_ && !pMachine_->GetSourcePath().empty();
        return _Machine;
    }


    ObjectMap _MakeSelectionPayload(IN const std::shared_ptr<iCAX::CAM::CSelectionComponent>& pSelection_)
    {
        ObjectMap _Selection;
        _Selection["kind"] = pSelection_ ? pSelection_->GetSelectedKind() : std::string();
        _Selection["id"] = pSelection_ ? pSelection_->GetSelectedID() : 0ull;
        _Selection["label"] = pSelection_ ? pSelection_->GetSelectedLabel() : std::string();
        _Selection["hoverKind"] = pSelection_ ? pSelection_->GetHoverKind() : std::string();
        _Selection["hoverId"] = pSelection_ ? pSelection_->GetHoverID() : 0ull;
        return _Selection;
    }

    ObjectMap _MakeWorkpiecePayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CWorkpieceComponent>& pWorkpiece_)
    {
        ObjectMap _Workpiece;
        _Workpiece["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Workpiece["name"] = pWorkpiece_ ? pWorkpiece_->GetName() : std::string();
        _Workpiece["sourcePath"] = pWorkpiece_ ? pWorkpiece_->GetSourcePath() : std::string();
        _Workpiece["modelResourceId"] = pWorkpiece_ ? pWorkpiece_->GetModelResourceID() : std::string();
        _Workpiece["brepResourceId"] = pWorkpiece_ ? pWorkpiece_->GetBRepResourceID() : std::string();
        _Workpiece["topologyResourceId"] = pWorkpiece_ ? pWorkpiece_->GetTopologyResourceID() : std::string();
        _Workpiece["topologyVersion"] = pWorkpiece_ ? pWorkpiece_->GetTopologyVersion() : 0ull;
        _Workpiece["isLoaded"] = pWorkpiece_ && !pWorkpiece_->GetSourcePath().empty();
        return _Workpiece;
    }

    VariantArray _MakeWorkpieceArray(IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Workpieces;
        for (const auto& [_pEntity, _pWorkpiece] : _CollectEntitiesWithComponent<iCAX::CAM::CWorkpieceComponent>(Repository_))
        {
            _Workpieces.emplace_back(_MakeWorkpiecePayload(_pEntity, _pWorkpiece));
        }
        return _Workpieces;
    }

    VariantArray _MakeMachineArray(IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Machines;
        for (const auto& [_pEntity, _pMachine] : _CollectEntitiesWithComponent<iCAX::CAM::CMachineComponent>(Repository_))
        {
            _Machines.emplace_back(_MakeMachinePayload(_pEntity, _pMachine));
        }
        return _Machines;
    }

    ObjectMap _MakeTopologyStatusPayload(IN const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_)
    {
        ObjectMap _Status;
        _Status["hasTopology"] = !_IsTopologyResourceEmpty(pTopology_);
        _Status["version"] = pTopology_ ? pTopology_->nVersion : 0ull;
        _Status["faceCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Faces.size()) : 0ull;
        _Status["loopCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Loops.size()) : 0ull;
        _Status["edgeCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Edges.size()) : 0ull;
        _Status["inlinePickMapLimit"] = static_cast<unsigned long long>(kMaxInlineTopologyPickItems);
        _Status["inlinePickMapTruncated"] = pTopology_
            && (pTopology_->Faces.size() > kMaxInlineTopologyPickItems
                || pTopology_->Loops.size() > kMaxInlineTopologyPickItems
                || pTopology_->Edges.size() > kMaxInlineTopologyPickItems);
        _Status["metadata"] = pTopology_ ? pTopology_->Metadata : ObjectMap{};

        if (pTopology_)
        {
            auto _ImportMode = pTopology_->Metadata.find("importMode");
            if (_ImportMode != pTopology_->Metadata.end())
            {
                _Status["importMode"] = _ImportMode->second;
            }

            auto _Diagnostic = pTopology_->Metadata.find("diagnostic");
            if (_Diagnostic != pTopology_->Metadata.end())
            {
                _Status["diagnostic"] = _Diagnostic->second;
            }
        }
        return _Status;
    }

    ObjectMap _MakeTopologyPickItemPayload(IN const Variant& Item_)
    {
        if (!Item_.Is<ObjectMap>())
        {
            return ObjectMap{};
        }

        const auto _Source = Item_.To<ObjectMap>();
        ObjectMap _Item;
        _Item["id"] = _GetObjectUInt64(_Source, "id");
        _Item["kind"] = _GetObjectString(_Source, "kind");
        _Item["label"] = _GetObjectString(_Source, "label");
        _Item["role"] = _GetObjectString(_Source, "role");
        _Item["triangleStart"] = _GetObjectUInt64(_Source, "triangleStart");
        _Item["triangleCount"] = _GetObjectUInt64(_Source, "triangleCount");
        return _Item;
    }

    VariantArray _MakeTopologyPickArray(IN const VariantArray& Items_)
    {
        VariantArray _Result;
        if (Items_.size() > kMaxInlineTopologyPickItems)
        {
            return _Result;
        }

        _Result.reserve(Items_.size());
        for (const auto& _Item : Items_)
        {
            _Result.emplace_back(_MakeTopologyPickItemPayload(_Item));
        }
        return _Result;
    }

    Variant _BuildScenePayload(IN iCAX::Project::ISceneContext& Scene_)
    {
        auto& _Repository = Scene_.Database();
        auto _pRoot = _GetComponent<iCAX::CAM::CRootComponent>(_Repository);
        auto [_pMachineEntity, _pMachine] = _GetActiveMachine(_Repository);
        auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
        auto _pSelection = _GetComponent<iCAX::CAM::CSelectionComponent>(_Repository);
        auto _pTopology = _GetTopologyResource(Scene_, _pWorkpiece);
        auto _Workpiece = _MakeWorkpiecePayload(_pWorkpieceEntity, _pWorkpiece);
        auto _Machine = _MakeMachinePayload(_pMachineEntity, _pMachine);

        ObjectMap _Scene;
        _Scene["root"] = _MakeRootPayload(_pRoot);
        _Scene["machine"] = _Machine;
        _Scene["machines"] = _MakeMachineArray(_Repository);
        _Scene["workpiece"] = _Workpiece;
        _Scene["workpieces"] = _MakeWorkpieceArray(_Repository);
        _Scene["model"] = _Workpiece;
        _Scene["topology"] = _MakeTopologyStatusPayload(_pTopology);
        _Scene["faces"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Faces) : VariantArray{};
        _Scene["loops"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Loops) : VariantArray{};
        _Scene["edges"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Edges) : VariantArray{};
        _Scene["selection"] = _MakeSelectionPayload(_pSelection);
        _Scene["cuttingLayers"] = _MakeCuttingLayerArray(_Repository);
        _Scene["visibleLayers"] = _MakeVisibleLayerArray(_Repository);
        _Scene["program"] = _MakeProgramPayload(_Repository);
        _Scene["toolpaths"] = _MakePathArray(_Repository);
        return Variant(_Scene);
    }

}
