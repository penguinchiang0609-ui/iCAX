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
#include "glEnvironment.h"

using namespace iCAX::Data;
using namespace iCAX::Math;

namespace iCAX
{
    namespace Render
    {
        struct glObject final
        {
            int nLayerNo = 0xFFFFFFFF;//!< 默认可见
            std::shared_ptr<glMesh> pMesh{};
            std::shared_ptr<glMaterial> pMaterial{};
            glm::mat4 Model2WorldMatrix{ 1.0f };//!< 默认单位矩阵
        };
    }
}