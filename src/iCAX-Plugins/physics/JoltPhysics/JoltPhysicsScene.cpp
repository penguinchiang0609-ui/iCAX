#include "pch.h"
#include "JoltPhysicsScene.h"



namespace
{
    namespace Layers
    {
        static constexpr JPH::ObjectLayer kStatic = 0;
        static constexpr JPH::ObjectLayer kMoving = 1;
        static constexpr JPH::ObjectLayer kCount = 2;
    }

    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer kStatic(0);
        static constexpr JPH::BroadPhaseLayer kMoving(1);
        static constexpr uint32_t kCount = 2;
    }

    JPH::Vec3 _ToJolt(IN const iCAX::JoltPhysics::SPhysicsVec3& Value_) noexcept
    {
        return JPH::Vec3(Value_.x, Value_.y, Value_.z);
    }

    JPH::Quat _ToJolt(IN const iCAX::JoltPhysics::SPhysicsQuat& Value_) noexcept
    {
        return JPH::Quat(Value_.x, Value_.y, Value_.z, Value_.w);
    }

    iCAX::JoltPhysics::SPhysicsVec3 _FromJolt(IN const JPH::Vec3& Value_) noexcept
    {
        return iCAX::JoltPhysics::SPhysicsVec3{ Value_.GetX(), Value_.GetY(), Value_.GetZ() };
    }

    iCAX::JoltPhysics::SPhysicsQuat _FromJolt(IN const JPH::Quat& Value_) noexcept
    {
        return iCAX::JoltPhysics::SPhysicsQuat{ Value_.GetX(), Value_.GetY(), Value_.GetZ(), Value_.GetW() };
    }

    JPH::EMotionType _ToJolt(IN iCAX::JoltPhysics::EPhysicsBodyMotionType eMotionType_) noexcept
    {
        switch (eMotionType_)
        {
        case iCAX::JoltPhysics::EPhysicsBodyMotionType::Dynamic:
            return JPH::EMotionType::Dynamic;
        case iCAX::JoltPhysics::EPhysicsBodyMotionType::Kinematic:
            return JPH::EMotionType::Kinematic;
        case iCAX::JoltPhysics::EPhysicsBodyMotionType::Static:
        default:
            return JPH::EMotionType::Static;
        }
    }

    JPH::ObjectLayer _ToObjectLayer(IN iCAX::JoltPhysics::EPhysicsBodyMotionType eMotionType_) noexcept
    {
        return eMotionType_ == iCAX::JoltPhysics::EPhysicsBodyMotionType::Static
            ? Layers::kStatic
            : Layers::kMoving;
    }

    class CObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer Layer1_, JPH::ObjectLayer Layer2_) const override
        {
            if (Layer1_ == Layers::kStatic && Layer2_ == Layers::kStatic)
            {
                return false;
            }
            return true;
        }
    };

    class CBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
    {
    public:
        uint32_t GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::kCount;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer Layer_) const override
        {
            return Layer_ == Layers::kStatic ? BroadPhaseLayers::kStatic : BroadPhaseLayers::kMoving;
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer Layer_) const override
        {
            switch (static_cast<JPH::BroadPhaseLayer::Type>(Layer_))
            {
            case static_cast<JPH::BroadPhaseLayer::Type>(0):
                return "Static";
            case static_cast<JPH::BroadPhaseLayer::Type>(1):
                return "Moving";
            default:
                return "Unknown";
            }
        }
#endif
    };

    class CObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer Layer_, JPH::BroadPhaseLayer BroadPhaseLayer_) const override
        {
            if (Layer_ == Layers::kStatic && BroadPhaseLayer_ == BroadPhaseLayers::kStatic)
            {
                return false;
            }
            return true;
        }
    };

    void _EnsureJoltRegistered()
    {
        static std::once_flag _Once;
        std::call_once(_Once, []() {
            JPH::RegisterDefaultAllocator();
            if (JPH::Factory::sInstance == nullptr)
            {
                JPH::Factory::sInstance = new JPH::Factory();
            }
            JPH::RegisterTypes();
        });
    }

    void _ValidateTriangleMeshDesc(IN const iCAX::JoltPhysics::STriangleMeshBodyDesc& Desc_)
    {
        if (Desc_.pVertices == nullptr || Desc_.nVertexCount == 0)
        {
            throw std::invalid_argument("triangle mesh body requires vertices");
        }
        if (Desc_.pIndices == nullptr || Desc_.nIndexCount == 0)
        {
            throw std::invalid_argument("triangle mesh body requires indices");
        }
        if (Desc_.nIndexCount % 3 != 0)
        {
            throw std::invalid_argument("triangle mesh index count must be a multiple of 3");
        }
        for (uint32_t _Index = 0; _Index < Desc_.nIndexCount; ++_Index)
        {
            if (Desc_.pIndices[_Index] >= Desc_.nVertexCount)
            {
                throw std::invalid_argument("triangle mesh index is out of vertex range");
            }
        }
    }
}

