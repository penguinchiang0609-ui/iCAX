#include "pch.h"
#include "CommandInternal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace iCAX::CAM::Commands::Internal
{
namespace
{
    constexpr double kSDFLengthToWorld = 1000.0;
}

    std::optional<ObjectMap> _TryGetObjectMapField(IN const ObjectMap& Object_, IN const std::string& strName_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<ObjectMap>())
        {
            return std::nullopt;
        }
        return _Iter->second.To<ObjectMap>();
    }

    double _GetObjectDoubleOrDefault(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN double dDefault_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return dDefault_;
        }
        try
        {
            return _ToDouble(_Iter->second, strName_);
        }
        catch (const std::exception&)
        {
            return dDefault_;
        }
    }

    struct SMachineJointState final
    {
        VariantArray Names;
        VariantArray Positions;
        VariantArray LowerLimits;
        VariantArray UpperLimits;
    };

    bool _IsMovableSDFJoint(IN const std::string& strJointType_)
    {
        return !strJointType_.empty() && strJointType_ != "fixed";
    }

    std::pair<double, double> _DefaultSDFJointLimits(IN const std::string& strJointType_)
    {
        if (strJointType_ == "prismatic")
        {
            return { -1000.0, 1000.0 };
        }
        if (strJointType_ == "continuous")
        {
            return { -6.283185307179586, 6.283185307179586 };
        }
        return { -3.141592653589793, 3.141592653589793 };
    }

    SMachineJointState _BuildMachineJointStateFromSDF(IN const VariantArray& Joints_)
    {
        SMachineJointState _State;
        for (const auto& _JointValue : Joints_)
        {
            if (!_JointValue.Is<ObjectMap>())
            {
                continue;
            }

            const auto _Joint = _JointValue.To<ObjectMap>();
            const auto _Type = _GetObjectString(_Joint, "type");
            if (!_IsMovableSDFJoint(_Type))
            {
                continue;
            }

            auto _Name = _GetObjectString(_Joint, "name");
            if (_Name.empty())
            {
                _Name = "Joint" + std::to_string(_State.Names.size() + 1);
            }

            auto [_Lower, _Upper] = _DefaultSDFJointLimits(_Type);
            if (const auto _Axis = _TryGetObjectMapField(_Joint, "axis"))
            {
                if (const auto _Limit = _TryGetObjectMapField(*_Axis, "limit"))
                {
                    _Lower = _GetObjectDoubleOrDefault(*_Limit, "lower", _Lower);
                    _Upper = _GetObjectDoubleOrDefault(*_Limit, "upper", _Upper);
                }
            }
            if (_Upper < _Lower)
            {
                std::swap(_Lower, _Upper);
            }

            _State.Names.emplace_back(_Name);
            _State.Positions.emplace_back(0.0);
            _State.LowerLimits.emplace_back(_Lower);
            _State.UpperLimits.emplace_back(_Upper);
        }
        return _State;
    }

    void _SetMachineJointStateFromSDF(
        IN const std::shared_ptr<iCAX::CAM::CMachineComponent>& pMachine_,
        IN const VariantArray& Joints_)
    {
        const auto _State = _BuildMachineJointStateFromSDF(Joints_);
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_JointNames, _State.Names);
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_JointPositions, _State.Positions);
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_JointLowerLimits, _State.LowerLimits);
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_JointUpperLimits, _State.UpperLimits);
    }

    void _SetMachineDefaultJointState(IN const std::shared_ptr<iCAX::CAM::CMachineComponent>& pMachine_)
    {
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_JointNames, _MakeStringArray({ "X", "Y", "Z", "A", "C" }));
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_JointPositions, _MakeDoubleArray({ 0.0, 0.0, 0.0, 0.0, 0.0 }));
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_JointLowerLimits, _MakeDoubleArray({ -1000.0, -1000.0, -500.0, -180.0, -360.0 }));
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_JointUpperLimits, _MakeDoubleArray({ 1000.0, 1000.0, 500.0, 180.0, 360.0 }));
    }

    void _SetMachineDefaultPose(IN const std::shared_ptr<iCAX::CAM::CMachineComponent>& pMachine_)
    {
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_TCP, _MakeDoubleArray({ 0.0, 0.0, 0.0 }));
        _SetVariantArrayProperty(pMachine_, iCAX::CAM::CMachineComponent::PropertyName_BeamDirection, _MakeDoubleArray({ 0.0, 0.0, 1.0 }));
    }

    std::optional<ObjectMap> _TryObjectMap(IN const Variant& Value_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return std::nullopt;
        }
        return Value_.To<ObjectMap>();
    }

    std::vector<double> _ReadObjectDoubleArray(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN size_t nCount_,
        IN const std::vector<double>& Default_)
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<VariantArray>())
        {
            auto _Result = Default_;
            _Result.resize(nCount_, 0.0);
            return _Result;
        }

        auto _Values = _VariantArrayToDoubles(_Iter->second.To<VariantArray>(), strName_);
        auto _Result = Default_;
        _Result.resize(nCount_, 0.0);
        for (size_t _Index = 0; _Index < nCount_ && _Index < _Values.size(); ++_Index)
        {
            _Result[_Index] = _Values[_Index];
        }
        return _Result;
    }

    std::array<double, 6> _ReadPoseArray(IN const ObjectMap& Object_)
    {
        auto _Values = _ReadObjectDoubleArray(Object_, "pose", 6, { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 });
        return { _Values[0], _Values[1], _Values[2], _Values[3], _Values[4], _Values[5] };
    }

    std::unordered_map<std::string, std::array<double, 6>> _BuildSDFLinkPoseMap(IN const VariantArray& Links_)
    {
        std::unordered_map<std::string, std::array<double, 6>> _Result;
        for (const auto& _Value : Links_)
        {
            auto _Link = _TryObjectMap(_Value);
            if (!_Link)
            {
                continue;
            }

            const auto _Name = _GetObjectString(*_Link, "name");
            if (!_Name.empty())
            {
                _Result[_Name] = _ReadPoseArray(*_Link);
            }
        }
        return _Result;
    }

    std::array<double, 6> _CombineSDFPose(
        IN const std::unordered_map<std::string, std::array<double, 6>>& LinkPoses_,
        IN const ObjectMap& Visual_)
    {
        auto _Pose = _ReadPoseArray(Visual_);
        const auto _LinkName = _GetObjectString(Visual_, "linkName");
        auto _LinkIter = LinkPoses_.find(_LinkName);
        if (_LinkIter == LinkPoses_.end())
        {
            return _Pose;
        }

        const auto& _LinkPose = _LinkIter->second;
        for (size_t _Index = 0; _Index < _Pose.size(); ++_Index)
        {
            _Pose[_Index] += _LinkPose[_Index];
        }
        return _Pose;
    }

    void _AppendBoxMesh(
        IN OUT iCAX::Render::SRenderMeshData& Mesh_,
        IN double dSizeX_,
        IN double dSizeY_,
        IN double dSizeZ_)
    {
        const float _X = static_cast<float>(std::max(0.001, std::abs(dSizeX_)) * 0.5);
        const float _Y = static_cast<float>(std::max(0.001, std::abs(dSizeY_)) * 0.5);
        const float _Z = static_cast<float>(std::max(0.001, std::abs(dSizeZ_)) * 0.5);
        const uint32_t _Base = static_cast<uint32_t>(Mesh_.Positions.size());
        Mesh_.Positions.insert(Mesh_.Positions.end(), {
            { -_X, -_Y, -_Z }, { _X, -_Y, -_Z }, { _X, _Y, -_Z }, { -_X, _Y, -_Z },
            { -_X, -_Y, _Z }, { _X, -_Y, _Z }, { _X, _Y, _Z }, { -_X, _Y, _Z }
        });
        const uint32_t _Indices[] = {
            0, 1, 2, 0, 2, 3,
            4, 6, 5, 4, 7, 6,
            0, 4, 5, 0, 5, 1,
            1, 5, 6, 1, 6, 2,
            2, 6, 7, 2, 7, 3,
            3, 7, 4, 3, 4, 0
        };
        for (const auto _Index : _Indices)
        {
            Mesh_.Indices.push_back(_Base + _Index);
        }
    }

    void _AppendPlaneMesh(
        IN OUT iCAX::Render::SRenderMeshData& Mesh_,
        IN double dSizeX_,
        IN double dSizeY_)
    {
        const float _X = static_cast<float>(std::max(0.001, std::abs(dSizeX_)) * 0.5);
        const float _Y = static_cast<float>(std::max(0.001, std::abs(dSizeY_)) * 0.5);
        const uint32_t _Base = static_cast<uint32_t>(Mesh_.Positions.size());
        Mesh_.Positions.insert(Mesh_.Positions.end(), {
            { -_X, -_Y, 0.0f }, { _X, -_Y, 0.0f }, { _X, _Y, 0.0f }, { -_X, _Y, 0.0f }
        });
        Mesh_.Indices.insert(Mesh_.Indices.end(), { _Base, _Base + 1, _Base + 2, _Base, _Base + 2, _Base + 3 });
    }

    void _AppendCylinderMesh(
        IN OUT iCAX::Render::SRenderMeshData& Mesh_,
        IN double dRadius_,
        IN double dLength_)
    {
        constexpr uint32_t _Segments = 32;
        const float _Radius = static_cast<float>(std::max(0.001, std::abs(dRadius_)));
        const float _HalfLength = static_cast<float>(std::max(0.001, std::abs(dLength_)) * 0.5);
        const uint32_t _Base = static_cast<uint32_t>(Mesh_.Positions.size());
        for (uint32_t _Index = 0; _Index < _Segments; ++_Index)
        {
            const float _Angle = static_cast<float>((6.283185307179586 * _Index) / _Segments);
            Mesh_.Positions.push_back({ std::cos(_Angle) * _Radius, std::sin(_Angle) * _Radius, -_HalfLength });
            Mesh_.Positions.push_back({ std::cos(_Angle) * _Radius, std::sin(_Angle) * _Radius, _HalfLength });
        }

        const uint32_t _BottomCenter = static_cast<uint32_t>(Mesh_.Positions.size());
        Mesh_.Positions.push_back({ 0.0f, 0.0f, -_HalfLength });
        const uint32_t _TopCenter = static_cast<uint32_t>(Mesh_.Positions.size());
        Mesh_.Positions.push_back({ 0.0f, 0.0f, _HalfLength });

        for (uint32_t _Index = 0; _Index < _Segments; ++_Index)
        {
            const uint32_t _Next = (_Index + 1) % _Segments;
            const uint32_t _B0 = _Base + _Index * 2;
            const uint32_t _T0 = _B0 + 1;
            const uint32_t _B1 = _Base + _Next * 2;
            const uint32_t _T1 = _B1 + 1;
            Mesh_.Indices.insert(Mesh_.Indices.end(), { _B0, _B1, _T1, _B0, _T1, _T0 });
            Mesh_.Indices.insert(Mesh_.Indices.end(), { _BottomCenter, _B0, _B1 });
            Mesh_.Indices.insert(Mesh_.Indices.end(), { _TopCenter, _T1, _T0 });
        }
    }

    void _AppendSphereMesh(
        IN OUT iCAX::Render::SRenderMeshData& Mesh_,
        IN double dRadius_)
    {
        constexpr uint32_t _Latitudes = 12;
        constexpr uint32_t _Longitudes = 24;
        const float _Radius = static_cast<float>(std::max(0.001, std::abs(dRadius_)));
        const uint32_t _Base = static_cast<uint32_t>(Mesh_.Positions.size());
        for (uint32_t _Lat = 0; _Lat <= _Latitudes; ++_Lat)
        {
            const float _Theta = static_cast<float>((3.141592653589793 * _Lat) / _Latitudes);
            const float _SinTheta = std::sin(_Theta);
            const float _CosTheta = std::cos(_Theta);
            for (uint32_t _Lon = 0; _Lon <= _Longitudes; ++_Lon)
            {
                const float _Phi = static_cast<float>((6.283185307179586 * _Lon) / _Longitudes);
                Mesh_.Positions.push_back({
                    std::cos(_Phi) * _SinTheta * _Radius,
                    std::sin(_Phi) * _SinTheta * _Radius,
                    _CosTheta * _Radius
                });
            }
        }

        for (uint32_t _Lat = 0; _Lat < _Latitudes; ++_Lat)
        {
            for (uint32_t _Lon = 0; _Lon < _Longitudes; ++_Lon)
            {
                const uint32_t _A = _Base + _Lat * (_Longitudes + 1) + _Lon;
                const uint32_t _B = _A + _Longitudes + 1;
                Mesh_.Indices.insert(Mesh_.Indices.end(), { _A, _B, _A + 1, _B, _B + 1, _A + 1 });
            }
        }
    }

    iCAX::Render::SRenderMeshData _MakeMachineVisualMesh(IN const ObjectMap& Visual_)
    {
        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.eTopology = iCAX::Render::ERenderTopology::TriangleList;

        const auto _Geometry = _TryGetObjectMapField(Visual_, "geometry");
        const auto _Type = _Geometry ? _GetObjectString(*_Geometry, "type") : std::string();
        if (_Type == "box")
        {
            const auto _Size = _ReadObjectDoubleArray(*_Geometry, "size", 3, { 1.0, 1.0, 1.0 });
            _AppendBoxMesh(_Mesh, _Size[0] * kSDFLengthToWorld, _Size[1] * kSDFLengthToWorld, _Size[2] * kSDFLengthToWorld);
        }
        else if (_Type == "cylinder")
        {
            _AppendCylinderMesh(
                _Mesh,
                _GetObjectDoubleOrDefault(*_Geometry, "radius", 0.5) * kSDFLengthToWorld,
                _GetObjectDoubleOrDefault(*_Geometry, "length", 1.0) * kSDFLengthToWorld);
        }
        else if (_Type == "sphere")
        {
            _AppendSphereMesh(_Mesh, _GetObjectDoubleOrDefault(*_Geometry, "radius", 0.5) * kSDFLengthToWorld);
        }
        else if (_Type == "plane")
        {
            const auto _Size = _ReadObjectDoubleArray(*_Geometry, "size", 2, { 1.0, 1.0 });
            _AppendPlaneMesh(_Mesh, _Size[0] * kSDFLengthToWorld, _Size[1] * kSDFLengthToWorld);
        }
        else if (_Type == "mesh")
        {
            const auto _Scale = _ReadObjectDoubleArray(*_Geometry, "scale", 3, { 1.0, 1.0, 1.0 });
            _AppendBoxMesh(_Mesh, 0.35 * kSDFLengthToWorld * _Scale[0], 0.22 * kSDFLengthToWorld * _Scale[1], 0.16 * kSDFLengthToWorld * _Scale[2]);
        }
        else
        {
            _AppendBoxMesh(_Mesh, 0.25 * kSDFLengthToWorld, 0.25 * kSDFLengthToWorld, 0.25 * kSDFLengthToWorld);
        }
        return _Mesh;
    }

    uint32_t _MakeRGBA(
        IN double dRed_,
        IN double dGreen_,
        IN double dBlue_,
        IN double dAlpha_) noexcept
    {
        auto _ToByte = [](double dValue_) -> uint32_t {
            return static_cast<uint32_t>(std::clamp(dValue_, 0.0, 1.0) * 255.0 + 0.5);
        };
        return (_ToByte(dRed_) << 24) | (_ToByte(dGreen_) << 16) | (_ToByte(dBlue_) << 8) | _ToByte(dAlpha_);
    }

    uint32_t _ReadVisualColor(IN const ObjectMap& Visual_) noexcept
    {
        try
        {
            if (const auto _Material = _TryGetObjectMapField(Visual_, "material"))
            {
                const auto _Diffuse = _ReadObjectDoubleArray(*_Material, "diffuse", 4, { 0.65, 0.70, 0.72, 1.0 });
                const auto _Transparency = _GetObjectDoubleOrDefault(Visual_, "transparency", 0.0);
                return _MakeRGBA(_Diffuse[0], _Diffuse[1], _Diffuse[2], _Diffuse[3] * (1.0 - std::clamp(_Transparency, 0.0, 1.0)));
            }
        }
        catch (const std::exception&)
        {
        }
        return 0xB8C0C4FFu;
    }

    uint64_t _NextMachineRenderResourceVersion() noexcept
    {
        static std::atomic_uint64_t _Version{ 1 };
        return _Version.fetch_add(1, std::memory_order_relaxed);
    }

    std::string _MakeMachinePartResourceID(
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::string& strSourceKind_,
        IN size_t nIndex_)
    {
        return "machine/" + iCAX::Data::to_string(MachineID_) + "/" + strSourceKind_ + "/" + std::to_string(nIndex_) + "#render.mesh";
    }

    void _DeleteMachinePartEntities(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        auto _Entities = Repository_.FilterEntities([&MachineID_](std::shared_ptr<iCAX::Database::IEntity> pEntity_) {
            auto _pPart = _GetComponent<iCAX::CAM::CMachinePartComponent>(pEntity_);
            return _pPart && _pPart->GetMachineID() == MachineID_;
        });

        for (const auto& _pEntity : _Entities)
        {
            std::string _strError;
            if (!_pEntity || !Repository_.DeleteEntity(_pEntity->GetID(), _strError))
            {
                throw std::runtime_error(_strError.empty() ? "Delete CAM machine part entity failed" : _strError);
            }
        }
    }

    void _CreateMachinePartEntity(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::string& strMachineName_,
        IN const std::unordered_map<std::string, std::array<double, 6>>& LinkPoses_,
        IN const ObjectMap& Visual_,
        IN const std::string& strSourceKind_,
        IN size_t nIndex_)
    {
        auto& _Repository = Scene_.Database();
        auto [_pPartEntity, _pPart] = _CreateEntityWithComponent<iCAX::CAM::CMachinePartComponent>(_Repository);
        auto _pTransform = _GetOrAddEntityComponent<iCAX::RenderInteraction::CRenderTransformComponent>(_pPartEntity);
        auto _pRender = _GetOrAddEntityComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(_pPartEntity);

        const auto _LinkName = _GetObjectString(Visual_, "linkName");
        auto _PartName = _GetObjectString(Visual_, "name");
        if (_PartName.empty())
        {
            _PartName = strSourceKind_ + std::to_string(nIndex_ + 1);
        }

        const auto _ResourceID = _MakeMachinePartResourceID(MachineID_, strSourceKind_, nIndex_);
        auto _Mesh = std::make_shared<iCAX::Render::SRenderMeshData>(_MakeMachineVisualMesh(Visual_));
        _Mesh->nDataVersion = _NextMachineRenderResourceVersion();

        iCAX::Resource::CResourceInfo _Info;
        _Info.Source = _ResourceID;
        _Info.Name = (strMachineName_.empty() ? std::string("Machine") : strMachineName_) + "/" + _LinkName + "/" + _PartName;
        _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        _Info.nVersion = _Mesh->nDataVersion;
        _Info.Metadata["kind"] = "render.mesh";
        _Info.Metadata["source"] = "sdf." + strSourceKind_;
        Scene_.Resources().Set<iCAX::Render::SRenderMeshData>(_ResourceID, _Mesh, _Info);

        _SetUuidProperty(_pPart, iCAX::CAM::CMachinePartComponent::PropertyName_MachineID, MachineID_);
        _SetStringProperty(_pPart, iCAX::CAM::CMachinePartComponent::PropertyName_LinkName, _LinkName);
        _SetStringProperty(_pPart, iCAX::CAM::CMachinePartComponent::PropertyName_PartName, _PartName);
        _SetStringProperty(_pPart, iCAX::CAM::CMachinePartComponent::PropertyName_SourceKind, strSourceKind_);
        _SetStringProperty(_pPart, iCAX::CAM::CMachinePartComponent::PropertyName_GeometryResourceID, _ResourceID);

        const auto _Pose = _CombineSDFPose(LinkPoses_, Visual_);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionX, _Pose[0] * kSDFLengthToWorld);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionY, _Pose[1] * kSDFLengthToWorld);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionZ, _Pose[2] * kSDFLengthToWorld);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_RollRadians, _Pose[3]);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PitchRadians, _Pose[4]);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_YawRadians, _Pose[5]);

        _SetStringProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID, _ResourceID);
        _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryKind, 1ull);
        _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_RenderClass, 4ull);
        _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_StyleID, static_cast<unsigned long long>(1000 + nIndex_));
        _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_ColorRGBA, static_cast<unsigned long long>(_ReadVisualColor(Visual_)));
        _SetDoubleProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_LineWidth, 1.0);
    }

    void _CreateMachinePartEntitiesFromSDF(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::string& strMachineName_,
        IN const iCAX::CAM::SSDFMachineDocument& SDF_)
    {
        _DeleteMachinePartEntities(Scene_.Database(), MachineID_);

        const auto _LinkPoses = _BuildSDFLinkPoseMap(SDF_.Links);
        const auto& _Source = !SDF_.Visuals.empty() ? SDF_.Visuals : SDF_.Collisions;
        const auto _SourceKind = !SDF_.Visuals.empty() ? std::string("visual") : std::string("collision");
        for (size_t _Index = 0; _Index < _Source.size(); ++_Index)
        {
            auto _Visual = _TryObjectMap(_Source[_Index]);
            if (!_Visual)
            {
                continue;
            }
            _CreateMachinePartEntity(Scene_, MachineID_, strMachineName_, _LinkPoses, *_Visual, _SourceKind, _Index);
        }
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineComponent>> _EnsureMachineEntity(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto _Existing = _GetActiveOrFirstMachine(Repository_);
        if (_Existing.first && _Existing.second)
        {
            return _Existing;
        }

        auto [_pEntity, _pMachine] = _CreateEntityWithComponent<iCAX::CAM::CMachineComponent>(Repository_);
        auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(Repository_);
        _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveMachineID, _pEntity->GetID());
        _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_WorkstationName, "默认工位");
        _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_DescriptionFormat, "SDF");
        _SetMachineDefaultJointState(_pMachine);
        _SetMachineDefaultPose(_pMachine);
        return { _pEntity, _pMachine };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineComponent>> _RequireActiveMachine(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto _Machine = _GetActiveOrFirstMachine(Repository_);
        if (!_Machine.first || !_Machine.second || _Machine.second->GetSourcePath().empty())
        {
            throw std::invalid_argument("Cam active machine is not available");
        }
        return _Machine;
    }

    std::string _CheckMachineLimitResult(IN const std::shared_ptr<iCAX::CAM::CMachineComponent>& pMachine_)
    {
        const auto _Positions = _VariantArrayToDoubles(pMachine_->GetJointPositions(), "jointPositions");
        const auto _LowerLimits = _VariantArrayToDoubles(pMachine_->GetJointLowerLimits(), "jointLowerLimits");
        const auto _UpperLimits = _VariantArrayToDoubles(pMachine_->GetJointUpperLimits(), "jointUpperLimits");
        const auto& _JointNames = pMachine_->GetJointNames();
        if (_Positions.size() != _LowerLimits.size() || _Positions.size() != _UpperLimits.size())
        {
            return "轴数据不完整";
        }

        std::vector<std::string> _Violations;
        for (size_t _Index = 0; _Index < _Positions.size(); ++_Index)
        {
            const auto _Name = _Index < _JointNames.size() && _JointNames[_Index].Is<std::string>()
                ? _JointNames[_Index].To<std::string>()
                : std::string("J") + std::to_string(_Index + 1);
            if (_Positions[_Index] < _LowerLimits[_Index] || _Positions[_Index] > _UpperLimits[_Index])
            {
                _Violations.push_back(_Name);
            }
        }

        if (_Violations.empty())
        {
            return "轴限位检查通过";
        }

        std::string _Result = "轴越界：";
        for (size_t _Index = 0; _Index < _Violations.size(); ++_Index)
        {
            if (_Index > 0)
            {
                _Result += ", ";
            }
            _Result += _Violations[_Index];
        }
        return _Result;
    }

}
