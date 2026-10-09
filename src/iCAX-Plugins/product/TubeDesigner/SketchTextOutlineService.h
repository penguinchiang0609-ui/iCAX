#pragma once
#include "Data/Variant.h"

namespace iCAX::TubeDesigner
{
    // Uses the installed TrueType font's real, unhinted cubic outlines.
    // x/y define the text baseline; height is the em size, rotation is radians.
    iCAX::Data::ObjectMap GenerateSketchTextOutline(const iCAX::Data::ObjectMap& Input);
}
