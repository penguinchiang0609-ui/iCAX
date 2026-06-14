#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>
#include <iostream>
#include "../RenderComponent/Shader.h"

namespace iCAX
{
    namespace Render
    {
        /**
        * @class glShader
        * @brief OpenGL 着色器封装类
        * @details
        * 该类用于将上层的 rShader 数据结构转换为 OpenGL 可用的 Program，
        * 并提供 uniform 设置接口。支持多模块组合（顶点、片段、几何、计算着色器）。
        *
        * 使用示例：
        * @code
        * rShader myShader;
        * // 添加子模块...
        * glShader glShader;
        * glShader.InitFromRShader(myShader);
        * glUseProgram(glShader.ProgramID());
        * glShader.SetUniform("uColor", glm::vec3(1.0f, 0.0f, 0.0f));
        * @endcode
        */
        class glShader
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 初始化 m_programID 为 0
            */
            glShader() : m_programID(0) {}

            /**
            * @brief 析构函数
            * @details 如果 m_programID 非 0，则删除 OpenGL Program
            */
            ~glShader() { if (m_programID) glDeleteProgram(m_programID); }

            void Use() const;
            /**
            * @brief 根据 rShader 初始化 GLShader
            * @param [in] shader rShader 对象
            * @return bool 初始化是否成功
            * @details
            * 遍历 rShader 的子模块，编译每个子着色器，
            * 并链接成 OpenGL Program。编译或链接失败会输出错误信息。
            */
            bool InitFromRShader(const rShader& shader);

            /**
            * @brief 获取 OpenGL Program ID
            * @return GLuint OpenGL Program ID
            */
            GLuint ProgramID() const { return m_programID; }

            /**
            * @brief 获取 uniform location
            * @param [in] name uniform 名称
            * @return GLint uniform location，如果未找到返回 -1
            * @details
            * 会在内部缓存 uniform location，提高访问效率。
            * 如果 uniform 在 Program 中不存在，会打印警告。
            */
            GLint GetUniformLocation(const std::string& name) const;

            // ---------------------------
            // Uniform 设置接口
            // ---------------------------

            /**
            * @brief 设置 bool 类型 uniform
            * @param [in] name uniform 名称
            * @param [in] value bool 值
            */
            void SetUniform(const std::string& name, bool value) const;

            /**
            * @brief 设置 int 类型 uniform
            * @param [in] name uniform 名称
            * @param [in] value int 值
            */
            void SetUniform(const std::string& name, int value) const;

            /**
            * @brief 设置 float 类型 uniform
            * @param [in] name uniform 名称
            * @param [in] value float 值
            */
            void SetUniform(const std::string& name, float value) const;

            /**
            * @brief 设置 vec2 类型 uniform
            * @param [in] name uniform 名称
            * @param [in] value glm::vec2 值
            */
            void SetUniform(const std::string& name, const glm::vec2& value) const;

            /**
            * @brief 设置 vec3 类型 uniform
            * @param [in] name uniform 名称
            * @param [in] value glm::vec3 值
            */
            void SetUniform(const std::string& name, const glm::vec3& value) const;

            /**
            * @brief 设置 vec4 类型 uniform
            * @param [in] name uniform 名称
            * @param [in] value glm::vec4 值
            */
            void SetUniform(const std::string& name, const glm::vec4& value) const;

            /**
            * @brief 设置 mat3 类型 uniform
            * @param [in] name uniform 名称
            * @param [in] value glm::mat3 值
            */
            void SetUniform(const std::string& name, const glm::mat3& value) const;

            /**
            * @brief 设置 mat4 类型 uniform
            * @param [in] name uniform 名称
            * @param [in] value glm::mat4 值
            */
            void SetUniform(const std::string& name, const glm::mat4& value) const;

            //!< 暂不支持Shader的版本号，CADCAM程序不需要支持动态修改Shader代码（游戏引擎也不需要，只有专业材质软件才会有这个需求）
            ///*
            //* @brief 获取版本号
            //*/
            //int Version();
        private:
            GLuint m_programID;  //!< OpenGL Program ID
            /**
            * @brief uniform location 缓存
            * @details 减少重复调用 glGetUniformLocation
            */
            mutable std::unordered_map<std::string, GLint> m_uniformLocationCache;

            /**
            * @brief 编译单个着色器
            * @param [in] type GL_VERTEX_SHADER / GL_FRAGMENT_SHADER / GL_GEOMETRY_SHADER / GL_COMPUTE_SHADER
            * @param [in] source GLSL 源码字符串
            * @return GLuint 编译后的 shader ID，如果失败返回 0
            * @details
            * 输出编译错误信息到 std::cerr
            */
            GLuint CompileShader(GLenum type, const std::string& source);
        };
    }
}
