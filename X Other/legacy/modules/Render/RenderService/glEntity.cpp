#include "pch.h"
#include "glEntity.h"
#include "Services/ServiceProvider.h"
#include "CoreService/IResourceService.h"


//! 更新
void iCAX::Render::glEntity::Update(IN const TransformComponent& Transform_, IN const LayerComponent& Layer_, IN const MeshFilterComponent& Meshes_, IN const MeshRenderComponent& Meterials_, IN const size_t& nVersions_)
{
    if (m_nVersion == nVersions_)
    {
        for (size_t i = 0; i < m_vecObjects.size(); i++)
        {
            m_vecObjects[i].pMaterial->Update();
            m_vecObjects[i].pMesh->Update();
        }
        return;
    }

    auto _pResource = iCAX::Services::GetGlobalServiceProvider()->Resolve<iCAX::Core::IResourceService>();

    auto& _nLayerNo = Layer_.GetLayerNo();
    auto& _Mat = Transform_.m_Local2WorldMatrix;
    auto _InverseMat = _Mat.Inverse();
    glm::mat4 _Model2WorldMatrix{ 1.0f };//!< 默认单位矩阵
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            _Model2WorldMatrix[i][j] = static_cast<float>(_Mat.Mat()(i, j));
        }
    }

    m_vecObjects.clear();
    std::vector<std::shared_ptr<glMesh>> m_Meshes;
    std::vector<std::shared_ptr<glMaterial>> m_Materials;

    const auto& _vecMeshes = Meshes_.GetMeshes();
    for (int i = 0; i < _vecMeshes.size(); i++)
    {
        const auto& _MeshID = _vecMeshes[i];
        auto _pRMesh = _pResource->Get<rMesh>(_MeshID);
        if (_pRMesh == nullptr)
        {
            continue;
        }
        auto _pGLMesh = _pResource->Get<glMesh>(_MeshID);
        if (_pGLMesh == nullptr)//!< 未创建glMesh，则创建并缓存
        {
            _pGLMesh = std::make_shared<glMesh>();
            _pGLMesh->Upload(_pRMesh);
            _pResource->Set<glMesh>(_MeshID, _pGLMesh);//!< 缓存
        }
        else//!< 已创建，则更新
        {
            _pGLMesh->Update();
        }

        m_Meshes.push_back(_pGLMesh);
    }

    const auto& _vecMaterials = Meterials_.GetMaterials();
    for (int i = 0; i < _vecMaterials.size(); i++)
    {
        const auto& _MaterialID = _vecMaterials[i];
        auto _pRMaterial = _pResource->Get<rMaterial>(_MaterialID);
        if (_pRMaterial == nullptr)
        {
            continue;
        }
        auto _pGLMaterial = _pResource->Get<glMaterial>(_MaterialID);
        if (_pGLMaterial == nullptr)
        {
            _pGLMaterial = std::make_shared<glMaterial>();
            _pGLMaterial->Upload(_pRMaterial);
            _pResource->Set<glMaterial>(_MaterialID, _pGLMaterial);//!< 缓存
        }
        else//!< 已创建，则更新
        {
            _pGLMaterial->Update();
        }
        m_Materials.push_back(_pGLMaterial);
    }

    size_t _nSize = std::min(m_Meshes.size(), m_Materials.size());
    for (size_t i = 0; i < _nSize; i++)
    {
        m_vecObjects.push_back(glObject());
        m_vecObjects.back().Model2WorldMatrix = _Model2WorldMatrix;
        m_vecObjects.back().pMaterial = m_Materials[i];
        m_vecObjects.back().pMesh = m_Meshes[i];
        m_vecObjects.back().nLayerNo = _nLayerNo;
    }

    for (size_t i = 0; i < m_vecObjects.size(); i++)
    {
        m_vecObjects[i].pMaterial->Update();
        m_vecObjects[i].pMesh->Update();
    }

    m_nVersion = nVersions_;
}

//!< 渲染
void iCAX::Render::glEntity::Render(IN const std::shared_ptr<glCamera>& pCamera_, IN const std::vector<std::shared_ptr<glLight>>& Lights_, IN const std::shared_ptr<glEnvironment>& pEnv_)
{
    if (m_vecObjects.empty())
    {
        return;
    }

    // 使用每个材质的 Shader
    for (size_t _nSubMeshIndex = 0; _nSubMeshIndex < m_vecObjects.size(); ++_nSubMeshIndex)
    {
        if (!pCamera_->IsVisiable(m_vecObjects[_nSubMeshIndex].nLayerNo))
        {
            //continue;
            return;//!< 此处不需要continue，因为可见是针对一个物体，而非一个子网格颗粒度
        }
        const auto& pGLMesh = m_vecObjects[_nSubMeshIndex].pMesh;
        const auto& pGLMaterial = m_vecObjects[_nSubMeshIndex].pMaterial;

        pGLMaterial->Use();
        // 获取材质的着色器
        auto shader = pGLMaterial->Shader();
        shader->Use();  // 使用当前材质的着色器

        // 设置视图和投影矩阵
        pCamera_->BindToShader(*shader);
        // 设置多个光源
        for (size_t _nLightIndex = 0; _nLightIndex < Lights_.size(); ++_nLightIndex)
        {
            // 使用不同的光源编号
            Lights_[_nLightIndex]->BindToShader(static_cast<int>(_nLightIndex), *shader);
        }
        if (pEnv_)
        {
            pEnv_->BindToShader(*shader);
        }

        // 设置模型矩阵
        shader->SetUniform("u_Model", m_vecObjects[_nSubMeshIndex].Model2WorldMatrix);


        // 绘制网格
        pGLMesh->Draw();
    }
}

//！获取所有待渲染的子物体
const std::vector<iCAX::Render::glObject>& iCAX::Render::glEntity::SubObjects() const
{
    return m_vecObjects;
}
