#include "pch.h"
#include "MachineTransformConstraints.h"

#include "MachineInstanceComponents.h"

#include "Data/Variant.h"
#include "Database/IChecker.h"
#include "Database/IFieldPolicyProvider.h"
#include "Database/IMetaRegistry.h"
#include "Database/IRepository.h"
#include "Database/MetaRegistrationCatalog.h"
#include "Transform/TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>

namespace
{
    constexpr double kEpsilon = 1e-7;
    constexpr double kRadiansToDegrees = 57.2957795130823208768;

    using iCAX::Data::ObjectMap;
    using iCAX::Data::PropertySet;
    using iCAX::Data::PropertyValue;
    using iCAX::Data::VariantArray;
    using iCAX::Database::SFieldEditPolicy;

    enum class EAxisKind
    {
        X = 0,
        Y = 1,
        Z = 2,
        None = 3
    };

    struct SAxisInfo final
    {
        EAxisKind eKind = EAxisKind::None;
        double dSign = 1.0;
        bool bAxisAligned = false;
    };

    struct STransformEditPolicy final
    {
        std::array<SFieldEditPolicy, 3> Position;
        std::array<SFieldEditPolicy, 3> RotationRadians;
        std::array<SFieldEditPolicy, 3> Scale;
        std::string JointType;
        std::string Reason;
    };

    bool IsFinite(IN const double dValue_) noexcept
    {
        return std::isfinite(dValue_);
    }

    bool ReadDouble(IN const PropertyValue& Value_, OUT double& dValue_) noexcept
    {
        try
        {
            if (Value_.Is<float>())
            {
                dValue_ = static_cast<double>(Value_.To<float>());
                return IsFinite(dValue_);
            }
            if (Value_.Is<double>())
            {
                dValue_ = Value_.To<double>();
                return IsFinite(dValue_);
            }
            if (Value_.Is<int>())
            {
                dValue_ = static_cast<double>(Value_.To<int>());
                return true;
            }
            if (Value_.Is<unsigned int>())
            {
                dValue_ = static_cast<double>(Value_.To<unsigned int>());
                return true;
            }
            if (Value_.Is<long long>())
            {
                dValue_ = static_cast<double>(Value_.To<long long>());
                return true;
            }
            if (Value_.Is<unsigned long long>())
            {
                dValue_ = static_cast<double>(Value_.To<unsigned long long>());
                return true;
            }
        }
        catch (...)
        {
            return false;
        }
        return false;
    }

    bool TryReadAxis(IN const VariantArray& Axis_, OUT std::array<double, 3>& Values_) noexcept
    {
        if (Axis_.size() < 3)
        {
            return false;
        }

        for (size_t _Index = 0; _Index < 3; ++_Index)
        {
            if (!ReadDouble(Axis_[_Index], Values_[_Index]))
            {
                return false;
            }
        }
        return true;
    }

    SAxisInfo ResolveAxis(IN const VariantArray& Axis_) noexcept
    {
        std::array<double, 3> _Values{};
        if (!TryReadAxis(Axis_, _Values))
        {
            _Values = { 0.0, 0.0, 1.0 };
        }

        const auto _Length = std::sqrt(
            _Values[0] * _Values[0]
            + _Values[1] * _Values[1]
            + _Values[2] * _Values[2]);
        if (_Length <= kEpsilon)
        {
            return {};
        }

        for (auto& _Value : _Values)
        {
            _Value /= _Length;
        }

        size_t _Dominant = 0;
        for (size_t _Index = 1; _Index < _Values.size(); ++_Index)
        {
            if (std::abs(_Values[_Index]) > std::abs(_Values[_Dominant]))
            {
                _Dominant = _Index;
            }
        }

        SAxisInfo _Axis;
        _Axis.eKind = static_cast<EAxisKind>(_Dominant);
        _Axis.dSign = _Values[_Dominant] < 0.0 ? -1.0 : 1.0;
        _Axis.bAxisAligned = std::abs(std::abs(_Values[_Dominant]) - 1.0) <= 1e-5;
        for (size_t _Index = 0; _Index < _Values.size(); ++_Index)
        {
            if (_Index != _Dominant && std::abs(_Values[_Index]) > 1e-5)
            {
                _Axis.bAxisAligned = false;
            }
        }
        return _Axis;
    }

