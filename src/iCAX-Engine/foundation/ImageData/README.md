# ImageData

`ImageData` 只定义中立图片数据结构，不提供 PNG、JPG、WebP、DDS、SVG、PDF、字体贴图等具体格式解析，也不提供缩放、采样、颜色空间转换、压缩、矢量栅格化、GPU 上传等行为。

图片导入插件可以把外部文件解码成这里的数据结构，再保存到 `Scene.Resources()`。例如图片导入可以同时生成：

- `source`：原始文件内容，通常保存为 `CBinaryResource`。
- `image.bitmap`：解码后的通用位图，通常保存为 `iCAX::ImageData::BitmapImage`。
- `image.rgba`：解码后的中立 RGBA 位图，通常保存为 `iCAX::ImageData::RGBAImage`。
- `image.vector`：解析后的中立矢量图，通常保存为 `iCAX::ImageData::VectorImage`。

位图和矢量图都属于图片资源，但二者不互相假装。位图保存像素、stride、像素格式、颜色空间；矢量图保存 viewport、path、fill、stroke、transform。矢量图需要显示成像素时，由 `ImageAlgo` 通过 `ImageAdapter` 进行栅格化。

## 目录结构

- `ImageData.h`：图片种类、像素格式、颜色空间、alpha 语义、图片原点、位图、RGBA 图片、矢量图 path/fill/stroke 等纯数据结构。
- `ImageData.vcxproj`：Visual Studio C++ 动态库项目。
