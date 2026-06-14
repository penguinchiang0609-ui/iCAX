#include "pch.h"
#include "glTexture3.h"
#include "glShader.h"

//!< 构造函数
iCAX::Render::glTexture3::glTexture3() : m_nTextureID(0)
{
}

//!< 析构函数
iCAX::Render::glTexture3::~glTexture3()
{
    if (m_nTextureID) glDeleteTextures(1, &m_nTextureID);
}

//!< 绑定到纹理rTexture3
bool iCAX::Render::glTexture3::Upload(IN const rTexture3& Tex_)
{
    if (Tex_.nWidth <= 0 || Tex_.nHeight <= 0 || Tex_.nDepth <= 0 || Tex_.nChannels <= 0)
        return false;

    if (!m_nTextureID)
        glGenTextures(1, &m_nTextureID);

    glBindTexture(GL_TEXTURE_3D, m_nTextureID);

    // 默认纹理参数
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);

    GLenum _nFormat = GL_RGBA;
    if (Tex_.nChannels == 1) _nFormat = GL_RED;
    else if (Tex_.nChannels == 3) _nFormat = GL_RGB;

    glTexImage3D(GL_TEXTURE_3D, 0, _nFormat, Tex_.nWidth, Tex_.nHeight, Tex_.nDepth, 0, _nFormat, GL_UNSIGNED_BYTE, Tex_.vecData.data());

    glBindTexture(GL_TEXTURE_3D, 0);
    return true;
}

//!< 绑定到指定寄存器
void iCAX::Render::glTexture3::Bind(IN const GLuint& nIndex_) const
{
    glActiveTexture(GL_TEXTURE0 + nIndex_);
    glBindTexture(GL_TEXTURE_3D, m_nTextureID);
}

//!< 从寄存器解绑
void iCAX::Render::glTexture3::Unbind(IN const GLuint& nIndex_) const
{
    glActiveTexture(GL_TEXTURE0 + nIndex_);
    glBindTexture(GL_TEXTURE_3D, 0);
}

void iCAX::Render::glTexture3::BindToShader(IN const std::string& paramName, IN glShader& shader, IN const GLuint& nIndex_) const
{
    // 绑定纹理
    glActiveTexture(GL_TEXTURE0 + nIndex_);
    glBindTexture(GL_TEXTURE_3D, nIndex_);

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