    int PositionIndex(IN const std::string& strPropertyName_) noexcept
    {
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_PositionX) return 0;
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_PositionY) return 1;
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_PositionZ) return 2;
        return -1;
    }

    int RotationIndex(IN const std::string& strPropertyName_) noexcept
    {
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_YawRadians) return 0;
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_PitchRadians) return 1;
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_RollRadians) return 2;
        return -1;
    }

    int ScaleIndex(IN const std::string& strPropertyName_) noexcept
    {
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_ScaleX) return 0;
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_ScaleY) return 1;
        if (strPropertyName_ == iCAX::Transform::CTransformComponent::PropertyName_ScaleZ) return 2;
        return -1;
    }

    std::shared_ptr<iCAX::CAM::CMachineJointComponent> GetMachineJoint(
        IN const iCAX::Database::CComponentBase& Component_)
    {
        auto _pEntity = Component_.GetEntity();
        return _pEntity ? _pEntity->GetComponent<iCAX::CAM::CMachineJointComponent>() : nullptr;
    }

    bool IsMachineTransform(IN const iCAX::Database::CComponentBase& Component_)
    {
        auto _pEntity = Component_.GetEntity();
        return _pEntity
            && (_pEntity->HasComponent<iCAX::CAM::CMachineInstanceComponent>()
                || _pEntity->HasComponent<iCAX::CAM::CMachineElementComponent>());
    }

    std::shared_ptr<iCAX::CAM::CMachineKinematicsComponent> FindMachineKinematics(
        IN const iCAX::Database::IEntity& Entity_)
    {
        if (auto _pKinematics = Entity_.GetComponent<iCAX::CAM::CMachineKinematicsComponent>())
        {
            return _pKinematics;
        }

        const auto _pElement = Entity_.GetComponent<iCAX::CAM::CMachineElementComponent>();
        const auto _pRepository = Entity_.GetRepository();
        if (!_pElement || !_pRepository || _pElement->GetMachineID().is_nil())
        {
            return nullptr;
        }

        auto _pMachineEntity = _pRepository->GetEntity(_pElement->GetMachineID());
        return _pMachineEntity ? _pMachineEntity->GetComponent<iCAX::CAM::CMachineKinematicsComponent>() : nullptr;
    }

    double GetLinearJogStep(IN const iCAX::Database::IEntity& Entity_)
    {
        const auto _pKinematics = FindMachineKinematics(Entity_);
        if (_pKinematics && IsFinite(_pKinematics->GetLinearJogStep()) && _pKinematics->GetLinearJogStep() > 0.0)
        {
            return _pKinematics->GetLinearJogStep();
        }
        return 10.0;
    }

    double GetAngularJogStepRadians(IN const iCAX::Database::IEntity& Entity_)
    {
        const auto _pKinematics = FindMachineKinematics(Entity_);
        if (_pKinematics && IsFinite(_pKinematics->GetAngularJogStep()) && _pKinematics->GetAngularJogStep() > 0.0)
        {
            return _pKinematics->GetAngularJogStep() / kRadiansToDegrees;
        }
        return 1.0 / kRadiansToDegrees;
    }

    std::pair<double, double> SignedRange(IN const double dLower_, IN const double dUpper_, IN const double dSign_) noexcept
    {
        auto _Min = dLower_ * dSign_;
        auto _Max = dUpper_ * dSign_;
        if (_Max < _Min)
        {
            std::swap(_Min, _Max);
        }
        return { _Min, _Max };
    }

    SFieldEditPolicy MakeEditablePolicy(
        IN double dMin_,
        IN double dMax_,
        IN double dStep_,
        IN const std::string& strReason_,
        IN const std::string& strUnit_,
        IN int nPrecision_)
    {
        SFieldEditPolicy _Policy;
        _Policy.bVisible = true;
        _Policy.bEditable = true;
        _Policy.bHasRange = true;
        _Policy.dMin = dMin_;
        _Policy.dMax = dMax_;
        _Policy.dStep = dStep_;
        _Policy.nPrecision = nPrecision_;
        _Policy.strReason = strReason_;
        _Policy.strUnit = strUnit_;
        _Policy.strControlKind = "number";
        return _Policy;
    }

    SFieldEditPolicy MakeDisabledPolicy(
        IN double dStep_,
        IN const std::string& strReason_,
        IN const std::string& strUnit_,
        IN int nPrecision_)
    {
        SFieldEditPolicy _Policy;
        _Policy.bVisible = true;
        _Policy.bEditable = false;
        _Policy.dStep = dStep_;
        _Policy.nPrecision = nPrecision_;
        _Policy.strReason = strReason_;
        _Policy.strUnit = strUnit_;
        _Policy.strControlKind = "number";
        return _Policy;
    }

    size_t RotationPolicyIndexFromAxis(IN const EAxisKind eAxis_) noexcept
    {
        switch (eAxis_)
        {
        case EAxisKind::X:
            return 2; // Roll
        case EAxisKind::Y:
            return 1; // Pitch
        case EAxisKind::Z:
            return 0; // Yaw
        default:
            return 0;
        }
    }

    STransformEditPolicy BuildTransformEditPolicy(
        IN const iCAX::Database::IEntity& Entity_)
    {
        const auto _LinearJogStep = GetLinearJogStep(Entity_);
        const auto _AngularJogStepRadians = GetAngularJogStepRadians(Entity_);
        STransformEditPolicy _Policy;
        for (auto& _Field : _Policy.Position)
        {
            _Field = MakeEditablePolicy(-1.0e9, 1.0e9, _LinearJogStep, "普通机床元素允许编辑本地位置", "mm", 3);
            _Field.bHasRange = false;
        }
        for (auto& _Field : _Policy.RotationRadians)
        {
            _Field = MakeEditablePolicy(-6.2831853071795864769, 6.2831853071795864769, _AngularJogStepRadians, "普通机床元素允许编辑本地姿态", "rad", 6);
            _Field.bHasRange = false;
        }
        for (auto& _Field : _Policy.Scale)
        {
            _Field = MakeDisabledPolicy(0.001, "机床元素不允许从属性面板缩放", "", 3);
        }

        if (Entity_.HasComponent<iCAX::CAM::CMachineInstanceComponent>())
        {
            _Policy.JointType = "machine";
            _Policy.Reason = "机床实例根允许调整整体摆放位置和姿态";
            return _Policy;
        }

        const auto _pJoint = Entity_.GetComponent<iCAX::CAM::CMachineJointComponent>();
        if (!_pJoint)
        {
            _Policy.JointType = "none";
            _Policy.Reason = "机床部件位姿由机床定义和父子结构决定，不能直接编辑";
            for (auto& _Field : _Policy.Position)
            {
                _Field = MakeDisabledPolicy(_LinearJogStep, _Policy.Reason, "mm", 3);
            }
            for (auto& _Field : _Policy.RotationRadians)
            {
                _Field = MakeDisabledPolicy(_AngularJogStepRadians, _Policy.Reason, "rad", 6);
            }
            return _Policy;
        }

        const auto _JointType = _pJoint->GetJointType();
        _Policy.JointType = _JointType;
        for (auto& _Field : _Policy.Position)
        {
            _Field = MakeDisabledPolicy(_LinearJogStep, "运动副约束禁用该平移自由度", "mm", 3);
        }
        for (auto& _Field : _Policy.RotationRadians)
        {
            _Field = MakeDisabledPolicy(_AngularJogStepRadians, "运动副约束禁用该旋转自由度", "rad", 6);
        }

        if (_JointType == "fixed" || _JointType.empty())
        {
            _Policy.Reason = "固定轴不允许编辑本地位移或姿态";
            return _Policy;
        }

        const auto _Axis = ResolveAxis(_pJoint->GetAxis());
        if (!_Axis.bAxisAligned || _Axis.eKind == EAxisKind::None)
        {
            _Policy.Reason = "当前属性面板只开放坐标轴对齐的运动副";
            return _Policy;
        }

        if (_JointType == "prismatic")
        {
            const auto _Index = static_cast<size_t>(_Axis.eKind);
            const auto [_Min, _Max] = SignedRange(_pJoint->GetLowerLimit(), _pJoint->GetUpperLimit(), _Axis.dSign);
            _Policy.Position[_Index] = MakeEditablePolicy(_Min, _Max, _LinearJogStep, "平移轴只允许沿轴向编辑，单位 mm", "mm", 3);
            _Policy.Reason = "平移轴只开放轴向位移";
            return _Policy;
        }

        if (_JointType == "revolute" || _JointType == "continuous")
        {
            const auto _Index = RotationPolicyIndexFromAxis(_Axis.eKind);
            const auto [_Min, _Max] = SignedRange(_pJoint->GetLowerLimit(), _pJoint->GetUpperLimit(), _Axis.dSign);
            _Policy.RotationRadians[_Index] = MakeEditablePolicy(
                _Min,
                _Max,
                _AngularJogStepRadians,
                "旋转轴只允许沿轴向编辑，单位弧度",
                "rad",
                6);
            _Policy.Reason = "旋转轴只开放轴向角度";
            return _Policy;
        }

        _Policy.Reason = "未知运动副类型不开放 Transform 直接编辑";
        return _Policy;
    }

    ObjectMap MakeFieldPolicyPayload(IN const SFieldEditPolicy& Policy_)
    {
        ObjectMap _Payload;
        _Payload["visible"] = Policy_.bVisible;
        _Payload["editable"] = Policy_.bEditable;
        _Payload["hasRange"] = Policy_.bHasRange;
        _Payload["min"] = Policy_.dMin;
        _Payload["max"] = Policy_.dMax;
        _Payload["step"] = Policy_.dStep;
        _Payload["precision"] = Policy_.nPrecision;
        _Payload["unit"] = Policy_.strUnit;
        _Payload["reason"] = Policy_.strReason;
        _Payload["controlKind"] = Policy_.strControlKind;
        if (!Policy_.Options.empty())
        {
            _Payload["options"] = Policy_.Options;
        }
        if (!Policy_.Metadata.empty())
        {
            _Payload["metadata"] = Policy_.Metadata;
        }
        return _Payload;
    }

    VariantArray MakeFieldPolicyArray(IN const std::array<SFieldEditPolicy, 3>& Policies_)
    {
        VariantArray _Array;
        _Array.reserve(Policies_.size());
        for (const auto& _Policy : Policies_)
        {
            _Array.emplace_back(MakeFieldPolicyPayload(_Policy));
        }
        return _Array;
    }

    bool ValidateValueAgainstPolicy(
        IN const SFieldEditPolicy& Policy_,
        IN double dValue_,
        OUT std::string& strError_)
    {
        if (!Policy_.bEditable)
        {
            strError_ = Policy_.strReason.empty() ? "Machine transform field is not editable" : Policy_.strReason;
            return false;
        }
        if (Policy_.bHasRange && (dValue_ < Policy_.dMin - kEpsilon || dValue_ > Policy_.dMax + kEpsilon))
        {
            strError_ = "Machine transform field exceeds joint limit";
            return false;
        }
        return true;
    }

    bool ValidateTransformProperties(
        IN const iCAX::Transform::CTransformComponent& Transform_,
        IN const PropertySet& Properties_,
        OUT std::string& strError_)
    {
        if (!IsMachineTransform(Transform_))
        {
            return true;
        }

        auto _pEntity = Transform_.GetEntity();
        if (!_pEntity)
        {
            return true;
        }
        const auto _Policy = BuildTransformEditPolicy(*_pEntity);

        for (const auto& [_PropertyName, _Value] : Properties_)
        {
            if (ScaleIndex(_PropertyName) >= 0)
            {
                strError_ = "Machine transform scale is not editable";
                return false;
            }

            if (const auto _PositionIndex = PositionIndex(_PropertyName); _PositionIndex >= 0)
            {
                double _NewValue = 0.0;
                if (!ReadDouble(_Value, _NewValue))
                {
                    strError_ = "Machine transform field must be numeric: " + _PropertyName;
                    return false;
                }
                if (!ValidateValueAgainstPolicy(_Policy.Position[static_cast<size_t>(_PositionIndex)], _NewValue, strError_))
                {
                    return false;
                }
                continue;
            }

            if (const auto _RotationIndex = RotationIndex(_PropertyName); _RotationIndex >= 0)
            {
                double _NewValue = 0.0;
                if (!ReadDouble(_Value, _NewValue))
                {
                    strError_ = "Machine transform field must be numeric: " + _PropertyName;
                    return false;
                }
                if (!ValidateValueAgainstPolicy(
                    _Policy.RotationRadians[static_cast<size_t>(_RotationIndex)],
                    _NewValue,
                    strError_))
                {
                    return false;
                }
                continue;
            }
        }
        return true;
    }

    bool ValidateJointProperties(
        IN const iCAX::CAM::CMachineJointComponent& Joint_,
        IN const PropertySet& Properties_,
        OUT std::string& strError_)
    {
        auto _Lower = Joint_.GetLowerLimit();
        auto _Upper = Joint_.GetUpperLimit();
        if (auto _Iter = Properties_.find(iCAX::CAM::CMachineJointComponent::PropertyName_LowerLimit); _Iter != Properties_.end())
        {
            if (!ReadDouble(_Iter->second, _Lower))
            {
                strError_ = "Machine joint lower limit must be numeric";
                return false;
            }
        }
        if (auto _Iter = Properties_.find(iCAX::CAM::CMachineJointComponent::PropertyName_UpperLimit); _Iter != Properties_.end())
        {
            if (!ReadDouble(_Iter->second, _Upper))
            {
                strError_ = "Machine joint upper limit must be numeric";
                return false;
            }
        }
        if (_Upper < _Lower)
        {
            strError_ = "Machine joint upper limit must not be less than lower limit";
            return false;
        }

        auto _Position = Joint_.GetPosition();
        if (auto _Iter = Properties_.find(iCAX::CAM::CMachineJointComponent::PropertyName_Position); _Iter != Properties_.end())
        {
            if (!ReadDouble(_Iter->second, _Position))
            {
                strError_ = "Machine joint position must be numeric";
                return false;
            }
        }
        if (_Position < _Lower - kEpsilon || _Position > _Upper + kEpsilon)
        {
            strError_ = "Machine joint position exceeds joint limit";
            return false;
        }
        return true;
    }

    class CMachineTransformChecker final : public iCAX::Database::IChecker
    {
    public:
        bool AllowAttachByName(const iCAX::Database::IEntity&, const std::string&, std::string&) override
        {
            return true;
        }

        bool AllowRemoveByName(const iCAX::Database::IEntity&, const std::string&, std::string&) override
        {
            return true;
        }

        bool AllowModify(const iCAX::Database::CComponentBase& Component_, const PropertySet& Properties_, std::string& strError_) override
        {
            const auto* _pTransform = dynamic_cast<const iCAX::Transform::CTransformComponent*>(&Component_);
            return !_pTransform || ValidateTransformProperties(*_pTransform, Properties_, strError_);
        }
    };

    class CMachineJointChecker final : public iCAX::Database::IChecker
    {
    public:
        bool AllowAttachByName(const iCAX::Database::IEntity&, const std::string&, std::string&) override
        {
            return true;
        }

        bool AllowRemoveByName(const iCAX::Database::IEntity&, const std::string&, std::string&) override
        {
            return true;
        }

        bool AllowModify(const iCAX::Database::CComponentBase& Component_, const PropertySet& Properties_, std::string& strError_) override
        {
            const auto* _pJoint = dynamic_cast<const iCAX::CAM::CMachineJointComponent*>(&Component_);
            return !_pJoint || ValidateJointProperties(*_pJoint, Properties_, strError_);
        }
    };

    class CMachineTransformFieldPolicyProvider final : public iCAX::Database::IFieldPolicyProvider
    {
    public:
        bool TryGetFieldPolicy(
            IN const iCAX::Database::IEntity& Entity_,
            IN const iCAX::Database::CComponentBase& Component_,
            IN const std::string& strPropertyName_,
            OUT SFieldEditPolicy& Policy_) const override
        {
            const auto* _pTransform = dynamic_cast<const iCAX::Transform::CTransformComponent*>(&Component_);
            if (!_pTransform || !IsMachineTransform(Component_))
            {
                return false;
            }

            const auto _Policy = BuildTransformEditPolicy(Entity_);
            if (const auto _PositionIndex = PositionIndex(strPropertyName_); _PositionIndex >= 0)
            {
                Policy_ = _Policy.Position[static_cast<size_t>(_PositionIndex)];
                return true;
            }

            if (const auto _RotationIndex = RotationIndex(strPropertyName_); _RotationIndex >= 0)
            {
                Policy_ = _Policy.RotationRadians[static_cast<size_t>(_RotationIndex)];
                return true;
            }

            if (const auto _ScaleIndex = ScaleIndex(strPropertyName_); _ScaleIndex >= 0)
            {
                Policy_ = _Policy.Scale[static_cast<size_t>(_ScaleIndex)];
                return true;
            }

            return false;
        }
    };

    struct CMachineConstraintRegistration final
    {
        CMachineConstraintRegistration()
        {
            iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& Registry_) {
                Registry_.RegistFieldPolicyProviderByName(
                    std::make_shared<CMachineTransformFieldPolicyProvider>(),
                    iCAX::Transform::CTransformComponent::S_ClassName);
                Registry_.RegistCheckerByName(
                    std::make_shared<CMachineTransformChecker>(),
                    iCAX::Transform::CTransformComponent::S_ClassName);
                Registry_.RegistCheckerByName(
                    std::make_shared<CMachineJointChecker>(),
                    iCAX::CAM::CMachineJointComponent::S_ClassName);
            }, this);
        }
    };

    CMachineConstraintRegistration g_MachineConstraintRegistration;
}

