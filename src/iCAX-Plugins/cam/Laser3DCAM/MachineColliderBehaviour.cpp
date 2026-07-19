#include "pch.h"

#include "MachineInstanceComponents.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "ColliderData/ColliderData.h"
#include "Facades/FacadeMethod.h"
#include "Data/Variant.h"
#include "Database/IEntity.h"
#include "Facades/FacadePayload.h"
#include "PDO/IPDOHub.h"
#include "PDO/PDOLease.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "Transform/Transform.h"


namespace
{
    using ObjectMap = iCAX::Data::ObjectMap;
    using Variant = iCAX::Data::Variant;
    using VariantArray = iCAX::Data::VariantArray;

    constexpr double kMachineDescriptionLengthToWorld = 1000.0;

    inline constexpr uint64_t kPDOColliderSlotAllocatedEvent =
        iCAX::Interaction::MakeFacadeMethodCode("PDOCollider", "SlotAllocated");
    inline constexpr uint64_t kPDOColliderSlotFreedEvent =
        iCAX::Interaction::MakeFacadeMethodCode("PDOCollider", "SlotFreed");
    inline constexpr uint64_t kPDOColliderSlotMovedEvent =
        iCAX::Interaction::MakeFacadeMethodCode("PDOCollider", "SlotMoved");
    inline constexpr uint64_t kPDOColliderDefragBeginEvent =
        iCAX::Interaction::MakeFacadeMethodCode("PDOCollider", "DefragBegin");
    inline constexpr uint64_t kPDOColliderDefragEndEvent =
        iCAX::Interaction::MakeFacadeMethodCode("PDOCollider", "DefragEnd");

    struct SSlotAssignment final
    {
        iCAX::PDO::PDOID nPDOID = 0;
        uint32_t nSlotVersion = 0;
        uint64_t nPayloadCapacity = 0;
        bool bNeedPublishAllocatedEvent = false;
    };

    std::unordered_map<std::string, SSlotAssignment> g_ColliderSlots;

    uint64_t NextColliderVersion() noexcept
    {
        static std::atomic_uint64_t _Version{ 1 };
        return _Version.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t MakeNonZeroVersion(IN uint64_t nVersion_) noexcept
    {
        return nVersion_ == 0 ? NextColliderVersion() : nVersion_;
    }

    std::optional<ObjectMap> TryObjectMap(IN const Variant& Value_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return std::nullopt;
        }
        return Value_.To<ObjectMap>();
    }

