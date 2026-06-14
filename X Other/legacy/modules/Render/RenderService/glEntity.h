#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include <vector>
#include "../RenderComponent/MeshFilterComponent.h"
#include "../RenderComponent/MeshRenderComponent.h"
#include "glMesh.h"
#include "glMaterial.h"
#include "glCamera.h"
#include "glLight.h"
#include "CoreComponent/TransformComponent.h"
#include "glEnvironment.h"
#include "glObject.h"
#include "../RenderComponent/LayerComponent.h"

using namespace iCAX::Data;
using namespace iCAX::Math;
using namespace iCAX::Core;

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief 渲染实体
        *
        * 该类负责管理一个实体的网格（Mesh）和材质（Material）。它提供上传、更新和渲染等操作。
        */
        class glEntity final
        {
        public:
            /**
            * @brief 上传实体的 mesh 和 material 数据
            *
            * 从传入的实体对象中提取网格和材质组件，并标记它们为脏，表示需要更新。
            * 然后会调用 Update() 方法进行更新操作。
            *
            * @param [in] pMainCamera_ 主相机
            * @param [in] Transform_ 变换组件
            * @param [in] Layer_ 图层组件
            * @param [in] Meshes_ 网格组件
            * @param [in] Meterials_ 材质组件
            * @param [in] nVersions_ 版本
            */
            void Update(IN const TransformComponent& Transform_, IN const LayerComponent& Layer_, IN const MeshFilterComponent& Meshes_, IN const MeshRenderComponent& Meterials_, IN const size_t& nVersions_);

            /**
            * @brief 渲染实体
            * @param [in] pCamera_ 当前的摄像机对象，用于计算视图矩阵和投影矩阵
            * @param [in] Lights_ 光源对象列表，用于设置场景中的光源信息
            * @param [in] Lights_ 光源对象列表，用于设置场景中的光源信息
            * @details
            *   根据当前的相机（Camera）和光源（Light）列表进行渲染。
            *   渲染时会将每个材质的 Shader、相机视图矩阵、投影矩阵和光源信息传递给着色器，最后绘制网格。
            */
            void Render(IN const std::shared_ptr<glCamera>& pCamera_, IN const std::vector<std::shared_ptr<glLight>>& Lights_, IN const std::shared_ptr<glEnvironment>& pEnv_);

            /*
            * @brief 获取所有待渲染的子物体
            * @热turn const std::vector<glObject>&
            */
            const std::vector<glObject>& SubObjects() const;

        private:
            std::vector<glObject> m_vecObjects;
            size_t m_nVersion = (size_t)-1;
        };
    }
}
