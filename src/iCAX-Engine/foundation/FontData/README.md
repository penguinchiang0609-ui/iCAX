# FontData

`FontData` 只定义中立字体数据结构，不提供 TTF、OTF、TTC、WOFF 等格式解析，也不提供复杂文本 shaping、字形轮廓提取、hinting、栅格化等行为。

字体资源通常分两层保存：

- `source`：原始字体文件内容，通常保存为 `CBinaryResource`。
- `font.face`：解析后的中立字体描述，保存为 `iCAX::FontData::FontFace`。

字体字形分为两类：

- 矢量字形：保存为 `GlyphOutline`，轮廓复用 `GeometryData::Loop2`。
- 点阵字形：保存为 `GlyphBitmap`，位图复用 `ImageData::BitmapImage`。

## 目录结构

- `FontData.h`：字体面、字形、字形度量、文本 run、shaping 输出等纯数据结构。
- `FontData.vcxproj`：Visual Studio C++ 动态库项目。

