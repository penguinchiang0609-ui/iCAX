#pragma once

#include "ComponentValueHelpers.h"
#include "IntentToolpathExport.h"

namespace iCAX::CAM::Intent
{
// 每个工件一个意图刀路文档根；根的ChildEntityIDs就是左侧树的顶层刀路。
class _INTENT_TOOLPATH_EXP CIntentDocumentComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CIntentDocumentComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CIntentDocumentComponent)
    DECLARED_ICAX_FIELD(CIntentDocumentComponent, iCAX::Data::uuid, WorkpieceEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
    DECLARED_ICAX_FIELD(CIntentDocumentComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

// 所有意图刀路共有的身份和当前可用状态。不使用 Kind 字段区分具体工艺。
class _INTENT_TOOLPATH_EXP CIntentToolpathComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CIntentToolpathComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CIntentToolpathComponent)

    DECLARED_ICAX_FIELD(CIntentToolpathComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CIntentToolpathComponent, bool, Enabled, true, BoolEqual, ToBoolVariant, FromBoolVariant)
    DECLARED_ICAX_FIELD(CIntentToolpathComponent, std::string, SourceState, std::string("Current"), StringEqual, ToStringVariant, FromStringVariant)
};

// 对通用几何表达资源的引用和裁剪区间；不保存任何具体几何内核对象。
class _INTENT_TOOLPATH_EXP CIntentGeometryComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CIntentGeometryComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CIntentGeometryComponent)

    DECLARED_ICAX_FIELD(CIntentGeometryComponent, std::string, CurveResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CIntentGeometryComponent, double, StartParameter, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
    DECLARED_ICAX_FIELD(CIntentGeometryComponent, double, EndParameter, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
    DECLARED_ICAX_FIELD(CIntentGeometryComponent, bool, Reversed, false, BoolEqual, ToBoolVariant, FromBoolVariant)
    DECLARED_ICAX_FIELD(CIntentGeometryComponent, std::string, SupportSurfaceResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CIntentGeometryComponent, std::string, CoordinateSystemResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

// 当前生成树。Parent/Children是所有权关系；Revision在子项、顺序或输出角色变化时递增。
class _INTENT_TOOLPATH_EXP CIntentHierarchyComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CIntentHierarchyComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CIntentHierarchyComponent)

    DECLARED_ICAX_FIELD(CIntentHierarchyComponent, iCAX::Data::uuid, ParentEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
    DECLARED_ICAX_FIELD(CIntentHierarchyComponent, iCAX::Data::VariantArray, ChildEntityIDs, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
    DECLARED_ICAX_FIELD(CIntentHierarchyComponent, unsigned long long, Revision, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
};

// 一次参数化生成操作，例如微联、合并、打断或桥接。输入决定替代关系，输出组成生成结果。
class _INTENT_TOOLPATH_EXP CIntentDerivationComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CIntentDerivationComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CIntentDerivationComponent)

    DECLARED_ICAX_FIELD(CIntentDerivationComponent, std::string, OperationKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CIntentDerivationComponent, bool, Active, true, BoolEqual, ToBoolVariant, FromBoolVariant)
    DECLARED_ICAX_FIELD(CIntentDerivationComponent, bool, LiveLinked, true, BoolEqual, ToBoolVariant, FromBoolVariant)
    DECLARED_ICAX_FIELD(CIntentDerivationComponent, iCAX::Data::VariantArray, InputIntentEntityIDs, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
    DECLARED_ICAX_FIELD(CIntentDerivationComponent, iCAX::Data::VariantArray, OutputIntentEntityIDs, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
    DECLARED_ICAX_FIELD(CIntentDerivationComponent, std::string, ParametersResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

// 生成输出的稳定身份：同一个Generator下通过RoleKey匹配重算前后的Entity。
class _INTENT_TOOLPATH_EXP CDerivedOutputComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CDerivedOutputComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CDerivedOutputComponent)

    DECLARED_ICAX_FIELD(CDerivedOutputComponent, iCAX::Data::uuid, GeneratorEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
    DECLARED_ICAX_FIELD(CDerivedOutputComponent, std::string, RoleKey, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CDerivedOutputComponent, std::string, SourceFeatureKey, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CCADIntentSourceComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CCADIntentSourceComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CCADIntentSourceComponent)

    DECLARED_ICAX_FIELD(CCADIntentSourceComponent, iCAX::Data::uuid, WorkpieceEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
    DECLARED_ICAX_FIELD(CCADIntentSourceComponent, std::string, FeatureCode, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CCADIntentSourceComponent, std::string, TopologyKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CCADIntentSourceComponent, unsigned long long, TopologyID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
    DECLARED_ICAX_FIELD(CCADIntentSourceComponent, std::string, GeometryFingerprint, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CIntentReferenceSourceComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CIntentReferenceSourceComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CIntentReferenceSourceComponent)

    DECLARED_ICAX_FIELD(CIntentReferenceSourceComponent, iCAX::Data::VariantArray, SourceIntentEntityIDs, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
    DECLARED_ICAX_FIELD(CIntentReferenceSourceComponent, std::string, RelationKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CDrawnIntentSourceComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CDrawnIntentSourceComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CDrawnIntentSourceComponent)

    DECLARED_ICAX_FIELD(CDrawnIntentSourceComponent, std::string, SketchResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CDrawnIntentSourceComponent, std::string, CoordinateSystemResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CConstructedIntentSourceComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CConstructedIntentSourceComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CConstructedIntentSourceComponent)

    DECLARED_ICAX_FIELD(CConstructedIntentSourceComponent, std::string, ConstructionKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CConstructedIntentSourceComponent, iCAX::Data::VariantArray, InputIntentEntityIDs, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
    DECLARED_ICAX_FIELD(CConstructedIntentSourceComponent, std::string, ParametersResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CTemplateIntentSourceComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CTemplateIntentSourceComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CTemplateIntentSourceComponent)

    DECLARED_ICAX_FIELD(CTemplateIntentSourceComponent, iCAX::Data::uuid, TemplateIntentEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
    DECLARED_ICAX_FIELD(CTemplateIntentSourceComponent, iCAX::Data::uuid, TargetWorkpieceEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
    DECLARED_ICAX_FIELD(CTemplateIntentSourceComponent, std::string, MappingResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CImportedIntentSourceComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CImportedIntentSourceComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CImportedIntentSourceComponent)

    DECLARED_ICAX_FIELD(CImportedIntentSourceComponent, std::string, SourcePath, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CImportedIntentSourceComponent, std::string, ExternalObjectID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CImportedIntentSourceComponent, std::string, ContentFingerprint, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

// 多工件归属。数组元素由产品约定为{workpieceEntityId, role, requiredForCompletion}。
class _INTENT_TOOLPATH_EXP CWorkpieceAttributionComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CWorkpieceAttributionComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CWorkpieceAttributionComponent)

    DECLARED_ICAX_FIELD(CWorkpieceAttributionComponent, iCAX::Data::VariantArray, Workpieces, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
};

// 工艺组件均为正交切面；组合而非继承决定Entity含义。
class _INTENT_TOOLPATH_EXP CCutIntentComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CCutIntentComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CCutIntentComponent)
    DECLARED_ICAX_FIELD(CCutIntentComponent, std::string, ParameterSetResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CMicroJointIntentComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CMicroJointIntentComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CMicroJointIntentComponent)
    DECLARED_ICAX_FIELD(CMicroJointIntentComponent, double, Width, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
    DECLARED_ICAX_FIELD(CMicroJointIntentComponent, std::string, Mode, std::string("NoCut"), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CLeadIntentComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CLeadIntentComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CLeadIntentComponent)
    DECLARED_ICAX_FIELD(CLeadIntentComponent, std::string, Direction, std::string("In"), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CLeadIntentComponent, std::string, ParameterSetResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _INTENT_TOOLPATH_EXP CBridgeIntentComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CBridgeIntentComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CBridgeIntentComponent)
    DECLARED_ICAX_FIELD(CBridgeIntentComponent, std::string, ParameterSetResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};
}
