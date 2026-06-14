#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include <variant>
#include <unordered_map>
#include <memory>
#include <string>
#include "glShader.h"
#include "glTexture2.h"
#include "glTexture3.h"
#include "glTextureCube.h"
#include "../RenderComponent/Material.h"

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief GL 端材质参数类型
        * @details GPU-friendly，向量使用 float
        */
        using glMaterialValue = std::variant<
            float,
            int,
            glm::vec2,
            glm::vec3,
            glm::vec4,
            std::shared_ptr<glTexture2>,
            std::shared_ptr<glTexture3>,
            std::shared_ptr<glTextureCube>,
            glm::mat4
        >;

        /**
        * @brief GL 端材质
        * @details
        * glMaterial 用于 GPU 渲染，存储已经转换为 GPU-friendly 的参数和对应的 GLShader。
        */
        class glMaterial final
        {
        public:
            /**
            * @brief 初始化 GLMaterial
            * @param [in] Matertail_
            * @details
            * 将 rMaterial 中的参数转换成 GPU-friendly 类型，并创建对应 GLTexture
            */
            void Upload(IN const std::shared_ptr<rMaterial>&  pMatertail_);

            /*
            * @brief 更新
            */
            void Update();

            /**
            * @brief 获取 GLShader
            */
            std::shared_ptr<glShader> Shader() const { return m_glShader; }

            /*
            * @brief 启用
            */
            void Use() const;

        private:
            std::weak_ptr<rMaterial> m_pMaterial;
            std::shared_ptr<glShader> m_glShader;                                  //!< 对应 GLShader
            std::unordered_map<std::string, glMaterialValue> m_params;            //!< GPU-friendly 参数
            size_t m_nVersion = (size_t)-1;//!< 初始负1
        };
    } // namespace Render
} // namespace iCAX