class iCAX::JoltPhysics::CJoltPhysicsScene::Impl final
{
public:
    PhysicsSceneID nSceneID = kInvalidPhysicsSceneID;
    bool bInitialized = false;
    JPH::PhysicsSystem PhysicsSystem;
    CBroadPhaseLayerInterface BroadPhaseLayerInterface;
    CObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseLayerFilter;
    CObjectLayerPairFilter ObjectLayerPairFilter;
    JPH::TempAllocatorImpl TempAllocator{ 16 * 1024 * 1024 };
    JPH::JobSystemSingleThreaded JobSystem{ 2048 };
    PhysicsBodyID nNextBodyID = 1;
    std::unordered_map<PhysicsBodyID, JPH::BodyID> Bodies;
    std::unordered_map<uint32_t, PhysicsBodyID> BodyIDToPublicID;
    std::unordered_map<PhysicsBodyID, PhysicsObjectID> BodyOwnerObjectIDs;
};

iCAX::JoltPhysics::CJoltPhysicsScene::CJoltPhysicsScene()
{
    _EnsureJoltRegistered();
    m_pImpl = std::make_unique<Impl>();
}

iCAX::JoltPhysics::CJoltPhysicsScene::~CJoltPhysicsScene()
{
    Shutdown();
}

iCAX::JoltPhysics::CJoltPhysicsScene::CJoltPhysicsScene(CJoltPhysicsScene&&) noexcept = default;

iCAX::JoltPhysics::CJoltPhysicsScene& iCAX::JoltPhysics::CJoltPhysicsScene::operator=(CJoltPhysicsScene&&) noexcept = default;

void iCAX::JoltPhysics::CJoltPhysicsScene::Initialize(IN PhysicsSceneID nSceneID_)
{
    if (nSceneID_ == kInvalidPhysicsSceneID)
    {
        throw std::invalid_argument("Jolt physics scene id cannot be zero");
    }
    if (m_pImpl->bInitialized)
    {
        throw std::logic_error("Jolt physics scene is already initialized");
    }

    constexpr uint32_t kMaxBodies = 65536;
    constexpr uint32_t kBodyMutexCount = 0;
    constexpr uint32_t kMaxBodyPairs = 65536;
    constexpr uint32_t kMaxContactConstraints = 10240;
    m_pImpl->PhysicsSystem.Init(
        kMaxBodies,
        kBodyMutexCount,
        kMaxBodyPairs,
        kMaxContactConstraints,
        m_pImpl->BroadPhaseLayerInterface,
        m_pImpl->ObjectVsBroadPhaseLayerFilter,
        m_pImpl->ObjectLayerPairFilter);
    m_pImpl->nSceneID = nSceneID_;
    m_pImpl->bInitialized = true;
}

void iCAX::JoltPhysics::CJoltPhysicsScene::Shutdown() noexcept
{
    if (!m_pImpl || !m_pImpl->bInitialized)
    {
        return;
    }

    auto& _BodyInterface = m_pImpl->PhysicsSystem.GetBodyInterface();
    for (const auto& _Pair : m_pImpl->Bodies)
    {
        _BodyInterface.RemoveBody(_Pair.second);
        _BodyInterface.DestroyBody(_Pair.second);
    }
    m_pImpl->Bodies.clear();
    m_pImpl->BodyIDToPublicID.clear();
    m_pImpl->BodyOwnerObjectIDs.clear();
    m_pImpl->nSceneID = kInvalidPhysicsSceneID;
    m_pImpl->bInitialized = false;
}

bool iCAX::JoltPhysics::CJoltPhysicsScene::IsInitialized() const noexcept
{
    return m_pImpl && m_pImpl->bInitialized;
}

