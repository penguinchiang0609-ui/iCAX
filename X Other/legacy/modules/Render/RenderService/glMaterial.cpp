#include "pch.h"
#include "glMaterial.h"
#include "Services/ServiceProvider.h"
#include "CoreService/IResourceService.h"

void iCAX::Render::glMaterial::Upload(IN const std::shared_ptr<rMaterial>& pMatertail_)
{
    m_pMaterial = pMatertail_;
    Update();
}

//!< 刷新
void iCAX::Render::glMaterial::Update()
{
    auto _pResource = iCAX::Services::GetGlobalServiceProvider()->Resolve<iCAX::Core::IResourceService>();
    if (!m_pMaterial.expired() || (m_pMaterial.lock()->nVersion ==  m_nVersion))
        return;

    const auto& Matertail_ = *m_pMaterial.lock();

    m_params.clear();

    std::shared_ptr<rShader> _pShader = _pResource->Get<rShader>(Matertail_.ShaderID);
    std::shared_ptr<glShader> _pGLShader = _pResource->Get<glShader>(Matertail_.ShaderID);
    // 处理 Shader
    if (_pGLShader == nullptr && _pShader != nullptr)
    {
        m_glShader = std::make_shared<glShader>();
        m_glShader->InitFromRShader(*_pShader);
        _pResource->Set(Matertail_.ShaderID, m_glShader);
    }

    // 遍历 rMaterial 参数
    for (const auto& [name, param] : Matertail_.mapParams)
    {
        switch (param.nType)
        {
        case kMaterialFloat:
            m_params[name] = std::get<float>(param.Value);
            break;
        case kMaterialInt:
            m_params[name] = std::get<int>(param.Value);
            break;
        case kMaterialVector2:
        {
            const auto& v = std::get<iCAX::Data::Real2>(param.Value);
            m_params[name] = glm::vec2(v[0], v[1]);
            break;
        }
        case kMaterialVector3:
        {
            const auto& v = std::get<iCAX::Data::Real3>(param.Value);
            m_params[name] = glm::vec3(v[0], v[1], v[2]);
            break;
        }
        case kMaterialVector4:
        {
            const auto& v = std::get<iCAX::Data::Real4>(param.Value);
            m_params[name] = glm::vec4(v[0], v[1], v[2], v[3]);
            break;
        }
        case kMaterialColor:
        {
            const auto& v = std::get<iCAX::Data::Real4>(param.Value);
            m_params[name] = glm::vec4(v[0], v[1], v[2], v[3]);
            break;
        }
        case kMaterialTexture2:
        {
            const auto& _ID = std::get<std::string>(param.Value);
            std::shared_ptr<rTexture2> _pTex = _pResource->Get<rTexture2>(_ID);
            std::shared_ptr<glTexture2> _pGLTex = _pResource->Get<glTexture2>(_ID);
            if (_pGLTex == nullptr && _pTex != nullptr)
            {
                auto glTex = std::make_shared<glTexture2>();
                glTex->Upload(*_pTex);
                _pResource->Set(_ID, glTex);
                m_params[name] = glTex;
            }
            break;
        }
        case kMaterialTexture3:
        {
            const auto& _ID = std::get<std::string>(param.Value);
            std::shared_ptr<rTexture3> _pTex = _pResource->Get<rTexture3>(_ID);
            std::shared_ptr<glTexture3> _pGLTex = _pResource->Get<glTexture3>(_ID);
            if (_pGLTex == nullptr && _pTex != nullptr)
            {
                auto glTex = std::make_shared<glTexture3>();
                glTex->Upload(*_pTex);
                _pResource->Set(_ID, glTex);
                m_params[name] = glTex;
            }
            break;
        }
        case kMaterialTextureCube:
        {
            const auto& _ID = std::get<std::string>(param.Value);
            std::shared_ptr<rTextureCube> _pTex = _pResource->Get<rTextureCube>(_ID);
            std::shared_ptr<glTextureCube> _pGLTex = _pResource->Get<glTextureCube>(_ID);
            if (_pGLTex == nullptr && _pTex != nullptr)
            {
                auto glTex = std::make_shared<glTextureCube>();
                glTex->Upload(*_pTex);
                _pResource->Set(_ID, glTex);
                m_params[name] = glTex;
            }
            break;
        }
        case kMaterialMatrix:
        {
            const auto& _matrix = std::get<iCAX::Data::Real4x4>(param.Value);
            m_params[name] = glm::mat4(
                _matrix(0, 0), _matrix(0, 1), _matrix(0, 2), _matrix(0, 3),
                _matrix(1, 0), _matrix(1, 1), _matrix(1, 2), _matrix(1, 3),
                _matrix(2, 0), _matrix(2, 1), _matrix(2, 2), _matrix(2, 3),
                _matrix(3, 0), _matrix(3, 1), _matrix(3, 2), _matrix(3, 3));
            break;
        }
        case kMaterialBool:
        {
            m_params[name] = std::get<bool>(param.Value) ? 1 : 0;
            break;
        }
        default:
            break;
        }
    }

    m_nVersion = Matertail_.nVersion;

}

//!< 使用
void iCAX::Render::glMaterial::Use() const
{
    m_glShader->Use();

    // 设置材质的参数
    GLuint textureIndex = 0;
    for (const auto& param : m_params)
    {
        const auto& paramName = param.first;
        const auto& paramValue = param.second;

        if (std::holds_alternative<float>(paramValue))
        {
            m_glShader->SetUniform(paramName, std::get<float>(paramValue));
        }
        else if (std::holds_alternative<int>(paramValue))
        {
            m_glShader->SetUniform(paramName, std::get<int>(paramValue));
        }
        else if (std::holds_alternative<glm::vec3>(paramValue))
        {
            m_glShader->SetUniform(paramName, std::get<glm::vec3>(paramValue));
        }
        else if (std::holds_alternative<glm::vec4>(paramValue))
        {
            m_glShader->SetUniform(paramName, std::get<glm::vec4>(paramValue));
        }
        else if (std::holds_alternative<glm::mat4>(paramValue))
        {
            m_glShader->SetUniform(paramName, std::get<glm::mat4>(paramValue));
        }
        else if (std::holds_alternative<std::shared_ptr<glTexture2>>(paramValue))
        {
            auto texture = std::get<std::shared_ptr<glTexture2>>(paramValue);
            texture->BindToShader(paramName, *m_glShader, textureIndex);
            textureIndex++;
        }
        else if (std::holds_alternative<std::shared_ptr<glTexture3>>(paramValue))
        {
            auto texture = std::get<std::shared_ptr<glTexture3>>(paramValue);
            texture->BindToShader(paramName, *m_glShader, textureIndex);
            textureIndex++;
        }
        else if (std::holds_alternative<std::shared_ptr<glTextureCube>>(paramValue))
        {
            auto texture = std::get<std::shared_ptr<glTextureCube>>(paramValue);
            texture->BindToShader(paramName, *m_glShader, textureIndex);

            textureIndex++;
        }
        // 其他类型参数处理...
    }
}
