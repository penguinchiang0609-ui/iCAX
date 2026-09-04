#pragma once

#include "TemplateRuntimeExport.h"

#include "Data/Variant.h"

#include <string>

namespace iCAX::TemplateRuntime
{
    // Standard UTF-8 JSON used only at language/process boundaries. This is
    // intentionally separate from Data::VariantSerializer's typed iCAX format.
    class _TEMPLATE_RUNTIME_EXP CStandardJsonCodec final
    {
    public:
        static iCAX::Data::Variant Parse(const std::string& strJson_);
        static std::string Serialize(const iCAX::Data::Variant& Value_);
    };
}
