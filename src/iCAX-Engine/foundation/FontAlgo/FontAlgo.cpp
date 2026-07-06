#include "pch.h"
#include "FontAlgo.h"

#include <cmath>

namespace
{
    bool IsFinite(const double value)
    {
        return std::isfinite(value);
    }
}

namespace iCAX::FontAlgo
{
    bool Validate(const FontData::FontFaceInfo& faceInfo, std::string& error)
    {
        if (faceInfo.FamilyName.empty() && faceInfo.FullName.empty() && faceInfo.PostScriptName.empty())
        {
            error = "Font face must provide at least one name.";
            return false;
        }

        if (faceInfo.UnitsPerEm == 0)
        {
            error = "Font face units per em must be non-zero.";
            return false;
        }

        error.clear();
        return true;
    }

    bool Validate(const FontData::GlyphMetrics& metrics, std::string& error)
    {
        if (!IsFinite(metrics.AdvanceX)
            || !IsFinite(metrics.AdvanceY)
            || !IsFinite(metrics.BearingX)
            || !IsFinite(metrics.BearingY)
            || !IsFinite(metrics.Width)
            || !IsFinite(metrics.Height))
        {
            error = "Glyph metrics contains invalid number.";
            return false;
        }

        error.clear();
        return true;
    }

    bool Validate(const FontData::FontFace& face, std::string& error)
    {
        if (!Validate(face.Info, error))
        {
            return false;
        }

        for (const auto& glyph : face.Glyphs)
        {
            if (!Validate(glyph.Metrics, error))
            {
                return false;
            }
        }

        error.clear();
        return true;
    }

    const FontData::Glyph* FindGlyphByCodePoint(
        const FontData::FontFace& face,
        const FontData::CodePoint codePoint)
    {
        for (const auto& glyph : face.Glyphs)
        {
            for (const auto mappedCodePoint : glyph.CodePoints)
            {
                if (mappedCodePoint == codePoint)
                {
                    return &glyph;
                }
            }
        }

        return nullptr;
    }

    bool TryShapeText(
        const FontData::FontFace& face,
        const FontData::TextRun& run,
        FontData::ShapedText& shapedText,
        std::string& error,
        const FontAdapter::IFontKernelAdapter* adapter)
    {
        if (adapter)
        {
            return adapter->TryShapeText(face, run, shapedText, error);
        }

        if (run.Direction != FontData::ETextDirection::LeftToRight)
        {
            error = "Font adapter is required for non-left-to-right text shaping.";
            return false;
        }

        shapedText = {};
        shapedText.SourceRun = run;
        double penX = 0.0;
        for (std::size_t index = 0; index < run.Text.size(); ++index)
        {
            const auto* glyph = FindGlyphByCodePoint(face, static_cast<FontData::CodePoint>(run.Text[index]));
            if (!glyph)
            {
                error = "Simple text shaping failed because a glyph mapping is missing.";
                return false;
            }

            FontData::GlyphPlacement placement;
            placement.Glyph = glyph->ID;
            placement.Cluster = static_cast<std::uint32_t>(index);
            placement.OffsetX = penX;
            placement.OffsetY = 0.0;
            placement.AdvanceX = glyph->Metrics.AdvanceX + run.Style.LetterSpacing;
            placement.AdvanceY = glyph->Metrics.AdvanceY;
            shapedText.Glyphs.push_back(placement);
            penX += placement.AdvanceX;
        }

        Measure(shapedText, shapedText.AdvanceX, shapedText.AdvanceY);
        error.clear();
        return true;
    }

    void Measure(
        const FontData::ShapedText& shapedText,
        double& advanceX,
        double& advanceY)
    {
        advanceX = 0.0;
        advanceY = 0.0;
        for (const auto& glyph : shapedText.Glyphs)
        {
            advanceX += glyph.AdvanceX;
            advanceY += glyph.AdvanceY;
        }
    }
}
