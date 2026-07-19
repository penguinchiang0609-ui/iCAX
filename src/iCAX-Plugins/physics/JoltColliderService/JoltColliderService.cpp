#include "pch.h"
#include "JoltColliderService.h"


namespace
{
    iCAX::JoltPhysics::SPhysicsVec3 ToJoltVec3(IN const iCAX::Collider::SColliderVec3& Value_)
    {
        return { Value_.x, Value_.y, Value_.z };
    }

    iCAX::Collider::SColliderVec3 FromJoltVec3(IN const iCAX::JoltPhysics::SPhysicsVec3& Value_)
    {
        return { Value_.x, Value_.y, Value_.z };
    }

    iCAX::JoltPhysics::SPhysicsQuat ToJoltQuat(IN const iCAX::Collider::SColliderQuat& Value_)
    {
        return { Value_.x, Value_.y, Value_.z, Value_.w };
    }

    iCAX::Collider::SColliderQuat FromJoltQuat(IN const iCAX::JoltPhysics::SPhysicsQuat& Value_)
    {
        return { Value_.x, Value_.y, Value_.z, Value_.w };
    }

    iCAX::JoltPhysics::SPhysicsTransform ToJoltTransform(IN const iCAX::Collider::SColliderTransform& Value_)
    {
        return { ToJoltVec3(Value_.Position), ToJoltQuat(Value_.Rotation) };
    }

    iCAX::Collider::SColliderTransform FromJoltTransform(IN const iCAX::JoltPhysics::SPhysicsTransform& Value_)
    {
        return { FromJoltVec3(Value_.Position), FromJoltQuat(Value_.Rotation) };
    }

    iCAX::JoltPhysics::EPhysicsBodyMotionType ToJoltMotionType(IN iCAX::Collider::EColliderMotionType eMotionType_)
    {
        switch (eMotionType_)
        {
        case iCAX::Collider::EColliderMotionType::Static:
            return iCAX::JoltPhysics::EPhysicsBodyMotionType::Static;
        case iCAX::Collider::EColliderMotionType::Kinematic:
            return iCAX::JoltPhysics::EPhysicsBodyMotionType::Kinematic;
        case iCAX::Collider::EColliderMotionType::Dynamic:
            return iCAX::JoltPhysics::EPhysicsBodyMotionType::Dynamic;
        default:
            throw std::invalid_argument("Collider motion type is invalid");
        }
    }

    void ValidateDataVersion(IN iCAX::Collider::ColliderDataVersion nDataVersion_)
    {
        if (nDataVersion_ == 0)
        {
            throw std::invalid_argument("Collider data version cannot be zero");
        }
    }

    void ValidateBox(IN const iCAX::Collider::SBoxColliderData& Collider_)
    {
        ValidateDataVersion(Collider_.nDataVersion);
        if (Collider_.HalfExtent.x <= 0.0f || Collider_.HalfExtent.y <= 0.0f || Collider_.HalfExtent.z <= 0.0f)
        {
            throw std::invalid_argument("Box collider half extent must be positive");
        }
    }

    void ValidateSphere(IN const iCAX::Collider::SSphereColliderData& Collider_)
    {
        ValidateDataVersion(Collider_.nDataVersion);
        if (Collider_.nRadius <= 0.0f)
        {
            throw std::invalid_argument("Sphere collider radius must be positive");
        }
    }

    void ValidateTriangleMesh(IN const iCAX::Collider::STriangleMeshColliderData& Collider_)
    {
        ValidateDataVersion(Collider_.nDataVersion);
        if (Collider_.Vertices.empty() || Collider_.Indices.empty())
        {
            throw std::invalid_argument("Triangle mesh collider vertices and indices cannot be empty");
        }
        if (Collider_.Indices.size() % 3 != 0)
        {
            throw std::invalid_argument("Triangle mesh collider index count must be a multiple of 3");
        }
    }
}

iCAX::JoltColliderService::CJoltColliderService::CJoltColliderService() = default;

iCAX::JoltColliderService::CJoltColliderService::~CJoltColliderService() = default;

void iCAX::JoltColliderService::CJoltColliderService::OnLoad()
{
}

void iCAX::JoltColliderService::CJoltColliderService::OnUnload()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Projects.clear();
}

bool iCAX::JoltColliderService::CJoltColliderService::CreateScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto& _pCatalog = m_Projects[ProjectID_];
    if (!_pCatalog)
    {
        _pCatalog = std::make_unique<iCAX::JoltPhysics::CJoltPhysicsSceneCatalog>();
    }
    if (_pCatalog->FindScene(nSceneID_) != nullptr)
    {
        return false;
    }
    (void)_pCatalog->CreateScene(nSceneID_);
    return true;
}

