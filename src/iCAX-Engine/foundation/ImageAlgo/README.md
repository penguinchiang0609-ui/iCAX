# ImageAlgo

`ImageAlgo` 承载图片算法函数。`ImageData` 只保存数据，图片校验、融合、栅格化、滤镜、颜色空间转换、缩放等行为都应放在本项目或本项目调用的适配器中。

当前项目先提供基础像素格式信息、位图校验、RGBA 图片创建、常用 RGBA 混合模式和矢量图片栅格化入口。复杂算法可以继续扩展；需要第三方库时通过 `ImageAdapter` 接入。

## 目录结构

- `ImageAlgo.h/.cpp`：图片算法函数。
- `ImageAlgo.vcxproj`：Visual Studio C++ 动态库项目。

