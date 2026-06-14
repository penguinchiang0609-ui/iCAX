#include "pch.h"
#include "glMesh.h"

//!< 构造函数
iCAX::Render::glMesh::glMesh()
    : m_vao(0)
    , m_vbo(0)
    , m_ebo(0)
    , m_normalsVbo(0)
    , m_colorsVbo(0)
    , m_uvsVbo(0)
    , m_tangentsVbo(0)
    , m_nIndicesMode(kIndicesTriangles)
    , m_indicesCount(0)
{
}

//!< 析构函数
iCAX::Render::glMesh::~glMesh()
{
    Cleanup();
}

//!< 初始化 GLMesh
void iCAX::Render::glMesh::Upload(IN const std::shared_ptr<rMesh>& pMesh_)
{
    m_pMesh = pMesh_;
    Update();
}

//!< 刷新
void iCAX::Render::glMesh::Update()
{
    if (m_pMesh.expired() || (m_pMesh.lock()->nVersion == m_nVersion))
        return;

    Cleanup();
    const rMesh& _Mesh  = *m_pMesh.lock();
    m_nIndicesMode = _Mesh.nIndicesMode;

    // 生成 VAO、VBO、EBO
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // 顶点缓冲区（VBO）
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, _Mesh.vecVertices.size() * sizeof(Real3), _Mesh.vecVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Real3), (void*)0); // 顶点位置
    glEnableVertexAttribArray(0);

    // 法向缓冲区
    if (!_Mesh.vecNormals.empty())
    {
        glGenBuffers(1, &m_normalsVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_normalsVbo);
        glBufferData(GL_ARRAY_BUFFER, _Mesh.vecNormals.size() * sizeof(Real3), _Mesh.vecNormals.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Real3), (void*)0); // 法向
        glEnableVertexAttribArray(1);
    }

    // 颜色缓冲区
    if (!_Mesh.vecColors.empty())
    {
        glGenBuffers(1, &m_colorsVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_colorsVbo);
        glBufferData(GL_ARRAY_BUFFER, _Mesh.vecColors.size() * sizeof(Real4), _Mesh.vecColors.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Real4), (void*)0); // 顶点颜色
        glEnableVertexAttribArray(2);
    }

    // UV 缓冲区
    if (!_Mesh.vecUVs.empty())
    {
        glGenBuffers(1, &m_uvsVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_uvsVbo);
        glBufferData(GL_ARRAY_BUFFER, _Mesh.vecUVs.size() * sizeof(Real2), _Mesh.vecUVs.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Real2), (void*)0); // UV坐标
        glEnableVertexAttribArray(3);
    }

    // 切线缓冲区
    if (!_Mesh.vecTangents.empty())
    {
        glGenBuffers(1, &m_tangentsVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_tangentsVbo);
        glBufferData(GL_ARRAY_BUFFER, _Mesh.vecTangents.size() * sizeof(Real4), _Mesh.vecTangents.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Real4), (void*)0); // 切线
        glEnableVertexAttribArray(4);
    }

    // 索引缓冲区（EBO）
    if (!_Mesh.vecIndices.empty())
    {
        glGenBuffers(1, &m_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, _Mesh.vecIndices.size() * sizeof(int), _Mesh.vecIndices.data(), GL_STATIC_DRAW);
        m_indicesCount =(GLsizei)_Mesh.vecIndices.size();  // 更新索引计数
    }

    // 解绑 VAO
    glBindVertexArray(0);

    m_nVersion = _Mesh.nVersion;
}

//!< 绘制网格
void iCAX::Render::glMesh::Draw() const
{
    glBindVertexArray(m_vao);
    switch (m_nIndicesMode)
    {
    case kIndicesTriangles:
        glDrawElements(GL_TRIANGLES, m_indicesCount, GL_UNSIGNED_INT, 0);
        break;
    case kIndicesLines:
        glDrawElements(GL_LINES, m_indicesCount, GL_UNSIGNED_INT, 0);
        break;
    case kIndicesPoints:
        glDrawElements(GL_POINTS, m_indicesCount, GL_UNSIGNED_INT, 0);
        break;
    default:
        break;
    }
    glBindVertexArray(0);
}

//!< 清理资源
void iCAX::Render::glMesh::Cleanup()
{
    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo)
    {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_normalsVbo)
    {
        glDeleteBuffers(1, &m_normalsVbo);
        m_normalsVbo = 0;
    }
    if (m_colorsVbo)
    {
        glDeleteBuffers(1, &m_colorsVbo);
        m_colorsVbo = 0;
    }
    if (m_uvsVbo)
    {
        glDeleteBuffers(1, &m_uvsVbo);
        m_uvsVbo = 0;
    }
    if (m_tangentsVbo)
    {
        glDeleteBuffers(1, &m_tangentsVbo);
        m_tangentsVbo = 0;
    }
    if (m_instanceVbo)
    {
        glDeleteBuffers(1, &m_instanceVbo);
        m_instanceVbo = 0;
    }
}

void iCAX::Render::glMesh::DrawInstanced(/*GLsizei instanceCount*/) const
{
    if (m_vao == 0 || m_indicesCount <= 0)
        return;

    glBindVertexArray(m_vao);

    switch (m_nIndicesMode)
    {
    case kIndicesTriangles:
        glDrawElementsInstanced(GL_TRIANGLES, m_indicesCount, GL_UNSIGNED_INT, 0, m_instanceCount);
        break;
    case kIndicesLines:
        glDrawElementsInstanced(GL_LINES, m_indicesCount, GL_UNSIGNED_INT, 0, m_instanceCount);
        break;
    case kIndicesPoints:
        glDrawElementsInstanced(GL_POINTS, m_indicesCount, GL_UNSIGNED_INT, 0, m_instanceCount);
        break;
    default:
        break;
    }

    glBindVertexArray(0);
}

void iCAX::Render::glMesh::UploadInstances(const std::vector<glm::mat4>& matrices)
{
    // 若还没初始化 VAO/VBO，则忽略
    if (m_vao == 0 || matrices.empty())
        return;

    if (m_instanceVbo && !m_bInstanceDirty)
    {
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, matrices.size() * sizeof(glm::mat4), matrices.data());
        glBindVertexArray(0);
        m_instanceCount = (GLuint)matrices.size();
    }
    else
    {
        // 如果之前已经存在实例缓冲，删除
        if (m_instanceVbo)
        {
            glDeleteBuffers(1, &m_instanceVbo);
            m_instanceVbo = 0;
        }

        // 生成实例缓冲对象
        glGenBuffers(1, &m_instanceVbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        glBufferData(GL_ARRAY_BUFFER, matrices.size() * sizeof(glm::mat4), matrices.data(), GL_DYNAMIC_DRAW);

        // 设置4个顶点属性位置，分别对应mat4的每一列
        std::size_t vec4Size = sizeof(glm::vec4);
        for (GLuint i = 0; i < 4; i++)
        {
            GLuint attribLocation = 5 + i; // 5~8 保留给 instance matrix
            glEnableVertexAttribArray(attribLocation);
            glVertexAttribPointer(attribLocation, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * vec4Size));
            glVertexAttribDivisor(attribLocation, 1); // 每个实例使用一个矩阵
        }

        glBindVertexArray(0);
    }
    m_instanceCount = (int)matrices.size();
}

//!< Instance是否被污染
bool& iCAX::Render::glMesh::InstanceDirty() const
{
    return m_bInstanceDirty;
}

