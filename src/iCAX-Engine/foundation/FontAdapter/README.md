# FontAdapter

`FontAdapter` 定义外部字体内核适配接口。它用于把 `FontData` 下沉到 FreeType、HarfBuzz、DirectWrite 或其他字体库，再把解析、shaping、轮廓提取、栅格化结果转换回 `FontData`。

本项目只定义接口，不保存字体数据，也不绑定任何第三方库。

## 目录结构

- `FontAdapter.h`：字体加载、字形轮廓、字形点阵、文本 shaping 等外部内核适配接口。
- `FontAdapter.vcxproj`：Visual Studio C++ 动态库项目。

