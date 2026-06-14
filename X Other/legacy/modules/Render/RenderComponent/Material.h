#pragma once
#include "Render.h"
#include <variant>
#include <memory>
#include <unordered_map>
#include "Data/uuid.h"
#include "Data/Array1.h"
#include "Data/Array2.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief 材质参数类型
        * @details
        *   材质可设置不同类型的参数，支持数值、向量、颜色以及纹理类型。
        */
        enum _RENDERCOMPONENT_EXP rMaterialParamType
        {
            kMaterialBool,          
            kMaterialFloat,       //!< 单精度浮点数
            kMaterialInt,
            kMaterialVector2,     //!< 二维向量
            kMaterialVector3,     //!< 三维向量
            kMaterialVector4,     //!< 四维向量
            kMaterialColor,       //!< RGBA32 颜色
            kMaterialTexture2,   //!< 二维纹理
            kMaterialTexture3,   //!< 三维纹理
            kMaterialTextureCube,  //!< 立方体纹理
            kMaterialMatrix
        };

        /**
        * @brief 材质参数值
        * @details 使用 std::variant 存储不同类型的参数值。
        */
        using rMaterialValue = std::variant<
            bool,                                       //!< 布尔
            float,                                      //!< 浮点数
            int,                                        //!< 整数
            iCAX::Data::Real2,                          //!< 二维向量
            iCAX::Data::Real3,                          //!< 三维向量
            iCAX::Data::Real4,                          //!< 四维向量
            iCAX::Data::Real4x4,                        //!< 矩阵变量
            std::string                                 //!< 资源ID
        >;

        /**
        * @brief 材质参数
        * @details
        *   封装单个材质参数，包括参数类型和对应值。
        */
        struct MaterialParam
        {
            rMaterialParamType nType;  //!< 参数类型
            rMaterialValue     Value;  //!< 参数值
        };

        /**
        * @class rMaterial
        * @brief 渲染材质
        * @details
        *   rMaterial 用于描述物体的表面属性，可以绑定 Shader 并设置各种参数，包括颜色、纹理、数值等。
        */
        struct _RENDERCOMPONENT_EXP rMaterial final
        {
            std::string ShaderID;                         //!< 材质绑定的 Shader
            std::unordered_map<std::string, MaterialParam> mapParams; //!< 参数表，key 为参数名
            int nVersion = 0;//!< 版本号，初始为零。每次修改都需要加一，调用者需要注意
        };
    }
}