    std::optional<ObjectMap> TryGetObjectMapField(IN const ObjectMap& Object_, IN const std::string& strName_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<ObjectMap>())
        {
            return std::nullopt;
        }
        return _Iter->second.To<ObjectMap>();
    }

    std::string GetObjectString(IN const ObjectMap& Object_, IN const std::string& strName_, IN const std::string& strDefault_ = {})
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<std::string>())
        {
            return strDefault_;
        }
        return _Iter->second.To<std::string>();
    }

    double ToDouble(IN const Variant& Value_, IN const std::string& strFieldName_)
    {
        if (Value_.Is<double>())
        {
            return Value_.To<double>();
        }
        if (Value_.Is<float>())
        {
            return static_cast<double>(Value_.To<float>());
        }
        if (Value_.Is<int>())
        {
            return static_cast<double>(Value_.To<int>());
        }
        if (Value_.Is<unsigned int>())
        {
            return static_cast<double>(Value_.To<unsigned int>());
        }
        if (Value_.Is<long long>())
        {
            return static_cast<double>(Value_.To<long long>());
        }
        if (Value_.Is<unsigned long long>())
        {
            return static_cast<double>(Value_.To<unsigned long long>());
        }
        throw std::invalid_argument("Machine collider field is not numeric: " + strFieldName_);
    }

    double GetObjectDoubleOrDefault(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN double nDefault_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return nDefault_;
        }
        try
        {
            return ToDouble(_Iter->second, strName_);
        }
        catch (const std::exception&)
        {
            return nDefault_;
        }
    }

    std::vector<double> ReadObjectDoubleArray(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN size_t nExpectedSize_,
        IN const std::vector<double>& Default_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<VariantArray>())
        {
            return Default_;
        }

        const auto _Array = _Iter->second.To<VariantArray>();
        if (_Array.size() != nExpectedSize_)
        {
            return Default_;
        }

        std::vector<double> _Values;
        _Values.reserve(_Array.size());
        for (const auto& _Value : _Array)
        {
            try
            {
                _Values.push_back(ToDouble(_Value, strName_));
            }
            catch (const std::exception&)
            {
                return Default_;
            }
        }
        return _Values;
    }

    std::array<double, 6> ReadPose(IN const ObjectMap& Attachment_)
    {
        const auto _Pose = ReadObjectDoubleArray(Attachment_, "pose", 6, { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 });
        return { _Pose[0], _Pose[1], _Pose[2], _Pose[3], _Pose[4], _Pose[5] };
    }

    iCAX::Collider::SColliderPDOID ToColliderPDOID(IN const iCAX::Data::uuid& ID_)
    {
        iCAX::Collider::SColliderPDOID _ID;
        const auto _Bytes = ID_.as_bytes();
        for (size_t _Index = 0; _Index < _ID.Bytes.size(); ++_Index)
        {
            _ID.Bytes[_Index] = std::to_integer<uint8_t>(_Bytes[_Index]);
        }
        return _ID;
    }

    iCAX::Collider::SColliderPDOMatrix4 ToColliderMatrix(IN const iCAX::Data::Double4x4& Matrix_) noexcept
    {
        auto _Matrix = iCAX::Collider::SColliderPDOMatrix4::Identity();
        for (int _Row = 0; _Row < 4; ++_Row)
        {
            for (int _Column = 0; _Column < 4; ++_Column)
            {
                _Matrix.Values[static_cast<size_t>(_Column) * 4 + static_cast<size_t>(_Row)] =
                    static_cast<float>(Matrix_(_Row, _Column));
            }
        }
        return _Matrix;
    }

    iCAX::Data::Double4x4 MakeAttachmentLocalMatrix(IN const ObjectMap& Attachment_)
    {
        const auto _Pose = ReadPose(Attachment_);
        return iCAX::Transform::MakeLocalMatrix(
            _Pose[0] * kMachineDescriptionLengthToWorld,
            _Pose[1] * kMachineDescriptionLengthToWorld,
            _Pose[2] * kMachineDescriptionLengthToWorld,
            _Pose[5],
            _Pose[4],
            _Pose[3],
            1.0,
            1.0,
            1.0);
    }

    bool TryMakeColliderShape(
        IN const ObjectMap& Attachment_,
        OUT iCAX::Collider::SColliderShapePDOData& Shape_)
    {
        const auto _Geometry = TryGetObjectMapField(Attachment_, "geometry");
        if (!_Geometry)
        {
            return false;
        }

        const auto _Type = GetObjectString(*_Geometry, "type");
        Shape_ = {};
        Shape_.nFlags = iCAX::Collider::kColliderFlagVisible | iCAX::Collider::kColliderFlagEnabled | iCAX::Collider::kColliderFlagSelectable;
        Shape_.nLayer = iCAX::Collider::kDefaultCollisionLayer;
        Shape_.nMask = iCAX::Collider::kCollisionMaskAll;
        Shape_.LocalMatrix = ToColliderMatrix(MakeAttachmentLocalMatrix(Attachment_));

        if (_Type == "box")
        {
            const auto _Size = ReadObjectDoubleArray(*_Geometry, "size", 3, { 1.0, 1.0, 1.0 });
            Shape_.nShapeKind = static_cast<uint32_t>(iCAX::Collider::EColliderShapeKind::Box);
            Shape_.Parameters[0] = static_cast<float>(_Size[0] * kMachineDescriptionLengthToWorld * 0.5);
            Shape_.Parameters[1] = static_cast<float>(_Size[1] * kMachineDescriptionLengthToWorld * 0.5);
            Shape_.Parameters[2] = static_cast<float>(_Size[2] * kMachineDescriptionLengthToWorld * 0.5);
            return true;
        }

        if (_Type == "sphere")
        {
            Shape_.nShapeKind = static_cast<uint32_t>(iCAX::Collider::EColliderShapeKind::Sphere);
            Shape_.Parameters[0] = static_cast<float>(GetObjectDoubleOrDefault(*_Geometry, "radius", 0.5) * kMachineDescriptionLengthToWorld);
            return true;
        }

        if (_Type == "cylinder")
        {
            Shape_.nShapeKind = static_cast<uint32_t>(iCAX::Collider::EColliderShapeKind::Cylinder);
            Shape_.Parameters[0] = static_cast<float>(GetObjectDoubleOrDefault(*_Geometry, "radius", 0.5) * kMachineDescriptionLengthToWorld);
            Shape_.Parameters[1] = static_cast<float>(GetObjectDoubleOrDefault(*_Geometry, "length", 1.0) * kMachineDescriptionLengthToWorld);
            return true;
        }

        // Machine collider visualization deliberately ignores triangle mesh collision
        // here. Product-grade machine definitions should use primitive envelopes for
        // collider display and future physical collision checks.
        return false;
    }

    uint64_t CalculatePayloadCapacity(IN size_t nShapeCount_)
    {
        if (nShapeCount_ > ((std::numeric_limits<uint64_t>::max)() - sizeof(iCAX::Collider::SObjectColliderPDOHeader))
            / sizeof(iCAX::Collider::SColliderShapePDOData))
        {
            throw std::overflow_error("Collider PDO payload size overflows");
        }
        return sizeof(iCAX::Collider::SObjectColliderPDOHeader)
            + static_cast<uint64_t>(nShapeCount_ * sizeof(iCAX::Collider::SColliderShapePDOData));
    }

    std::vector<std::byte> MakeColliderPayload(
        IN const iCAX::Data::uuid& EntityID_,
        IN uint64_t nDataVersion_,
        IN uint32_t nFlags_,
        IN const std::vector<iCAX::Collider::SColliderShapePDOData>& Shapes_)
    {
        std::vector<std::byte> _Payload(static_cast<size_t>(CalculatePayloadCapacity(Shapes_.size())));

        iCAX::Collider::SObjectColliderPDOHeader _Header;
        _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::Collider::EColliderPDOPayloadKind::Object);
        _Header.Header.nHeaderSize = sizeof(iCAX::Collider::SObjectColliderPDOHeader);
        _Header.Header.nPayloadSize = static_cast<uint64_t>(_Payload.size());
        _Header.Header.nDataVersion = MakeNonZeroVersion(nDataVersion_);
        _Header.nObjectID = ToColliderPDOID(EntityID_);
        _Header.nFlags = nFlags_;
        _Header.nShapeCount = static_cast<uint32_t>(Shapes_.size());
        _Header.nShapesOffset = Shapes_.empty() ? 0 : sizeof(iCAX::Collider::SObjectColliderPDOHeader);

        std::memcpy(_Payload.data(), &_Header, sizeof(_Header));
        if (!Shapes_.empty())
        {
            std::memcpy(
                _Payload.data() + _Header.nShapesOffset,
                Shapes_.data(),
                Shapes_.size() * sizeof(iCAX::Collider::SColliderShapePDOData));
        }
        return _Payload;
    }

    std::string MakeScenePrefix(
        IN const iCAX::Data::uuid& ProjectID_,
        IN const iCAX::Data::uuid& SceneID_)
    {
        std::ostringstream _Stream;
        _Stream << iCAX::Data::to_string(ProjectID_) << ".scene." << iCAX::Data::to_string(SceneID_);
        return _Stream.str();
    }

    std::string MakeObjectInstanceName(
        IN const iCAX::Data::uuid& ProjectID_,
        IN const iCAX::Data::uuid& SceneID_,
        IN const iCAX::Data::uuid& EntityID_)
    {
        std::ostringstream _Stream;
        _Stream << MakeScenePrefix(ProjectID_, SceneID_) << ".collider.object." << iCAX::Data::to_string(EntityID_);
        return _Stream.str();
    }

    std::string MakeSlotKey(
        IN const iCAX::Data::uuid& ProjectID_,
        IN const iCAX::Data::uuid& SceneID_,
        IN const iCAX::Data::uuid& EntityID_)
    {
        std::ostringstream _Stream;
        _Stream << iCAX::Data::to_string(ProjectID_) << '|'
            << iCAX::Data::to_string(SceneID_) << '|'
            << iCAX::Data::to_string(EntityID_);
        return _Stream.str();
    }

    iCAX::PDO::PDODecl MakeColliderDeclWithID(
        IN iCAX::PDO::PDOID nPDOID_,
        IN uint64_t nPayloadCapacity_)
    {
        auto _Decl = iCAX::Collider::MakeColliderPDODecl(
            iCAX::Collider::EColliderPDOPayloadKind::Object,
            "temporary.instance.name.for.dynamic.collider.pdo",
            iCAX::PDO::kDirection2External,
            nPayloadCapacity_);
        _Decl.nID = nPDOID_;
        return _Decl;
    }

    void SendColliderSlotEvent(
        IN iCAX::Project::IProjectContext& ProjectContext_,
        IN iCAX::Project::ISceneContext& SceneContext_,
        IN uint64_t nEventTypeCode_,
        IN const char* pEventName_,
        IN iCAX::PDO::PDOID nPDOID_,
        IN const iCAX::Data::uuid& ObjectID_,
        IN uint32_t nSlotVersion_,
        IN uint64_t nPayloadCapacity_)
    {
        std::ostringstream _Payload;
        _Payload
            << "{\"event\":\"" << pEventName_
            << "\",\"projectId\":\"" << iCAX::Data::to_string(ProjectContext_.GetProjectID())
            << "\",\"channelId\":\"" << iCAX::Data::to_string(SceneContext_.GetSceneChannelID())
            << "\",\"sceneId\":\"" << iCAX::Data::to_string(SceneContext_.GetSceneID())
            << "\",\"slotRole\":\"Object"
            << "\",\"payloadKind\":\"collider.object"
            << "\",\"pdoId\":\"" << nPDOID_
            << "\",\"objectId\":\"" << (ObjectID_.is_nil() ? std::string() : iCAX::Data::to_string(ObjectID_))
            << "\",\"transformId\":\"" << (ObjectID_.is_nil() ? std::string() : iCAX::Data::to_string(ObjectID_))
            << "\",\"slotVersion\":\"" << nSlotVersion_
            << "\",\"payloadCapacity\":\"" << nPayloadCapacity_
            << "\"}";

        SceneContext_.GetBackendFacadeEndpoint().SendText(
            0,
            nEventTypeCode_,
            iCAX::Interaction::EFacadeFrameKind::Event,
            _Payload.str());
    }

    void SendColliderDefragEvent(
        IN iCAX::Project::IProjectContext& ProjectContext_,
        IN iCAX::Project::ISceneContext& SceneContext_,
        IN uint64_t nEventTypeCode_,
        IN const char* pEventName_,
        IN iCAX::PDO::PDOID nPDOID_)
    {
        std::ostringstream _Payload;
        _Payload
            << "{\"event\":\"" << pEventName_
            << "\",\"projectId\":\"" << iCAX::Data::to_string(ProjectContext_.GetProjectID())
            << "\",\"channelId\":\"" << iCAX::Data::to_string(SceneContext_.GetSceneChannelID())
            << "\",\"sceneId\":\"" << iCAX::Data::to_string(SceneContext_.GetSceneID())
            << "\",\"pdoId\":\"" << nPDOID_
            << "\"}";

        SceneContext_.GetBackendFacadeEndpoint().SendText(
            0,
            nEventTypeCode_,
            iCAX::Interaction::EFacadeFrameKind::Event,
            _Payload.str());
    }

    iCAX::PDO::CPDOHubAllocationCallbacks MakeAllocationCallbacks(
        IN iCAX::Project::IProjectContext& ProjectContext_,
        IN iCAX::Project::ISceneContext& SceneContext_)
    {
        iCAX::PDO::CPDOHubAllocationCallbacks _Callbacks;
        _Callbacks.OnDefragmentBegin =
            [&ProjectContext_, &SceneContext_]()
            {
                SendColliderDefragEvent(ProjectContext_, SceneContext_, kPDOColliderDefragBeginEvent, "DefragBegin", 0);
            };
        _Callbacks.OnDefragmentEnd =
            [&ProjectContext_, &SceneContext_](IN const std::vector<iCAX::PDO::CPDOHubDefragMove>& Moves_)
            {
                for (const auto& _Move : Moves_)
                {
                    SendColliderDefragEvent(ProjectContext_, SceneContext_, kPDOColliderSlotMovedEvent, "SlotMoved", _Move.nPDOID);
                }
                SendColliderDefragEvent(ProjectContext_, SceneContext_, kPDOColliderDefragEndEvent, "DefragEnd", 0);
            };
        return _Callbacks;
    }

    void EnsureSlot(
        IN iCAX::PDO::PDOID nPDOID_,
        IN uint64_t nPayloadCapacity_,
        IN iCAX::PDO::CPDOHubAllocationCallbacks& AllocationCallbacks_,
        IN iCAX::PDO::IPDOHub& PDOHub_,
        IN OUT SSlotAssignment& Assignment_)
    {
        if (Assignment_.nPDOID == 0)
        {
            const auto _Decl = MakeColliderDeclWithID(nPDOID_, nPayloadCapacity_);
            auto& _Slot = PDOHub_.AllocateSlot(_Decl, AllocationCallbacks_);
            Assignment_.nPDOID = nPDOID_;
            Assignment_.nSlotVersion = _Slot.GetHeader().nVersion;
            Assignment_.nPayloadCapacity = nPayloadCapacity_;
            Assignment_.bNeedPublishAllocatedEvent = true;
            return;
        }

        if (Assignment_.nPDOID != nPDOID_)
        {
            throw std::logic_error("Collider PDO slot assignment id is inconsistent");
        }

        if (Assignment_.nPayloadCapacity < nPayloadCapacity_)
        {
            PDOHub_.FreeSlot(Assignment_.nPDOID);
            const auto _Decl = MakeColliderDeclWithID(nPDOID_, nPayloadCapacity_);
            auto& _Slot = PDOHub_.AllocateSlot(_Decl, AllocationCallbacks_);
            Assignment_.nSlotVersion = _Slot.GetHeader().nVersion;
            Assignment_.nPayloadCapacity = nPayloadCapacity_;
            Assignment_.bNeedPublishAllocatedEvent = true;
        }
    }

    bool CommitPayloadToSlot(
        IN const std::vector<std::byte>& Payload_,
        IN iCAX::PDO::IPDOSlot& Slot_,
        IN uint64_t nDataVersion_)
    {
        if (Payload_.size() > static_cast<size_t>(Slot_.GetHeader().nPayloadSize))
        {
            throw std::runtime_error("Collider PDO payload does not fit allocated slot");
        }

        auto _Lease = iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(Slot_, MakeNonZeroVersion(nDataVersion_));
        if (!_Lease)
        {
            return false;
        }
        std::memcpy(_Lease->Data(), Payload_.data(), Payload_.size());
        _Lease->Commit();
        Slot_.SwapBuffersIfReady();
        return true;
    }

    class CMachineColliderBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CMachineColliderBehaviour)

    public:
        std::string GetComponentClass() const override
        {
            return iCAX::CAM::CMachineCollisionComponent::S_ClassName;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = 3200;
            _Schedule.RunAfter = { "CTransformBehaviour" };
            return _Schedule;
        }

    protected:
        void OnAwake(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, true);
        }

        void OnStart(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, true);
        }

        void OnUpdate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN const double&,
            IN const double&) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, true);
        }

        void OnEnable(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, true);
        }

        void OnDisable(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, false);
        }

        void OnDestroyImmediate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            const auto _pEntity = Component_.GetEntity();
            if (_pEntity)
            {
                RemoveByEntityID(_pEntity->GetID(), ProjectContext_, SceneContext_);
            }
        }

        void OnDestroy(
            IN const iCAX::Behaviour::CComponentDestroyInfo& DestroyInfo_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            RemoveByEntityID(DestroyInfo_.EntityID, ProjectContext_, SceneContext_);
        }

    private:
        void RemoveByEntityID(
            IN const iCAX::Data::uuid& EntityID_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_)
        {
            const auto _Key = MakeSlotKey(ProjectContext_.GetProjectID(), SceneContext_.GetSceneID(), EntityID_);
            const auto _Iter = g_ColliderSlots.find(_Key);
            if (_Iter == g_ColliderSlots.end() || _Iter->second.nPDOID == 0)
            {
                return;
            }

            auto& _Assignment = _Iter->second;
            if (SceneContext_.HasPDOHub() && SceneContext_.PDOHub().FreeSlot(_Assignment.nPDOID))
            {
                SendColliderSlotEvent(
                    ProjectContext_,
                    SceneContext_,
                    kPDOColliderSlotFreedEvent,
                    "SlotFreed",
                    _Assignment.nPDOID,
                    EntityID_,
                    _Assignment.nSlotVersion,
                    _Assignment.nPayloadCapacity);
            }
            g_ColliderSlots.erase(_Iter);
        }

        void Publish(
            IN iCAX::Database::CComponentBase& Component_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN bool bEnabled_)
        {
            if (!SceneContext_.HasPDOHub())
            {
                return;
            }

            auto& _Collision = static_cast<iCAX::CAM::CMachineCollisionComponent&>(Component_);
            const auto _pEntity = _Collision.GetEntity();
            if (!_pEntity)
            {
                throw std::runtime_error("MachineCollisionComponent is detached from entity");
            }

            const auto _pTransform = _pEntity->GetComponent<iCAX::Transform::CTransformComponent>();
            if (!_pTransform)
            {
                throw std::runtime_error("Machine collider entity must have Transform component");
            }

            const auto _pAppearance = _pEntity->GetComponent<iCAX::CAM::CMachineAppearanceComponent>();
            const bool _bShowCollision = !_pAppearance || _pAppearance->GetShowCollision();
            uint32_t _Flags = 0;
            if (bEnabled_ && _bShowCollision)
            {
                _Flags |= iCAX::Collider::kColliderFlagVisible;
                _Flags |= iCAX::Collider::kColliderFlagEnabled;
                _Flags |= iCAX::Collider::kColliderFlagSelectable;
            }

            std::vector<iCAX::Collider::SColliderShapePDOData> _Shapes;
            for (const auto& _CollisionVariant : _Collision.GetCollisions())
            {
                const auto _Attachment = TryObjectMap(_CollisionVariant);
                if (!_Attachment)
                {
                    continue;
                }

                iCAX::Collider::SColliderShapePDOData _Shape;
                if (TryMakeColliderShape(*_Attachment, _Shape))
                {
                    if ((_Flags & iCAX::Collider::kColliderFlagVisible) == 0)
                    {
                        _Shape.nFlags = 0;
                    }
                    _Shapes.push_back(_Shape);
                }
            }

            if (_Shapes.empty())
            {
                RemoveByEntityID(_pEntity->GetID(), ProjectContext_, SceneContext_);
                return;
            }

            const auto _DataVersion = MakeNonZeroVersion((std::max)(
                _Collision.Version(),
                _pAppearance ? _pAppearance->Version() : 0ull));
            const auto _Payload = MakeColliderPayload(_pEntity->GetID(), _DataVersion, _Flags, _Shapes);

            auto& _PDOHub = SceneContext_.PDOHub();
            auto _Callbacks = MakeAllocationCallbacks(ProjectContext_, SceneContext_);
            const auto _PDOID = iCAX::PDO::MakePDOID(
                iCAX::Collider::GetColliderPDOPayloadTypeName(iCAX::Collider::EColliderPDOPayloadKind::Object),
                MakeObjectInstanceName(ProjectContext_.GetProjectID(), SceneContext_.GetSceneID(), _pEntity->GetID()));
            const auto _Key = MakeSlotKey(ProjectContext_.GetProjectID(), SceneContext_.GetSceneID(), _pEntity->GetID());
            auto& _Assignment = g_ColliderSlots[_Key];
            EnsureSlot(_PDOID, static_cast<uint64_t>(_Payload.size()), _Callbacks, _PDOHub, _Assignment);

            if (CommitPayloadToSlot(_Payload, _PDOHub.GetSlot(_Assignment.nPDOID), _DataVersion)
                && _Assignment.bNeedPublishAllocatedEvent)
            {
                SendColliderSlotEvent(
                    ProjectContext_,
                    SceneContext_,
                    kPDOColliderSlotAllocatedEvent,
                    "SlotAllocated",
                    _Assignment.nPDOID,
                    _pEntity->GetID(),
                    _Assignment.nSlotVersion,
                    _Assignment.nPayloadCapacity);
                _Assignment.bNeedPublishAllocatedEvent = false;
            }
        }
    };
}
