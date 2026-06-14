#pragma once
#include "Render.h"
#include <string>
#include <vector>
#include <functional>

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief 着色器类型
        */
        enum _RENDERCOMPONENT_EXP rShaderType
        {
            kShaderVertex = 0,      //!< 顶点着色器
            kShaderFragment,    //!< 片元着色器
            //!< 以下三个此阶段没必要支持，对于CADCAM软件意义不大，后续看需求，如果做CAM仿真对效果要求更高再议
            //kShaderGeometry,    //!< 几何着色器
            //kShaderTessControl,     //!< 细分控制着色器（Tessellation Control Shader）
            //kShaderTessEvaluation,   //!< 细分评估着色器（Tessellation Evaluation Shader）
            kShaderCompute      //!< 计算着色器
        };

        /*
        * @brief 单个子着色器模块
        */
        struct _RENDERCOMPONENT_EXP rSubShader final
        {
            rShaderType nType = kShaderVertex;
            std::string strSrc = "";   //!< GLSL/HLSL 源码
        };

        /*
        * @brief 一个完整的着色器程序（多个子模块组合）
        */
        struct _RENDERCOMPONENT_EXP rShader final
        {
            std::vector<rSubShader> vecModules;
            //!< 暂不支持动态修改，后续如果需要再打开（CADCAM应该不需要此功能，除非集成Shader编辑器）
            //int nVersion = 0;//!< 版本号，初始为零。每次修改都需要加一，调用者需要注意
        };
    }
}
