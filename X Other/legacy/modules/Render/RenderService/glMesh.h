#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include <vector>
#include "Math/Point3.h"
#include "Math/Dir3.h"
#include "Math/RGBA32.h"
#include "Data/uuid.h"
#include "Data/Array1.h"
#include "../RenderComponent/Mesh.h"
#include <glm/ext/matrix_float4x4.hpp>

using namespace iCAX::Data;
using namespace iCAX::Math;

namespace iCAX
{
    namespace Render
    {
        /**
        * @class glMesh
        * @brief OpenGL 网格封装类
        * @details
        * glMesh 负责将 rMesh 中的几何数据传递给 OpenGL 渲染。
        * 包括顶点数据、法线数据、UV、切线等，并提供渲染接口。
        */
        class glMesh final
        {
        public:
            /*
            * @brief 构造函数
            */
            glMesh();

            /*
            * @brief 析构函数
            */
            ~glMesh();

        public:
            /**
            * @brief 初始化 GLMesh
            * @param mesh 传入的 rMesh 数据
            */
            void Upload(IN const std::shared_ptr<rMesh>& pMesh_);

            /*
            * @brief 更新
            */
            void Update();

            /**
            * @brief 绘制网格
            */
            void Draw() const;

            /**
            * @brief 清理资源
            */
            void Cleanup();

            /*
            * @brief 获取版本号
            * @return int
            */
            size_t Version()
            {
                return m_nVersion;
            }

            //!< 以下代码还未启用，需要shader侧同步改造
        public:
            /**
            * @brief 绘制网格（实例化方式）
            * @param instanceCount 实例数量
            */
            void DrawInstanced(/*GLsizei instanceCount*/) const;

            /**
            * @brief 上传实例矩阵数据
            * @param matrices 模型矩阵数组
            */
            void UploadInstances(const std::vector<glm::mat4>& matrices);

            /*
            * @brief Instance是否被污染
            * @return bool&
            */
            bool& InstanceDirty() const;
        private:
            std::weak_ptr<rMesh> m_pMesh;
            GLuint m_vao; // 顶点数组对象
            GLuint m_vbo; // 顶点缓冲对象
            GLuint m_ebo; // 索引缓冲对象
            GLuint m_normalsVbo; // 法向量缓冲
            GLuint m_colorsVbo; // 顶点颜色缓冲
            GLuint m_uvsVbo; // UV坐标缓冲
            GLuint m_tangentsVbo; // 切线缓冲
            cIndicesMode m_nIndicesMode; // 索引模式
            GLsizei m_indicesCount; // 索引的数量
            size_t m_nVersion = (size_t)-1;//!< 初始负1

            GLuint m_instanceVbo = 0;  //!< 用于实例矩阵
            GLuint m_instanceCount = 0;
            mutable bool m_bInstanceDirty = false;
        };
    }
}
