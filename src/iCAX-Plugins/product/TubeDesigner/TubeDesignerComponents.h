#pragma once

#include "Database/ComponentHelper.h"

#include <cmath>
#include <string>

namespace iCAX::TubeDesigner
{
    inline iCAX::Data::PropertyValue ToStringVariant(const std::string& Value_) { return iCAX::Data::PropertyValue(Value_); }
    inline iCAX::Data::PropertyValue ToUInt64Variant(const unsigned long long& Value_) { return iCAX::Data::PropertyValue(Value_); }
    inline iCAX::Data::PropertyValue ToDoubleVariant(const double& Value_) { return iCAX::Data::PropertyValue(Value_); }
    inline iCAX::Data::PropertyValue ToUuidVariant(const iCAX::Data::uuid& Value_) { return iCAX::Data::PropertyValue(Value_); }
    inline iCAX::Data::PropertyValue ToObjectMapVariant(const iCAX::Data::ObjectMap& Value_) { return iCAX::Data::PropertyValue(Value_); }

    inline std::string FromStringVariant(const iCAX::Data::PropertyValue& Value_) { return Value_.To<std::string>(); }
    inline unsigned long long FromUInt64Variant(const iCAX::Data::PropertyValue& Value_) { return Value_.To<unsigned long long>(); }
    inline double FromDoubleVariant(const iCAX::Data::PropertyValue& Value_)
    {
        return Value_.Is<float>() ? static_cast<double>(Value_.To<float>()) : Value_.To<double>();
    }
    inline iCAX::Data::uuid FromUuidVariant(const iCAX::Data::PropertyValue& Value_) { return Value_.To<iCAX::Data::uuid>(); }
    inline iCAX::Data::ObjectMap FromObjectMapVariant(const iCAX::Data::PropertyValue& Value_) { return Value_.To<iCAX::Data::ObjectMap>(); }

    inline bool StringEqual(const std::string& Left_, const std::string& Right_) { return Left_ == Right_; }
    inline bool UInt64Equal(const unsigned long long& Left_, const unsigned long long& Right_) { return Left_ == Right_; }
    inline bool DoubleEqual(const double& Left_, const double& Right_) { return std::abs(Left_ - Right_) <= 1.0e-7; }
    inline bool UuidEqual(const iCAX::Data::uuid& Left_, const iCAX::Data::uuid& Right_) { return Left_ == Right_; }
    inline bool ObjectMapEqual(const iCAX::Data::ObjectMap& Left_, const iCAX::Data::ObjectMap& Right_) { return Left_ == Right_; }

    class CTubeDesignerRootComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CTubeDesignerRootComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CTubeDesignerRootComponent)
        DECLARED_ICAX_FIELD(CTubeDesignerRootComponent, iCAX::Data::uuid, ActiveProductID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
    };

    class CProductInstanceComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CProductInstanceComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CProductInstanceComponent)
        DECLARED_ICAX_FIELD(CProductInstanceComponent, std::string, ProductCode, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CProductInstanceComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CProductInstanceComponent, std::string, CreatedAt, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CProductInstanceComponent, std::string, TemplateID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CProductInstanceComponent, std::string, TemplateVersion, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CProductInstanceComponent, std::string, Status, std::string("Current"), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CProductInstanceComponent, iCAX::Data::ObjectMap, Parameters, iCAX::Data::ObjectMap(), ObjectMapEqual, ToObjectMapVariant, FromObjectMapVariant)
        DECLARED_ICAX_FIELD(CProductInstanceComponent, iCAX::Data::uuid, ActiveGenerationRunID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
    };

    class CAssemblyMemberComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CAssemblyMemberComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CAssemblyMemberComponent)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, iCAX::Data::uuid, ProductID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, iCAX::Data::uuid, ManufacturingPartID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, unsigned long long, MemberIndex, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, std::string, StableKey, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, std::string, MemberType, std::string("part"), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, unsigned long long, ChildPartCount, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, std::string, Role, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, std::string, ProfileType, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, SectionWidth, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, SectionDepth, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, WallThickness, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, CornerRadius, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, Length, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, X1, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, Y1, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, X2, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, double, Y2, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, std::string, PreviewGeometryResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, unsigned long long, PreviewGeometryResourceVersion, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CAssemblyMemberComponent, iCAX::Data::ObjectMap, ItemProperties, iCAX::Data::ObjectMap(), ObjectMapEqual, ToObjectMapVariant, FromObjectMapVariant)
    };

    class CManufacturingPartComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CManufacturingPartComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CManufacturingPartComponent)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, iCAX::Data::uuid, ProductID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, iCAX::Data::uuid, SourceMemberID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, iCAX::Data::uuid, GenerationRunID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, unsigned long long, PartIndex, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, std::string, StableKey, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, std::string, PartNumber, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, std::string, Role, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, unsigned long long, Quantity, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, std::string, ProfileType, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, double, SectionWidth, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, double, SectionDepth, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, double, WallThickness, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, double, CornerRadius, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, double, Length, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, std::string, ManufacturingGeometryResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, unsigned long long, ManufacturingGeometryResourceVersion, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, std::string, ThumbnailGeometryResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, unsigned long long, ThumbnailGeometryResourceVersion, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, std::string, FileName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, std::string, Status, std::string("Ready"), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CManufacturingPartComponent, iCAX::Data::ObjectMap, ItemProperties, iCAX::Data::ObjectMap(), ObjectMapEqual, ToObjectMapVariant, FromObjectMapVariant)
    };

    class CJointIntentComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CJointIntentComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CJointIntentComponent)
        DECLARED_ICAX_FIELD(CJointIntentComponent, iCAX::Data::uuid, ProductID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CJointIntentComponent, iCAX::Data::uuid, TargetMemberID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CJointIntentComponent, iCAX::Data::uuid, InsertedMemberID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CJointIntentComponent, std::string, Mode, std::string("through"), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CJointIntentComponent, double, Clearance, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CJointIntentComponent, double, X, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        DECLARED_ICAX_FIELD(CJointIntentComponent, double, Y, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
    };

    class CGenerationRunComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CGenerationRunComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CGenerationRunComponent)
        DECLARED_ICAX_FIELD(CGenerationRunComponent, iCAX::Data::uuid, ProductID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        DECLARED_ICAX_FIELD(CGenerationRunComponent, std::string, TemplateID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CGenerationRunComponent, std::string, TemplateVersion, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CGenerationRunComponent, std::string, Status, std::string("Succeeded"), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CGenerationRunComponent, unsigned long long, PartCount, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CGenerationRunComponent, unsigned long long, IssueCount, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        DECLARED_ICAX_FIELD(CGenerationRunComponent, std::string, PackageDigest, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        DECLARED_ICAX_FIELD(CGenerationRunComponent, iCAX::Data::ObjectMap, NeutralModel, iCAX::Data::ObjectMap(), ObjectMapEqual, ToObjectMapVariant, FromObjectMapVariant)
    };
}
