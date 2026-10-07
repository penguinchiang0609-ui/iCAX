#pragma once

#include "TemplateContracts.h"

namespace iCAX::TemplateRuntime
{
    class _TEMPLATE_RUNTIME_EXP CTemplateCodec final
    {
    public:
        static STemplateDescriptor ParseDescriptor(
            const iCAX::Data::Variant& Document_);

        static SNeutralModel ParseNeutralModel(
            const iCAX::Data::Variant& Document_);

        static SNeutralModel ParseNeutralModel(
            const iCAX::Data::ObjectMap& Document_);

        // Validate a display-only result and adapt it for internal evaluation.
        // The returned neutral document is temporary; identity and input
        // parameters belong to the host, not to the display script result.
        static iCAX::Data::ObjectMap AdaptDisplayModel(
            const iCAX::Data::Variant& Document_);

        static iCAX::Data::ObjectMap AdaptDisplayModel(
            const iCAX::Data::ObjectMap& Document_);

        // Internal input models use versions two and three. Public version
        // four requires the separately owned display design document.
        static iCAX::Data::ObjectMap AdaptManufacturingModel(
            const iCAX::Data::Variant& Document_);

        static iCAX::Data::ObjectMap AdaptManufacturingModel(
            const iCAX::Data::ObjectMap& Document_);

        static iCAX::Data::ObjectMap AdaptManufacturingInputModel(
            const iCAX::Data::Variant& Document_);

        static iCAX::Data::ObjectMap AdaptManufacturingInputModel(
            const iCAX::Data::ObjectMap& Document_);

        // Compose a temporary private model from shared design plus connections
        // and process declarations; never change either public document.
        static iCAX::Data::ObjectMap ComposeManufacturingModel(
            const iCAX::Data::Variant& Definition_,
            const iCAX::Data::Variant& Design_);

        static iCAX::Data::ObjectMap ComposeManufacturingModel(
            const iCAX::Data::ObjectMap& Definition_,
            const iCAX::Data::ObjectMap& Design_);

        // Check fixed identities or declared generated output sets against the
        // independently executed model; design inputs are never counted as stock.
        static void ValidateManufacturingExecutionModel(
            const iCAX::Data::Variant& Definition_,
            const iCAX::Data::Variant& Execution_,
            const iCAX::Data::Variant* Design_ = nullptr);

        static void ValidateManufacturingExecutionModel(
            const iCAX::Data::ObjectMap& Definition_,
            const iCAX::Data::ObjectMap& Execution_,
            const iCAX::Data::ObjectMap* Design_ = nullptr);

        // Perform the same complete declaration, graph and provenance checks
        // once, returning an independently owned model for evaluation.
        static SNeutralModel ValidateAndParseManufacturingExecutionModel(
            const iCAX::Data::ObjectMap& Definition_,
            const iCAX::Data::ObjectMap& Execution_,
            const iCAX::Data::ObjectMap* Design_ = nullptr);

        // Adapt resource declarations and item placements to the existing
        // evaluator graph without changing part identity or the source document.
        // Version-one documents pass through unchanged.
        static iCAX::Data::ObjectMap ExpandNeutralModelResources(
            const iCAX::Data::Variant& Document_);

        static iCAX::Data::ObjectMap ExpandNeutralModelResources(
            const iCAX::Data::ObjectMap& Document_);

        static iCAX::Data::ObjectMap ValidateAndNormalizeParameters(
            const STemplateDescriptor& Descriptor_,
            const iCAX::Data::ObjectMap& Values_);

        // Validate the field's full constraints for the supplied role subset.
        // Call with an empty map to validate declarations before execution.
        static void ValidateProductProfileOverrides(
            const STemplateDescriptor& Descriptor_,
            const iCAX::Data::ObjectMap& Overrides_);

        // Native second guard uses actual SDK lookup roles from the response
        // envelope. Inactive drafts remain in the original normalized inputs.
        static void ValidateConsumedProductProfileOverrides(
            const STemplateDescriptor& Descriptor_,
            const iCAX::Data::ObjectMap& Overrides_,
            const std::vector<std::string>& ProfileRolesConsumed_);

        static iCAX::Data::ObjectMap MakePresentationDescriptor(
            const STemplateDescriptor& Descriptor_,
            const std::string& strLocale_ = "zh-CN");

        static iCAX::Data::ObjectMap MakeEvaluationRequest(
            const STemplateDescriptor& Descriptor_,
            const iCAX::Data::ObjectMap& Parameters_,
            const std::string& strTemplatePath_);
    };
}
