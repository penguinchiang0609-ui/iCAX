#pragma once

#include "TransformExport.h"

#include "Database/ComponentHelper.h"

#include <array>
#include <cmath>

namespace iCAX
{
    namespace Transform
    {
        inline iCAX::Data::PropertyValue ToDoubleVariant(IN const double& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline double FromDoubleVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            if (Value_.Is<float>())
            {
                return static_cast<double>(Value_.To<float>());
            }
            return Value_.To<double>();
        }

        inline bool DoubleEqual(IN const double& Left_, IN const double& Right_)
        {
            return std::abs(Left_ - Right_) <= 0.0000001;
        }

        inline iCAX::Data::PropertyValue ToUuidVariant(IN const iCAX::Data::uuid& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::uuid FromUuidVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            if (Value_.Is<iCAX::Data::uuid>())
            {
                return Value_.To<iCAX::Data::uuid>();
            }
            if (Value_.Is<std::string>())
            {
                auto _Parsed = iCAX::Data::uuid::from_string(Value_.To<std::string>());
                if (_Parsed.has_value())
                {
                    return *_Parsed;
                }
            }
            return iCAX::Data::uuid();
        }

        inline bool UuidEqual(IN const iCAX::Data::uuid& Left_, IN const iCAX::Data::uuid& Right_)
        {
            return Left_ == Right_;
        }

        inline iCAX::Data::PropertyValue ToVariantArrayVariant(IN const iCAX::Data::VariantArray& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::VariantArray FromVariantArrayVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            if (Value_.Is<iCAX::Data::VariantArray>())
            {
                return Value_.To<iCAX::Data::VariantArray>();
            }
            return {};
        }

        inline bool VariantArrayEqual(IN const iCAX::Data::VariantArray& Left_, IN const iCAX::Data::VariantArray& Right_)
        {
            return Left_ == Right_;
        }

        inline iCAX::Data::PropertyValue ToDouble4x4Variant(IN const iCAX::Data::Double4x4& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::Double4x4 MakeIdentityMatrix() noexcept
        {
            iCAX::Data::Double4x4 _Matrix;
            for (int _Index = 0; _Index < 4; ++_Index)
            {
                _Matrix(_Index, _Index) = 1.0;
            }
            return _Matrix;
        }

        inline iCAX::Data::Double4x4 MultiplyMatrix(
            IN const iCAX::Data::Double4x4& Left_,
            IN const iCAX::Data::Double4x4& Right_) noexcept
        {
            iCAX::Data::Double4x4 _Result;
            for (int _Row = 0; _Row < 4; ++_Row)
            {
                for (int _Column = 0; _Column < 4; ++_Column)
                {
                    double _Value = 0.0;
                    for (int _K = 0; _K < 4; ++_K)
                    {
                        _Value += Left_(_Row, _K) * Right_(_K, _Column);
                    }
                    _Result(_Row, _Column) = _Value;
                }
            }
            return _Result;
        }

        inline iCAX::Data::Double4x4 MakeLocalMatrix(
            IN double dPositionX_,
            IN double dPositionY_,
            IN double dPositionZ_,
            IN double dYawRadians_,
            IN double dPitchRadians_,
            IN double dRollRadians_,
            IN double dScaleX_,
            IN double dScaleY_,
            IN double dScaleZ_) noexcept
        {
            const auto _CosYaw = std::cos(dYawRadians_);
            const auto _SinYaw = std::sin(dYawRadians_);
            const auto _CosPitch = std::cos(dPitchRadians_);
            const auto _SinPitch = std::sin(dPitchRadians_);
            const auto _CosRoll = std::cos(dRollRadians_);
            const auto _SinRoll = std::sin(dRollRadians_);

            // 采用列向量约定：LocalToParent = T * Rz(yaw) * Ry(pitch) * Rx(roll) * S。
            auto _Matrix = MakeIdentityMatrix();
            _Matrix(0, 0) = _CosYaw * _CosPitch * dScaleX_;
            _Matrix(0, 1) = (_CosYaw * _SinPitch * _SinRoll - _SinYaw * _CosRoll) * dScaleY_;
            _Matrix(0, 2) = (_CosYaw * _SinPitch * _CosRoll + _SinYaw * _SinRoll) * dScaleZ_;
            _Matrix(0, 3) = dPositionX_;

            _Matrix(1, 0) = _SinYaw * _CosPitch * dScaleX_;
            _Matrix(1, 1) = (_SinYaw * _SinPitch * _SinRoll + _CosYaw * _CosRoll) * dScaleY_;
            _Matrix(1, 2) = (_SinYaw * _SinPitch * _CosRoll - _CosYaw * _SinRoll) * dScaleZ_;
            _Matrix(1, 3) = dPositionY_;

            _Matrix(2, 0) = -_SinPitch * dScaleX_;
            _Matrix(2, 1) = _CosPitch * _SinRoll * dScaleY_;
            _Matrix(2, 2) = _CosPitch * _CosRoll * dScaleZ_;
            _Matrix(2, 3) = dPositionZ_;
            return _Matrix;
        }

        inline double ReadDerivedDouble(
            IN iCAX::Database::CDerivedPropertyContext& Context_,
            IN const iCAX::Database::CComponentBase& Component_,
            IN const std::string& strPropertyName_,
            IN double dFallback_)
        {
            auto _Value = Context_.GetProperty(Component_, strPropertyName_);
            if (_Value.Is<float>())
            {
                return static_cast<double>(_Value.To<float>());
            }
            if (_Value.Is<double>())
            {
                return _Value.To<double>();
            }
            return dFallback_;
        }

        inline iCAX::Data::uuid ReadDerivedUuid(
            IN iCAX::Database::CDerivedPropertyContext& Context_,
            IN const iCAX::Database::CComponentBase& Component_,
            IN const std::string& strPropertyName_)
        {
            auto _Value = Context_.GetProperty(Component_, strPropertyName_);
            return FromUuidVariant(_Value);
        }

        inline iCAX::Data::Double4x4 EvaluateLocalToWorldMatrix(
            IN const class CTransformComponent& Transform_,
            IN iCAX::Database::CDerivedPropertyContext& Context_);

        /*
        * @brief 通用场景 Transform 组件。
        * @details
        *   Transform 是 Entity 的基础空间切面，表达局部位置、姿态、缩放和父级 Transform。
        *   它不属于 Render，也不属于 Physics；渲染、碰撞、相机、机床部件等系统都可以消费同一个 Transform。
        *   前端只消费派生后的世界矩阵，不参与父子关系和矩阵换算。
        *   ParentEntityID 与 ChildEntityIDs 是持久字段：它们表达树结构和同父节点下的有序孩子列表。
        *   局部 TRS 字段沿用运行时可观察策略：触发事件和版本，便于 PDO/服务增量同步。
        */
        class _TRANSFORM_EXP CTransformComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CTransformComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CTransformComponent)

