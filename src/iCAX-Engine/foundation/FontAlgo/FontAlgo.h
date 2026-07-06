#pragma once

#include "framework.h"
#include "../FontAdapter/FontAdapter.h"
#include "../FontData/FontData.h"

#include <string>

namespace iCAX::FontAlgo
{
    FONTALGO_API bool Validate(const FontData::FontFaceInfo& faceInfo, std::string& error);
    FONTALGO_API bool Validate(const FontData::GlyphMetrics& metrics, std::string& error);
    FONTALGO_API bool Validate(const FontData::FontFace& face, std::string& error);

    FONTALGO_API const FontData::Glyph* FindGlyphByCodePoint(
        const FontData::FontFace& face,
        FontData::CodePoint codePoint);

    FONTALGO_API bool TryShapeText(
        const FontData::FontFace& face,
        const FontData::TextRun& run,
        FontData::ShapedText& shapedText,
        std::string& error,
        const FontAdapter::IFontKernelAdapter* adapter = nullptr);

    FONTALGO_API void Measure(
        const FontData::ShapedText& shapedText,
        double& advanceX,
        double& advanceY);
}