iCAX::Data::ObjectMap iCAX::CAM::MakeMachineTransformEditPolicyPayload(
    IN const iCAX::Database::IEntity& Entity_)
{
    const auto _Policy = BuildTransformEditPolicy(Entity_);
    auto _Position = _Policy.Position;
    auto _RotationRadians = _Policy.RotationRadians;
    auto _Scale = _Policy.Scale;

    const auto _pTransform = Entity_.GetComponent<iCAX::Transform::CTransformComponent>();
    const auto _pRepository = Entity_.GetRepository();
    const auto _pMetaRegistry = _pRepository ? _pRepository->GetMetaRegistry() : nullptr;
    if (_pTransform && _pMetaRegistry)
    {
        const std::array<std::string, 3> _PositionProperties = {
            iCAX::Transform::CTransformComponent::PropertyName_PositionX,
            iCAX::Transform::CTransformComponent::PropertyName_PositionY,
            iCAX::Transform::CTransformComponent::PropertyName_PositionZ
        };
        const std::array<std::string, 3> _RotationProperties = {
            iCAX::Transform::CTransformComponent::PropertyName_YawRadians,
            iCAX::Transform::CTransformComponent::PropertyName_PitchRadians,
            iCAX::Transform::CTransformComponent::PropertyName_RollRadians
        };
        const std::array<std::string, 3> _ScaleProperties = {
            iCAX::Transform::CTransformComponent::PropertyName_ScaleX,
            iCAX::Transform::CTransformComponent::PropertyName_ScaleY,
            iCAX::Transform::CTransformComponent::PropertyName_ScaleZ
        };

        for (size_t _Index = 0; _Index < 3; ++_Index)
        {
            (void)_pMetaRegistry->TryGetFieldPolicy(Entity_, *_pTransform, _PositionProperties[_Index], _Position[_Index]);
            (void)_pMetaRegistry->TryGetFieldPolicy(Entity_, *_pTransform, _RotationProperties[_Index], _RotationRadians[_Index]);
            (void)_pMetaRegistry->TryGetFieldPolicy(Entity_, *_pTransform, _ScaleProperties[_Index], _Scale[_Index]);
        }
    }

    ObjectMap _Payload;
    _Payload["jointType"] = _Policy.JointType;
    _Payload["reason"] = _Policy.Reason;
    _Payload["position"] = MakeFieldPolicyArray(_Position);
    _Payload["rotationRadians"] = MakeFieldPolicyArray(_RotationRadians);
    _Payload["scale"] = MakeFieldPolicyArray(_Scale);
    return _Payload;
}
