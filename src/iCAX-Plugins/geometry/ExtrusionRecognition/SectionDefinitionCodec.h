#pragma once

#include "ExtrusionRecognitionTypes.h"

namespace iCAX::ExtrusionRecognition
{
    SDefinitionLoadResult DecodeSectionDefinitions(
        IN const iCAX::Data::Variant& Document_);

    SDefinitionLoadResult DecodeSectionDefinitionsJSON(
        IN std::string_view JSON_);
}