bool iCAX::JoltColliderService::CJoltColliderService::DestroyScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _ProjectIter = m_Projects.find(ProjectID_);
    if (_ProjectIter == m_Projects.end() || !_ProjectIter->second)
    {
        return false;
    }
    const bool _bRemoved = _ProjectIter->second->DestroyScene(nSceneID_);
    if (_ProjectIter->second->ListSceneIDs().empty())
    {
        m_Projects.erase(_ProjectIter);
    }
    return _bRemoved;
}

void iCAX::JoltColliderService::CJoltColliderService::DestroyProject(IN const iCAX::Data::uuid& ProjectID_)
{
    if (ProjectID_.is_nil())
    {
        throw std::invalid_argument("Collider project id cannot be nil");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Projects.erase(ProjectID_);
}

bool iCAX::JoltColliderService::CJoltColliderService::HasScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_) const
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return FindSceneNoLock(ProjectID_, nSceneID_) != nullptr;
}

std::vector<iCAX::Collider::ColliderSceneID> iCAX::JoltColliderService::CJoltColliderService::ListSceneIDs(
    IN const iCAX::Data::uuid& ProjectID_) const
{
    if (ProjectID_.is_nil())
    {
        throw std::invalid_argument("Collider project id cannot be nil");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _ProjectIter = m_Projects.find(ProjectID_);
    if (_ProjectIter == m_Projects.end() || !_ProjectIter->second)
    {
        return {};
    }
    auto _IDs = _ProjectIter->second->ListSceneIDs();
    std::sort(_IDs.begin(), _IDs.end());
    return _IDs;
}

iCAX::Collider::ColliderID iCAX::JoltColliderService::CJoltColliderService::CreateBoxCollider(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_,
    IN const iCAX::Collider::SBoxColliderData& Collider_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidateBox(Collider_);

    iCAX::JoltPhysics::SBoxBodyDesc _Desc;
    _Desc.nOwnerObjectID = Collider_.nOwnerObjectID;
    _Desc.eMotionType = ToJoltMotionType(Collider_.eMotionType);
    _Desc.Transform = ToJoltTransform(Collider_.Transform);
    _Desc.HalfExtent = ToJoltVec3(Collider_.HalfExtent);
    _Desc.nDensity = Collider_.Material.nDensity;
    _Desc.nFriction = Collider_.Material.nFriction;
    _Desc.nRestitution = Collider_.Material.nRestitution;
    _Desc.bActive = Collider_.bActive;

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return RequireSceneNoLock(ProjectID_, nSceneID_).CreateBoxBody(_Desc);
}

iCAX::Collider::ColliderID iCAX::JoltColliderService::CJoltColliderService::CreateSphereCollider(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_,
    IN const iCAX::Collider::SSphereColliderData& Collider_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidateSphere(Collider_);

    iCAX::JoltPhysics::SSphereBodyDesc _Desc;
    _Desc.nOwnerObjectID = Collider_.nOwnerObjectID;
    _Desc.eMotionType = ToJoltMotionType(Collider_.eMotionType);
    _Desc.Transform = ToJoltTransform(Collider_.Transform);
    _Desc.nRadius = Collider_.nRadius;
    _Desc.nDensity = Collider_.Material.nDensity;
    _Desc.nFriction = Collider_.Material.nFriction;
    _Desc.nRestitution = Collider_.Material.nRestitution;
    _Desc.bActive = Collider_.bActive;

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return RequireSceneNoLock(ProjectID_, nSceneID_).CreateSphereBody(_Desc);
}

iCAX::Collider::ColliderID iCAX::JoltColliderService::CJoltColliderService::CreateTriangleMeshCollider(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_,
    IN const iCAX::Collider::STriangleMeshColliderData& Collider_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidateTriangleMesh(Collider_);

    std::vector<iCAX::JoltPhysics::SPhysicsVec3> _Vertices;
    _Vertices.reserve(Collider_.Vertices.size());
    for (const auto& _Vertex : Collider_.Vertices)
    {
        _Vertices.push_back(ToJoltVec3(_Vertex));
    }

    iCAX::JoltPhysics::STriangleMeshBodyDesc _Desc;
    _Desc.nOwnerObjectID = Collider_.nOwnerObjectID;
    _Desc.eMotionType = ToJoltMotionType(Collider_.eMotionType);
    _Desc.Transform = ToJoltTransform(Collider_.Transform);
    _Desc.pVertices = _Vertices.data();
    _Desc.nVertexCount = static_cast<uint32_t>(_Vertices.size());
    _Desc.pIndices = Collider_.Indices.data();
    _Desc.nIndexCount = static_cast<uint32_t>(Collider_.Indices.size());
    _Desc.nFriction = Collider_.Material.nFriction;
    _Desc.nRestitution = Collider_.Material.nRestitution;
    _Desc.bActive = Collider_.bActive;

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return RequireSceneNoLock(ProjectID_, nSceneID_).CreateTriangleMeshBody(_Desc);
}

bool iCAX::JoltColliderService::CJoltColliderService::DestroyCollider(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_,
    IN iCAX::Collider::ColliderID nColliderID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nColliderID_ == iCAX::Collider::kInvalidColliderID)
    {
        throw std::invalid_argument("Collider id cannot be zero");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return RequireSceneNoLock(ProjectID_, nSceneID_).DestroyBody(nColliderID_);
}

bool iCAX::JoltColliderService::CJoltColliderService::SetColliderTransform(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_,
    IN iCAX::Collider::ColliderID nColliderID_,
    IN const iCAX::Collider::SColliderTransform& Transform_,
    IN bool bActivate_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nColliderID_ == iCAX::Collider::kInvalidColliderID)
    {
        throw std::invalid_argument("Collider id cannot be zero");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return RequireSceneNoLock(ProjectID_, nSceneID_).SetBodyTransform(nColliderID_, ToJoltTransform(Transform_), bActivate_);
}

bool iCAX::JoltColliderService::CJoltColliderService::GetColliderTransform(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_,
    IN iCAX::Collider::ColliderID nColliderID_,
    OUT iCAX::Collider::SColliderTransform& Transform_) const
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nColliderID_ == iCAX::Collider::kInvalidColliderID)
    {
        throw std::invalid_argument("Collider id cannot be zero");
    }

    iCAX::JoltPhysics::SPhysicsTransform _Transform;
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const bool _bFound = RequireSceneNoLock(ProjectID_, nSceneID_).GetBodyTransform(nColliderID_, _Transform);
    if (_bFound)
    {
        Transform_ = FromJoltTransform(_Transform);
    }
    return _bFound;
}

