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

        static iCAX::Data::ObjectMap ValidateAndNormalizeParameters(
            const STemplateDescriptor& Descriptor_,
            const iCAX::Data::ObjectMap& Values_);

        static iCAX::Data::ObjectMap MakePresentationDescriptor(
            const STemplateDescriptor& Descriptor_,
            const std::string& strLocale_ = "zh-CN");

        static iCAX::Data::ObjectMap MakeEvaluationRequest(
            const STemplateDescriptor& Descriptor_,
            const iCAX::Data::ObjectMap& Parameters_,
            const std::string& strTemplatePath_);
    };
}

