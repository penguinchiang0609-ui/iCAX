#pragma once

#include "TemplateRuntimeExport.h"

#include "Data/Variant.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace iCAX::TemplateRuntime
{
    inline constexpr const char* kTemplateDescriptorSchema = "icax.template-descriptor";
    inline constexpr std::uint32_t kTemplateDescriptorSchemaVersion = 1;
    inline constexpr const char* kNeutralModelSchema = "icax.neutral-model";
    inline constexpr std::uint32_t kNeutralModelSchemaVersion = 1;
    inline constexpr const char* kTemplateProtocol = "icax.template-runtime";
    inline constexpr std::uint32_t kTemplateProtocolVersion = 1;
    inline constexpr std::size_t kMaximumResourceBRepBytes = 32u * 1024u * 1024u;

    struct _TEMPLATE_RUNTIME_EXP SLocalizedText final
    {
        std::string Default;
        std::map<std::string, std::string> Translations;

        std::string Resolve(const std::string& strLocale_ = "zh-CN") const;
    };

    enum class EParameterValueType : std::uint8_t
    {
        Number,
        Integer,
        Boolean,
        String,
        Enumeration
    };

    enum class EParameterQuantity : std::uint8_t
    {
        None,
        Length,
        Angle,
        Ratio,
        Count
    };

    enum class EConditionKind : std::uint8_t
    {
        Always,
        Equals,
        NotEquals,
        All,
        Any,
        Not
    };

    struct _TEMPLATE_RUNTIME_EXP SParameterCondition final
    {
        EConditionKind Kind = EConditionKind::Always;
        std::string ParameterKey;
        iCAX::Data::Variant ExpectedValue;
        std::vector<SParameterCondition> Children;
    };

    struct _TEMPLATE_RUNTIME_EXP SParameterChoice final
    {
        iCAX::Data::Variant Value;
        SLocalizedText DisplayName;
        std::string Description;
    };

    struct _TEMPLATE_RUNTIME_EXP SParameterConstraints final
    {
        std::optional<double> Minimum;
        std::optional<double> Maximum;
        std::optional<double> Step;
        std::optional<std::uint64_t> MinimumLength;
        std::optional<std::uint64_t> MaximumLength;
        std::string Pattern;
    };

    struct _TEMPLATE_RUNTIME_EXP SParameterDefinition final
    {
        std::string Key;
        SLocalizedText DisplayName;
        std::string Description;
        EParameterValueType ValueType = EParameterValueType::String;
        EParameterQuantity Quantity = EParameterQuantity::None;
        std::string Unit;
        iCAX::Data::Variant DefaultValue;
        bool Required = true;
        bool ReadOnly = false;
        SParameterConstraints Constraints;
        std::vector<SParameterChoice> Choices;
        std::string GroupKey;
        std::optional<std::int32_t> Order;
        SParameterCondition VisibleWhen;
        SParameterCondition EnabledWhen;
        iCAX::Data::ObjectMap Presentation;
    };

    struct _TEMPLATE_RUNTIME_EXP SParameterGroup final
    {
        std::string Key;
        SLocalizedText DisplayName;
        std::int32_t Order = 0;
    };

    struct _TEMPLATE_RUNTIME_EXP STemplateDescriptor final
    {
        std::string ID;
        std::string Version;
        std::string PackageDigest;
        SLocalizedText DisplayName;
        std::string Description;
        std::vector<SParameterGroup> Groups;
        std::vector<SParameterDefinition> Parameters;
        iCAX::Data::ObjectMap Extensions;
    };

    enum class EGeometryOperator : std::uint8_t
    {
        Profile2D,
        Extrude,
        Sweep,
        Revolve,
        Loft,
        Boolean,
        Transform,
        Pattern,
        Fillet,
        Chamfer,
        Compound,
        // Resolved, in-memory ASCII BRepTools text in arguments.brep. References
        // may be resolved by a host, but generic geometry evaluation never does IO.
        Resource
    };

    struct _TEMPLATE_RUNTIME_EXP SGeometryNode final
    {
        std::string Key;
        EGeometryOperator Operator = EGeometryOperator::Profile2D;
        std::vector<std::string> Inputs;
        iCAX::Data::ObjectMap Arguments;
    };

    struct _TEMPLATE_RUNTIME_EXP SModelItem final
    {
        std::string Key;
        SLocalizedText DisplayName;
        std::map<std::string, std::string> Representations;
        std::vector<std::string> Children;
        iCAX::Data::ObjectMap Properties;
    };

    struct _TEMPLATE_RUNTIME_EXP SOutputSet final
    {
        std::string Key;
        std::string Purpose;
        std::vector<std::string> ItemKeys;
        iCAX::Data::ObjectMap Properties;
    };

    struct _TEMPLATE_RUNTIME_EXP SModelRelationship final
    {
        std::string Key;
        std::string Kind;
        std::vector<std::string> ItemKeys;
        iCAX::Data::ObjectMap Properties;
    };

    struct _TEMPLATE_RUNTIME_EXP STableColumn final
    {
        std::string Key;
        SLocalizedText DisplayName;
        std::string ValueType;
        std::string Unit;
    };

    struct _TEMPLATE_RUNTIME_EXP STableRow final
    {
        std::string Key;
        std::string ParentKey;
        std::string ItemKey;
        iCAX::Data::ObjectMap Values;
    };

    struct _TEMPLATE_RUNTIME_EXP SDataTable final
    {
        std::string Key;
        SLocalizedText DisplayName;
        std::vector<STableColumn> Columns;
        std::vector<STableRow> Rows;
    };

    struct _TEMPLATE_RUNTIME_EXP SDiagnostic final
    {
        std::string Severity;
        std::string Code;
        std::string Message;
        std::vector<std::string> ParameterKeys;
        std::string ModelKey;
    };

    struct _TEMPLATE_RUNTIME_EXP SNeutralModel final
    {
        std::string TemplateID;
        std::string TemplateVersion;
        std::string PackageDigest;
        std::string CoordinateSystem = "right-handed-x-width-y-depth-z-height";
        std::string LengthUnit = "mm";
        iCAX::Data::ObjectMap Parameters;
        std::vector<SGeometryNode> Geometry;
        std::vector<SModelItem> Items;
        std::vector<SOutputSet> Outputs;
        std::vector<SModelRelationship> Relationships;
        std::vector<SDataTable> Tables;
        std::vector<SDiagnostic> Diagnostics;
        iCAX::Data::ObjectMap Extensions;
    };
}
