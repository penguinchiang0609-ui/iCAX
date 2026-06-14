#include "pch.h"
#include "glShader.h"

void iCAX::Render::glShader::Use() const
{
    glUseProgram(m_programID);
}

bool iCAX::Render::glShader::InitFromRShader(const rShader& shader)
{
    std::vector<GLuint> compiledShaders;
    for (const auto& module : shader.vecModules)
    {
        GLenum glType = GL_VERTEX_SHADER;
        switch (module.nType)
        {
        case kShaderVertex: glType = GL_VERTEX_SHADER; break;
        case kShaderFragment: glType = GL_FRAGMENT_SHADER; break;
        case kShaderCompute: glType = GL_COMPUTE_SHADER; break;
        default: break;
        }

        GLuint shaderID = CompileShader(glType, module.strSrc);
        if (!shaderID)
        {
            for (auto s : compiledShaders)
                glDeleteShader(s);
            return false;
        }
        compiledShaders.push_back(shaderID);
    }

    m_programID = glCreateProgram();
    for (auto s : compiledShaders)
        glAttachShader(m_programID, s);

    glLinkProgram(m_programID);

    GLint isLinked = 0;
    glGetProgramiv(m_programID, GL_LINK_STATUS, &isLinked);
    if (!isLinked)
    {
        GLint maxLength = 0;
        glGetProgramiv(m_programID, GL_INFO_LOG_LENGTH, &maxLength);
        std::string infoLog(maxLength, ' ');
        glGetProgramInfoLog(m_programID, maxLength, &maxLength, &infoLog[0]);
        std::cerr << "Shader link error: " << infoLog << std::endl;

        glDeleteProgram(m_programID);
        m_programID = 0;
        for (auto s : compiledShaders)
            glDeleteShader(s);
        return false;
    }

    // 链接成功后删除单个 Shader
    for (auto s : compiledShaders)
    {
        glDetachShader(m_programID, s);
        glDeleteShader(s);
    }

    return true;
}

GLuint iCAX::Render::glShader::CompileShader(GLenum type, const std::string& source)
{
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (!isCompiled)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
        std::string infoLog(maxLength, ' ');
        glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);
        std::cerr << (type == GL_VERTEX_SHADER ? "Vertex" :
            type == GL_FRAGMENT_SHADER ? "Fragment" :
            type == GL_GEOMETRY_SHADER ? "Geometry" : "Compute")
            << " shader compile error: " << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLint iCAX::Render::glShader::GetUniformLocation(const std::string& name) const
{
    if (m_uniformLocationCache.count(name))
        return m_uniformLocationCache[name];

    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location == -1)
        std::cerr << "Warning: uniform '" << name << "' not found in shader." << std::endl;

    m_uniformLocationCache[name] = location;
    return location;
}

void iCAX::Render::glShader::SetUniform(const std::string& name, bool value) const
{
    glUniform1i(GetUniformLocation(name), value ? 1 : 0);  // 传递 bool 转换为 int (0 或 1)
}

void iCAX::Render::glShader::SetUniform(const std::string& name, int value) const
{
    glUniform1i(GetUniformLocation(name), value);
}

void iCAX::Render::glShader::SetUniform(const std::string& name, float value) const
{
    glUniform1f(GetUniformLocation(name), value);
}

void iCAX::Render::glShader::SetUniform(const std::string& name, const glm::vec2& value) const
{
    glUniform2fv(GetUniformLocation(name), 1, &value[0]);
}

void iCAX::Render::glShader::SetUniform(const std::string& name, const glm::vec3& value) const
{
    glUniform3fv(GetUniformLocation(name), 1, &value[0]);
}

void iCAX::Render::glShader::SetUniform(const std::string& name, const glm::vec4& value) const
{
    glUniform4fv(GetUniformLocation(name), 1, &value[0]);
}

void iCAX::Render::glShader::SetUniform(const std::string& name, const glm::mat3& value) const
{
    glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void iCAX::Render::glShader::SetUniform(const std::string& name, const glm::mat4& value) const
{
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}
