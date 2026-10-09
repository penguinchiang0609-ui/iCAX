#include "pch.h"
#include "SketchTextOutlineService.h"
#include <Windows.h>
#include <usp10.h>
#include <array>
#include <cstring>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "usp10.lib")

namespace iCAX::TubeDesigner
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    namespace
    {
        std::string Text(const ObjectMap& Input, const char* Key, const char* Default = "")
        {
            const auto It = Input.find(Key);
            if (It == Input.end()) return Default;
            if (!It->second.Is<std::string>()) throw std::invalid_argument("文字和字体名称必须是字符串。");
            return It->second.To<std::string>();
        }
        double Number(const ObjectMap& Input, const char* Key, double Default)
        {
            const auto It = Input.find(Key);
            if (It == Input.end()) return Default;
            const auto& Value = It->second;
            double Result;
            if (Value.Is<double>()) Result = Value.To<double>();
            else if (Value.Is<float>()) Result = Value.To<float>();
            else if (Value.Is<int>()) Result = Value.To<int>();
            else if (Value.Is<unsigned int>()) Result = Value.To<unsigned int>();
            else if (Value.Is<long long>()) Result = static_cast<double>(Value.To<long long>());
            else throw std::invalid_argument("文字参数必须是数值。");
            if (!std::isfinite(Result)) throw std::invalid_argument("文字参数必须是有限数值。");
            return Result;
        }
        std::wstring Wide(const std::string& Value)
        {
            if (Value.find('\0') != std::string::npos) throw std::invalid_argument("文字不能包含空字符。");
            const int Size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Value.data(), static_cast<int>(Value.size()), nullptr, 0);
            if (!Size) throw std::invalid_argument("文字不是有效的 UTF-8。");
            std::wstring Result(Size, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Value.data(), static_cast<int>(Value.size()), Result.data(), Size);
            return Result;
        }
        int CALLBACK FontFamilyExists(const LOGFONTW*, const TEXTMETRICW*, DWORD Type, LPARAM Param)
        {
            if (Type & TRUETYPE_FONTTYPE) *reinterpret_cast<bool*>(Param) = true;
            return 0;
        }
        struct FontContext
        {
            HDC DC = CreateCompatibleDC(nullptr);
            HFONT Font = nullptr;
            HGDIOBJ OldFont = nullptr;
            SCRIPT_CACHE Cache = nullptr;
            ~FontContext()
            {
                ScriptFreeCache(&Cache);
                if (OldFont && DC) SelectObject(DC, OldFont);
                if (Font) DeleteObject(Font);
                if (DC) DeleteDC(DC);
            }
            void Select(const std::wstring& Family, int Em)
            {
                // GetTextFace can return a localized family name (e.g. 微软雅黑
                // for Microsoft YaHei). Enumerate the requested family first
                // instead of rejecting a valid localized name as substitution.
                LOGFONTW Requested{};
                Requested.lfCharSet = DEFAULT_CHARSET;
                wcsncpy_s(Requested.lfFaceName, Family.c_str(), _TRUNCATE);
                bool Installed = false;
                EnumFontFamiliesExW(DC, &Requested, &FontFamilyExists, reinterpret_cast<LPARAM>(&Installed), 0);
                if (!Installed) throw std::invalid_argument("所选字体未安装，请填写已安装的 TrueType 字体名称。");
                HFONT Next = CreateFontW(-Em, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, Family.c_str());
                if (!Next) throw std::runtime_error("无法加载所选字体。");
                const auto Previous = SelectObject(DC, Next);
                if (!OldFont) OldFont = Previous;
                if (Font) DeleteObject(Font);
                Font = Next;
            }
        };
        double FixedValue(const FIXED& Value) { return static_cast<double>(Value.value) + static_cast<double>(Value.fract) / 65536.0; }
        using Point = std::array<double, 2>;
        VariantArray Pair(const Point& P) { return { P[0], P[1] }; }
        ObjectMap Line(const Point& A, const Point& B)
        {
            return {{"kind",std::string("line")},{"x1",A[0]},{"y1",A[1]},{"x2",B[0]},{"y2",B[1]}};
        }
        struct Placement
        {
            double X = 0, Y = 0, Scale = 1, Cos = 1, Sin = 0;
            Point Map(const POINTFX& P, double PenX, double OffsetX, double OffsetY) const
            {
                const double A = (FixedValue(P.x) + PenX + OffsetX) * Scale;
                const double B = (FixedValue(P.y) + OffsetY) * Scale;
                return { X + Cos * A - Sin * B, Y + Sin * A + Cos * B };
            }
        };
        void AppendGlyph(FontContext& Context, WORD Glyph, double PenX, const GOFFSET& Offset,
            const Placement& Transform, const std::string& Group, VariantArray& Entities)
        {
            GLYPHMETRICS Metrics{};
            MAT2 Identity{}; Identity.eM11.value = 1; Identity.eM22.value = 1;
            const UINT Flags = GGO_BEZIER | GGO_UNHINTED | GGO_GLYPH_INDEX;
            const DWORD Size = GetGlyphOutlineW(Context.DC, Glyph, Flags, &Metrics, 0, nullptr, &Identity);
            if (Size == GDI_ERROR) throw std::runtime_error("所选字体不支持精确轮廓，请选择 TrueType 轮廓字体。");
            if (!Size) return; // Spaces have a real advance and no outline.
            if (Size > 16 * 1024 * 1024) throw std::runtime_error("字形轮廓过大。");
            std::vector<unsigned char> Buffer(Size);
            if (GetGlyphOutlineW(Context.DC, Glyph, Flags, &Metrics, Size, Buffer.data(), &Identity) == GDI_ERROR)
                throw std::runtime_error("读取字形轮廓失败。");
            std::size_t Position = 0;
            while (Position < Buffer.size())
            {
                if (Buffer.size() - Position < sizeof(TTPOLYGONHEADER)) throw std::runtime_error("字形轮廓数据不完整。");
                TTPOLYGONHEADER Header{};
                std::memcpy(&Header, Buffer.data() + Position, sizeof(Header));
                if (Header.dwType != TT_POLYGON_TYPE || Header.cb < sizeof(Header) || Header.cb > Buffer.size() - Position)
                    throw std::runtime_error("字形轮廓头无效。");
                const std::size_t End = Position + Header.cb;
                Position += sizeof(Header);
                Point Start = Transform.Map(Header.pfxStart, PenX, Offset.du, Offset.dv), Previous = Start;
                VariantArray Segments;
                while (Position < End)
                {
                    constexpr std::size_t Prefix = offsetof(TTPOLYCURVE, apfx);
                    if (End - Position < Prefix) throw std::runtime_error("字形曲线数据不完整。");
                    WORD Type = 0, Count = 0;
                    std::memcpy(&Type, Buffer.data() + Position, sizeof(WORD));
                    std::memcpy(&Count, Buffer.data() + Position + sizeof(WORD), sizeof(WORD));
                    const std::size_t Bytes = Prefix + static_cast<std::size_t>(Count) * sizeof(POINTFX);
                    if (!Count || Bytes > End - Position) throw std::runtime_error("字形控制点数据无效。");
                    auto Get = [&](std::size_t Index)
                    {
                        POINTFX Value{};
                        std::memcpy(&Value, Buffer.data() + Position + Prefix + Index * sizeof(POINTFX), sizeof(Value));
                        return Transform.Map(Value, PenX, Offset.du, Offset.dv);
                    };
                    if (Type == TT_PRIM_LINE)
                    {
                        for (std::size_t I = 0; I < Count; ++I) { const auto Next = Get(I); Segments.emplace_back(Line(Previous, Next)); Previous = Next; }
                    }
                    else if (Type == TT_PRIM_CSPLINE && Count % 3 == 0)
                    {
                        for (std::size_t I = 0; I < Count; I += 3)
                        {
                            const auto A = Get(I), B = Get(I + 1), C = Get(I + 2);
                            Segments.emplace_back(ObjectMap{{"kind",std::string("bezier")},{"points",VariantArray{Pair(Previous),Pair(A),Pair(B),Pair(C)}}});
                            Previous = C;
                        }
                    }
                    else throw std::runtime_error("字体返回了不支持的轮廓曲线。");
                    Position += Bytes;
                }
                if (std::hypot(Previous[0] - Start[0], Previous[1] - Start[1]) > 1.0e-10) Segments.emplace_back(Line(Previous, Start));
                if (!Segments.empty()) Entities.emplace_back(ObjectMap{{"kind",std::string("path")},{"closed",true},
                    {"segments",std::move(Segments)},{"fillGroup",Group},{"fillRule",std::string("evenodd")}});
                if (Entities.size() > 5000) throw std::invalid_argument("文字轮廓超过 5000 条，请分批创建。");
            }
        }
    }

    ObjectMap GenerateSketchTextOutline(const ObjectMap& Input)
    {
        try
        {
            const auto Source = Text(Input, "text"), Family = Text(Input, "fontFamily", "Microsoft YaHei");
            if (Source.empty() || Source.size() > 32768) throw std::invalid_argument("请输入文字，且文字不能超过 32768 字节。");
            const auto WideText = Wide(Source), WideFamily = Wide(Family);
            if (WideText.size() > 2000 || WideFamily.size() >= LF_FACESIZE) throw std::invalid_argument("文字过长或字体名称无效。");
            if (WideText.find_first_of(L"\r\n\t") != std::wstring::npos) throw std::invalid_argument("一次创建一行文字，请分别创建多行。");
            const double Height = Number(Input,"height",10), Spacing = Number(Input,"letterSpacing",0), Rotation = Number(Input,"rotation",0);
            if (Height <= 0 || Height > 10000 || std::abs(Spacing) > 10000) throw std::invalid_argument("文字高度或字距超出有效范围。");
            FontContext Context;
            if (!Context.DC) throw std::runtime_error("无法创建字体上下文。");
            Context.Select(WideFamily, 1024);
            const UINT MetricSize = GetOutlineTextMetricsW(Context.DC, 0, nullptr);
            if (!MetricSize) throw std::invalid_argument("所选字体没有 TrueType 轮廓。");
            std::vector<unsigned char> MetricBuffer(MetricSize);
            auto* Metrics = reinterpret_cast<OUTLINETEXTMETRICW*>(MetricBuffer.data());
            Metrics->otmSize = MetricSize;
            if (!GetOutlineTextMetricsW(Context.DC, MetricSize, Metrics) || Metrics->otmEMSquare == 0)
                throw std::runtime_error("读取字体设计单位失败。");
            const int Em = static_cast<int>(Metrics->otmEMSquare);
            Context.Select(WideFamily, Em);
            Placement Transform{Number(Input,"x",0), Number(Input,"y",0), Height / Em, std::cos(Rotation), std::sin(Rotation)};
            std::vector<SCRIPT_ITEM> Items(WideText.size() + 2);
            int ItemCount = 0;
            if (FAILED(ScriptItemize(WideText.data(), static_cast<int>(WideText.size()), static_cast<int>(Items.size()), nullptr, nullptr, Items.data(), &ItemCount)))
                throw std::runtime_error("文字排版失败。");
            std::vector<BYTE> Levels(ItemCount);
            std::vector<int> VisualToLogical(ItemCount);
            for (int I = 0; I < ItemCount; ++I) Levels[I] = static_cast<BYTE>(Items[I].a.s.uBidiLevel);
            if (FAILED(ScriptLayout(ItemCount, Levels.data(), VisualToLogical.data(), nullptr))) throw std::runtime_error("文字方向排版失败。");
            VariantArray Entities;
            double Pen = 0;
            std::size_t GlyphSequence = 0;
            for (int Visual = 0; Visual < ItemCount; ++Visual)
            {
                const int I = VisualToLogical[Visual];
                const int Begin = Items[I].iCharPos, Length = Items[I + 1].iCharPos - Begin;
                const int MaxGlyphs = Length * 3 + 32;
                std::vector<WORD> Glyphs(MaxGlyphs), Clusters(Length);
                std::vector<SCRIPT_VISATTR> Attributes(MaxGlyphs);
                int GlyphCount = 0;
                if (FAILED(ScriptShape(Context.DC, &Context.Cache, WideText.data() + Begin, Length, MaxGlyphs, &Items[I].a,
                    Glyphs.data(), Clusters.data(), Attributes.data(), &GlyphCount))) throw std::runtime_error("所选字体无法排版这些文字。");
                SCRIPT_FONTPROPERTIES Properties{}; Properties.cBytes = sizeof(Properties);
                if (FAILED(ScriptGetFontProperties(Context.DC, &Context.Cache, &Properties))) throw std::runtime_error("读取字体属性失败。");
                for (int G = 0; G < GlyphCount; ++G)
                    if (Glyphs[G] == Properties.wgDefault
                        || (Glyphs[G] == Properties.wgInvalid && Glyphs[G] != Properties.wgBlank))
                        throw std::invalid_argument("所选字体缺少部分字形，请选择包含这些文字的字体。");
                std::vector<int> Advances(GlyphCount);
                std::vector<GOFFSET> Offsets(GlyphCount);
                ABC Width{};
                if (FAILED(ScriptPlace(Context.DC, &Context.Cache, Glyphs.data(), GlyphCount, Attributes.data(), &Items[I].a,
                    Advances.data(), Offsets.data(), &Width))) throw std::runtime_error("文字定位失败。");
                for (int G = 0; G < GlyphCount; ++G)
                {
                    AppendGlyph(Context, Glyphs[G], Pen, Offsets[G], Transform, "text-" + std::to_string(GlyphSequence++), Entities);
                    Pen += Advances[G];
                    if (G + 1 == GlyphCount || Attributes[G + 1].fClusterStart) Pen += Spacing / Transform.Scale;
                }
            }
            if (GlyphSequence) Pen -= Spacing / Transform.Scale;
            if (Entities.empty()) throw std::invalid_argument("文字没有可绘制的轮廓。");
            return {{"bOK",true},{"fontFamily",Family},{"entities",std::move(Entities)},{"advanceX",Pen * Transform.Scale}};
        }
        catch (const std::exception& Error)
        {
            return {{"bOK",false},{"message",std::string(Error.what())}};
        }
    }
}
