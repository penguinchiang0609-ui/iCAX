# ImageAdapter

`ImageAdapter` 定义外部图像算法库适配接口。它用于把 `ImageData` 下沉到 OpenCV、Skia、FreeImage、libvips、Direct2D 或其他图像库，再把计算结果转换回 `ImageData`。

本项目只定义接口，不保存图片数据，也不绑定任何第三方库。

## 目录结构

- `ImageAdapter.h`：图片解码、编码、矢量栅格化等外部内核适配接口。
- `ImageAdapter.vcxproj`：Visual Studio C++ 动态库项目。