iCAX::JoltPhysics::PhysicsSceneID iCAX::JoltPhysics::CJoltPhysicsScene::GetSceneID() const noexcept
{
    return m_pImpl ? m_pImpl->nSceneID : kInvalidPhysicsSceneID;
}

void iCAX::JoltPhysics::CJoltPhysicsScene::SetGravity(IN const SPhysicsVec3& Gravity_)
{
    if (!IsInitialized())
    {
        throw std::logic_error("Jolt physics scene is not initialized");
    }
    m_pImpl->PhysicsSystem.SetGravity(_ToJolt(Gravity_));
}

iCAX::JoltPhysics::SPhysicsVec3 iCAX::JoltPhysics::CJoltPhysicsScene::GetGravity() const
{
    if (!IsInitialized())
    {
        throw std::logic_error("Jolt physics scene is not initialized");
    }
    return _FromJolt(m_pImpl->PhysicsSystem.GetGravity());
}

iCAX::JoltPhysics::PhysicsBodyID iCAX::JoltPhysics::CJoltPhysicsScene::CreateBoxBody(IN const SBoxBodyDesc& Desc_)
{
    if (!IsInitialized())
    {
        throw std::logic_error("Jolt physics scene is not initialized");
    }

    JPH::BoxShapeSettings _ShapeSettings(_ToJolt(Desc_.HalfExtent));
    _ShapeSettings.SetDensity(Desc_.nDensity);
    JPH::ShapeSettings::ShapeResult _ShapeResult = _ShapeSettings.Create();
    if (_ShapeResult.HasError())
    {
        throw std::runtime_error(_ShapeResult.GetError().c_str());
    }

    JPH::BodyCreationSettings _Settings(
        _ShapeResult.Get(),
        JPH::RVec3(Desc_.Transform.Position.x, Desc_.Transform.Position.y, Desc_.Transform.Position.z),
        _ToJolt(Desc_.Transform.Rotation),
        _ToJolt(Desc_.eMotionType),
        _ToObjectLayer(Desc_.eMotionType));
    _Settings.mFriction = Desc_.nFriction;
    _Settings.mRestitution = Desc_.nRestitution;

    const PhysicsBodyID _PublicBodyID = m_pImpl->nNextBodyID++;
    _Settings.mUserData = _PublicBodyID;
    JPH::BodyID _BodyID = m_pImpl->PhysicsSystem.GetBodyInterface().CreateAndAddBody(
        _Settings,
        Desc_.bActive ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

    m_pImpl->Bodies.emplace(_PublicBodyID, _BodyID);
    m_pImpl->BodyIDToPublicID.emplace(_BodyID.GetIndexAndSequenceNumber(), _PublicBodyID);
    m_pImpl->BodyOwnerObjectIDs.emplace(_PublicBodyID, Desc_.nOwnerObjectID);
    return _PublicBodyID;
}

iCAX::JoltPhysics::PhysicsBodyID iCAX::JoltPhysics::CJoltPhysicsScene::CreateSphereBody(IN const SSphereBodyDesc& Desc_)
{
    if (!IsInitialized())
    {
        throw std::logic_error("Jolt physics scene is not initialized");
    }

    JPH::SphereShapeSettings _ShapeSettings(Desc_.nRadius);
    _ShapeSettings.SetDensity(Desc_.nDensity);
    JPH::ShapeSettings::ShapeResult _ShapeResult = _ShapeSettings.Create();
    if (_ShapeResult.HasError())
    {
        throw std::runtime_error(_ShapeResult.GetError().c_str());
    }

    JPH::BodyCreationSettings _Settings(
        _ShapeResult.Get(),
        JPH::RVec3(Desc_.Transform.Position.x, Desc_.Transform.Position.y, Desc_.Transform.Position.z),
        _ToJolt(Desc_.Transform.Rotation),
        _ToJolt(Desc_.eMotionType),
        _ToObjectLayer(Desc_.eMotionType));
    _Settings.mFriction = Desc_.nFriction;
    _Settings.mRestitution = Desc_.nRestitution;

    const PhysicsBodyID _PublicBodyID = m_pImpl->nNextBodyID++;
    _Settings.mUserData = _PublicBodyID;
    JPH::BodyID _BodyID = m_pImpl->PhysicsSystem.GetBodyInterface().CreateAndAddBody(
        _Settings,
        Desc_.bActive ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

    m_pImpl->Bodies.emplace(_PublicBodyID, _BodyID);
    m_pImpl->BodyIDToPublicID.emplace(_BodyID.GetIndexAndSequenceNumber(), _PublicBodyID);
    m_pImpl->BodyOwnerObjectIDs.emplace(_PublicBodyID, Desc_.nOwnerObjectID);
    return _PublicBodyID;
}

iCAX::JoltPhysics::PhysicsBodyID iCAX::JoltPhysics::CJoltPhysicsScene::CreateTriangleMeshBody(IN const STriangleMeshBodyDesc& Desc_)
{
    if (!IsInitialized())
    {
        throw std::logic_error("Jolt physics scene is not initialized");
    }
    _ValidateTriangleMeshDesc(Desc_);

    JPH::VertexList _Vertices;
    _Vertices.reserve(Desc_.nVertexCount);
    for (uint32_t _Index = 0; _Index < Desc_.nVertexCount; ++_Index)
    {
        const SPhysicsVec3& _Vertex = Desc_.pVertices[_Index];
        _Vertices.emplace_back(_Vertex.x, _Vertex.y, _Vertex.z);
    }

    JPH::IndexedTriangleList _Triangles;
    _Triangles.reserve(Desc_.nIndexCount / 3);
    for (uint32_t _Index = 0; _Index < Desc_.nIndexCount; _Index += 3)
    {
        _Triangles.emplace_back(Desc_.pIndices[_Index], Desc_.pIndices[_Index + 1], Desc_.pIndices[_Index + 2], 0);
    }

    JPH::MeshShapeSettings _ShapeSettings(std::move(_Vertices), std::move(_Triangles));
    JPH::ShapeSettings::ShapeResult _ShapeResult = _ShapeSettings.Create();
    if (_ShapeResult.HasError())
    {
        throw std::runtime_error(_ShapeResult.GetError().c_str());
    }

    JPH::BodyCreationSettings _Settings(
        _ShapeResult.Get(),
        JPH::RVec3(Desc_.Transform.Position.x, Desc_.Transform.Position.y, Desc_.Transform.Position.z),
        _ToJolt(Desc_.Transform.Rotation),
        _ToJolt(Desc_.eMotionType),
        _ToObjectLayer(Desc_.eMotionType));
    _Settings.mFriction = Desc_.nFriction;
    _Settings.mRestitution = Desc_.nRestitution;

    const PhysicsBodyID _PublicBodyID = m_pImpl->nNextBodyID++;
    _Settings.mUserData = _PublicBodyID;
    JPH::BodyID _BodyID = m_pImpl->PhysicsSystem.GetBodyInterface().CreateAndAddBody(
        _Settings,
        Desc_.bActive ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

    m_pImpl->Bodies.emplace(_PublicBodyID, _BodyID);
    m_pImpl->BodyIDToPublicID.emplace(_BodyID.GetIndexAndSequenceNumber(), _PublicBodyID);
    m_pImpl->BodyOwnerObjectIDs.emplace(_PublicBodyID, Desc_.nOwnerObjectID);
    return _PublicBodyID;
}

bool iCAX::JoltPhysics::CJoltPhysicsScene::DestroyBody(IN PhysicsBodyID nBodyID_)
{
    const auto _Iter = m_pImpl->Bodies.find(nBodyID_);
    if (_Iter == m_pImpl->Bodies.end())
    {
        return false;
    }

    auto& _BodyInterface = m_pImpl->PhysicsSystem.GetBodyInterface();
    _BodyInterface.RemoveBody(_Iter->second);
    _BodyInterface.DestroyBody(_Iter->second);
    m_pImpl->BodyIDToPublicID.erase(_Iter->second.GetIndexAndSequenceNumber());
    m_pImpl->BodyOwnerObjectIDs.erase(nBodyID_);
    m_pImpl->Bodies.erase(_Iter);
    return true;
}

bool iCAX::JoltPhysics::CJoltPhysicsScene::SetBodyTransform(
    IN PhysicsBodyID nBodyID_,
    IN const SPhysicsTransform& Transform_,
    IN bool bActivate_)
{
    const auto _Iter = m_pImpl->Bodies.find(nBodyID_);
    if (_Iter == m_pImpl->Bodies.end())
    {
        return false;
    }

    m_pImpl->PhysicsSystem.GetBodyInterface().SetPositionAndRotation(
        _Iter->second,
        JPH::RVec3(Transform_.Position.x, Transform_.Position.y, Transform_.Position.z),
        _ToJolt(Transform_.Rotation),
        bActivate_ ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    return true;
}

bool iCAX::JoltPhysics::CJoltPhysicsScene::GetBodyTransform(
    IN PhysicsBodyID nBodyID_,
    OUT SPhysicsTransform& Transform_) const
{
    const auto _Iter = m_pImpl->Bodies.find(nBodyID_);
    if (_Iter == m_pImpl->Bodies.end())
    {
        return false;
    }

    const auto& _BodyInterface = m_pImpl->PhysicsSystem.GetBodyInterface();
    const JPH::RVec3 _Position = _BodyInterface.GetCenterOfMassPosition(_Iter->second);
    const JPH::Quat _Rotation = _BodyInterface.GetRotation(_Iter->second);
    Transform_.Position = SPhysicsVec3{
        static_cast<float>(_Position.GetX()),
        static_cast<float>(_Position.GetY()),
        static_cast<float>(_Position.GetZ())
    };
    Transform_.Rotation = _FromJolt(_Rotation);
    return true;
}

void iCAX::JoltPhysics::CJoltPhysicsScene::Step(IN float nDeltaSeconds_, IN int nCollisionSteps_)
{
    if (!IsInitialized())
    {
        throw std::logic_error("Jolt physics scene is not initialized");
    }
    if (nDeltaSeconds_ <= 0.0f)
    {
        return;
    }

    const int _CollisionSteps = (std::max)(1, nCollisionSteps_);
    m_pImpl->PhysicsSystem.Update(
        nDeltaSeconds_,
        _CollisionSteps,
        &m_pImpl->TempAllocator,
        &m_pImpl->JobSystem);
}

iCAX::JoltPhysics::SPhysicsRaycastHit iCAX::JoltPhysics::CJoltPhysicsScene::Raycast(IN const SPhysicsRaycastRequest& Request_) const
{
    if (!IsInitialized())
    {
        throw std::logic_error("Jolt physics scene is not initialized");
    }
    if (Request_.nMaxDistance <= 0.0f)
    {
        return {};
    }

    JPH::Vec3 _Direction = _ToJolt(Request_.Direction);
    const float _Length = _Direction.Length();
    if (_Length <= 0.0f)
    {
        return {};
    }
    _Direction /= _Length;

    JPH::RRayCast _Ray(
        JPH::RVec3(Request_.Origin.x, Request_.Origin.y, Request_.Origin.z),
        _Direction * Request_.nMaxDistance);

    JPH::RayCastResult _Result;
    if (!m_pImpl->PhysicsSystem.GetNarrowPhaseQuery().CastRay(_Ray, _Result))
    {
        return {};
    }

    const uint32_t _BodyKey = _Result.mBodyID.GetIndexAndSequenceNumber();
    const auto _PublicIter = m_pImpl->BodyIDToPublicID.find(_BodyKey);
    const PhysicsBodyID _PublicBodyID = _PublicIter == m_pImpl->BodyIDToPublicID.end()
        ? kInvalidPhysicsBodyID
        : _PublicIter->second;
    const float _Distance = _Result.mFraction * Request_.nMaxDistance;
    const JPH::RVec3 _HitPosition = _Ray.GetPointOnRay(_Result.mFraction);

    SPhysicsRaycastHit _Hit;
    _Hit.bHit = true;
    _Hit.nBodyID = _PublicBodyID;
    const auto _OwnerIter = m_pImpl->BodyOwnerObjectIDs.find(_PublicBodyID);
    _Hit.nOwnerObjectID = _OwnerIter == m_pImpl->BodyOwnerObjectIDs.end() ? 0 : _OwnerIter->second;
    _Hit.Position = SPhysicsVec3{
        static_cast<float>(_HitPosition.GetX()),
        static_cast<float>(_HitPosition.GetY()),
        static_cast<float>(_HitPosition.GetZ())
    };
    JPH::BodyLockRead _BodyLock(m_pImpl->PhysicsSystem.GetBodyLockInterface(), _Result.mBodyID);
    if (_BodyLock.Succeeded())
    {
        _Hit.Normal = _FromJolt(_BodyLock.GetBody().GetWorldSpaceSurfaceNormal(_Result.mSubShapeID2, _HitPosition));
    }
    _Hit.nDistance = _Distance;
    return _Hit;
}
