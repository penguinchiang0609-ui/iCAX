//#pragma once
//#include "Render.h"
//#include <string>
//#include <unordered_map>
//#include "Texture2.h"
//#include "../Math/Vector2.h"
//#include "../Math/Vector3.h"
//#include "../Math/Point3.h"
//#include "../Math/RGBA32.h"
//
//using namespace iCAX::Data;
//using namespace iCAX::Math;
//
//namespace iCAX
//{
//    namespace Render
//    {
//        /**
//        * @struct rGlyph
//        * @brief 单个字符信息
//        * @details
//        * 用于文本渲染，包含字符在字体纹理集中的位置、大小及偏移信息。
//        */
//        struct _RENDERCOMPONENT_EXP rGlyph
//        {
//            char character;        //!< 字符
//            double advance;        //!< 光标移动距离（到下一个字符起点的水平偏移）
//            Vector2 size;          //!< 字符矩形尺寸（宽度、高度）
//            Vector2 bearing;       //!< 字符起点偏移（相对于基线的左上角）
//            Vector2 uvTopLeft;     //!< 纹理坐标左上角（归一化0~1）
//            Vector2 uvBottomRight; //!< 纹理坐标右下角（归一化0~1）
//        };
//
//        /**
//        * @struct rFont
//        * @brief 字体信息
//        * @details
//        * 用于管理字体及字符映射，用于渲染文字。
//        * 字体包含字符纹理集（atlas）和每个字符的排版信息。
//        */
//        struct _RENDERCOMPONENT_EXP rFont
//        {
//            std::string name;                           //!< 字体名称，例如 "Arial"
//            double size;                                //!< 字体大小（点数或像素）
//            std::unordered_map<char, rGlyph> glyphs;   //!< 字符映射表，用于查找每个字符的排版信息
//            rTexture2 atlas;                            //!< 字符纹理集，存储所有字符的纹理
//        };
//
//        /**
//        * @struct rRenderText
//        * @brief 可渲染文本对象
//        * @details
//        * 包含文字内容、字体、位置、颜色和缩放信息。
//        * 用于渲染三维场景中的文字，支持位置、缩放和颜色设置。
//        */
//        struct _RENDERCOMPONENT_EXP rRenderText
//        {
//            std::string strFontFile;            //!< 字体文件
//            std::shared_ptr<rFont> pFont;       //!< 使用的字体，懒延迟加载
//            std::string strText;                //!< 文本内容
//            Point3 Position;                    //!< 三维空间位置
//            RGBA32 Color;                       //!< 文字颜色
//            double nScale;                      //!< 缩放比例（默认 1.0）
//        };
//    }
//}