void iCAX::JoltColliderService::CJoltColliderService::Step(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_,
    IN float nDeltaSeconds_,
    IN int nCollisionSteps_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    RequireSceneNoLock(ProjectID_, nSceneID_).Step(nDeltaSeconds_, nCollisionSteps_);
}

iCAX::Collider::SColliderRaycastHit iCAX::JoltColliderService::CJoltColliderService::Raycast(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_,
    IN const iCAX::Collider::SColliderRaycastRequest& Request_) const
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    iCAX::JoltPhysics::SPhysicsRaycastRequest _Request;
    _Request.Origin = ToJoltVec3(Request_.Origin);
    _Request.Direction = ToJoltVec3(Request_.Direction);
    _Request.nMaxDistance = Request_.nMaxDistance;

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _Hit = RequireSceneNoLock(ProjectID_, nSceneID_).Raycast(_Request);

    iCAX::Collider::SColliderRaycastHit _Result;
    _Result.bHit = _Hit.bHit;
    _Result.nColliderID = _Hit.nBodyID;
    _Result.nOwnerObjectID = _Hit.nOwnerObjectID;
    _Result.Position = FromJoltVec3(_Hit.Position);
    _Result.Normal = FromJoltVec3(_Hit.Normal);
    _Result.nDistance = _Hit.nDistance;
    return _Result;
}

iCAX::JoltPhysics::CJoltPhysicsScene* iCAX::JoltColliderService::CJoltColliderService::FindSceneNoLock(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_)
{
    auto _ProjectIter = m_Projects.find(ProjectID_);
    if (_ProjectIter == m_Projects.end() || !_ProjectIter->second)
    {
        return nullptr;
    }
    return _ProjectIter->second->FindScene(nSceneID_);
}

const iCAX::JoltPhysics::CJoltPhysicsScene* iCAX::JoltColliderService::CJoltColliderService::FindSceneNoLock(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_) const
{
    auto _ProjectIter = m_Projects.find(ProjectID_);
    if (_ProjectIter == m_Projects.end() || !_ProjectIter->second)
    {
        return nullptr;
    }
    return _ProjectIter->second->FindScene(nSceneID_);
}

iCAX::JoltPhysics::CJoltPhysicsScene& iCAX::JoltColliderService::CJoltColliderService::RequireSceneNoLock(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_)
{
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        throw std::logic_error("Collider scene does not exist");
    }
    return *_pScene;
}

const iCAX::JoltPhysics::CJoltPhysicsScene& iCAX::JoltColliderService::CJoltColliderService::RequireSceneNoLock(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_) const
{
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        throw std::logic_error("Collider scene does not exist");
    }
    return *_pScene;
}

void iCAX::JoltColliderService::CJoltColliderService::ValidateProjectAndScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Collider::ColliderSceneID nSceneID_)
{
    if (ProjectID_.is_nil())
    {
        throw std::invalid_argument("Collider project id cannot be nil");
    }
    if (nSceneID_ == iCAX::Collider::kInvalidColliderSceneID)
    {
        throw std::invalid_argument("Collider scene id cannot be zero");
    }
}