            DECLARED_ICAX_FIELD(CTransformComponent, iCAX::Data::uuid, ParentEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CTransformComponent, iCAX::Data::VariantArray, ChildEntityIDs, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, PositionX, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, PositionY, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, PositionZ, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, YawRadians, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, PitchRadians, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, RollRadians, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, ScaleX, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, ScaleY, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CTransformComponent, double, ScaleZ, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_DERIVED_FIELD(CTransformComponent, iCAX::Data::Double4x4, LocalToWorldMatrix, ToDouble4x4Variant, EvaluateLocalToWorldMatrix)
        };

        inline iCAX::Data::Double4x4 EvaluateLocalToWorldMatrix(
            IN const CTransformComponent& Transform_,
            IN iCAX::Database::CDerivedPropertyContext& Context_)
        {
            const auto _Local = MakeLocalMatrix(
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_PositionX, 0.0),
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_PositionY, 0.0),
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_PositionZ, 0.0),
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_YawRadians, 0.0),
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_PitchRadians, 0.0),
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_RollRadians, 0.0),
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_ScaleX, 1.0),
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_ScaleY, 1.0),
                ReadDerivedDouble(Context_, Transform_, CTransformComponent::PropertyName_ScaleZ, 1.0));

            const auto _ParentID = ReadDerivedUuid(Context_, Transform_, CTransformComponent::PropertyName_ParentEntityID);
            if (_ParentID.is_nil())
            {
                return _Local;
            }

            auto _ParentWorld = Context_.GetProperty(
                _ParentID,
                CTransformComponent::S_ClassName,
                CTransformComponent::PropertyName_LocalToWorldMatrix);
            if (!_ParentWorld.Is<iCAX::Data::Double4x4>())
            {
                return _Local;
            }

            return MultiplyMatrix(_ParentWorld.To<iCAX::Data::Double4x4>(), _Local);
        }
    }
}
