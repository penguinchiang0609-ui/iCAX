#include "pch.h"
#include "glTextureCube.h"
#include "glShader.h"

//!< 构造函数
iCAX::Render::glTextureCube::glTextureCube() : m_nTextureID(0)
{
}

//!< 析构函数
iCAX::Render::glTextureCube::~glTextureCube()
{
    if (m_nTextureID) glDeleteTextures(1, &m_nTextureID);
}

//!< 初始化
bool iCAX::Render::glTextureCube::Upload(IN const rTextureCube& Tex_)
{
    if (Tex_.nSize <= 0 || Tex_.nChannels <= 0)
        return false;

    if (!m_nTextureID)
        glGenTextures(1, &m_nTextureID);

    glBindTexture(GL_TEXTURE_CUBE_MAP, m_nTextureID);

    // 默认纹理参数
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    GLenum _nFormat = GL_RGBA;
    if (Tex_.nChannels == 1) _nFormat = GL_RED;
    else if (Tex_.nChannels == 3) _nFormat = GL_RGB;

    // 上传 6 个面
    for (int i = 0; i < 6; ++i)
    {
        const unsigned char* pData = Tex_.vecFaces[i].data();
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, _nFormat, Tex_.nSize, Tex_.nSize, 0, _nFormat, GL_UNSIGNED_BYTE, pData);
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return true;
}

//!< 绑定到指定寄存器
void iCAX::Render::glTextureCube::Bind(IN const GLuint& _nIndex) const
{
    glActiveTexture(GL_TEXTURE0 + _nIndex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_nTextureID);
}

//!< 从寄存器解绑
void iCAX::Render::glTextureCube::Unbind(IN const GLuint& _nIndex) const
{
    glActiveTexture(GL_TEXTURE0 + _nIndex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void iCAX::Render::glTextureCube::BindToShader(IN const std::string& paramName, IN glShader& shader, IN const GLuint& nIndex_) const
{
    // 绑定纹理
    glActiveTexture(GL_TEXTURE0 + nIndex_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, nIndex_);

    // 获取纹理在着色器中的 sampler 位置
    GLint samplerLocation = shader.GetUniformLocation(paramName);
    if (samplerLocation != -1)
    {
        // 设置该 sampler 的值为绑定的纹理单元
        shader.SetUniform(paramName, (int)nIndex_);
    }
    else
    {
        // 错误处理：找不到该参数
        std::cerr << "Error: Cannot find sampler uniform " << paramName << " in shader." << std::endl;
    }
}