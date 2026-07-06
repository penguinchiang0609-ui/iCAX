# FontAlgo

`FontAlgo` 承载字体算法函数。`FontData` 只保存数据，字体校验、简单度量、文本 shaping、字形栅格化、轮廓转换等行为应放在本项目或本项目调用的适配器中。

当前项目先提供基础校验、简单字形查找、简单左到右文本排布和度量。复杂文本 shaping、连字、复杂脚本、hinting 和高质量栅格化必须通过 `FontAdapter` 接入 FreeType、HarfBuzz、DirectWrite 等成熟库。

## 目录结构

- `FontAlgo.h/.cpp`：字体算法函数。
- `FontAlgo.vcxproj`：Visual Studio C++ 动态库项目。

