#pragma once
#include "Render.h"
#include "Math/Point3.h"
#include "Math/Dir3.h"
#include "Math/RGBA32.h"
#include "Database/Component.h"
#include "Database/ComponentHelper.h"
#include "Database/IEntity.h"
#include "Mesh.h"
#include "CoreComponent/TransformComponent.h"
#include "LayerComponent.h"
#include "SceneComponent.h"
#include "MeshRenderComponent.h"

using namespace iCAX::Data;
using namespace iCAX::Math;
using namespace iCAX::Core;

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief MeshFilter
        */
        class _RENDERCOMPONENT_EXP MeshFilterComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(MeshFilterComponent);

            //!< Meshes
            DECLARED_ICAX_FIELD(MeshFilterComponent, std::vector<std::string>, Meshes, std::vector<std::string>(),
                [](const std::vector<std::string>& lhs_, const std::vector<std::string>& rhs_) {return lhs_ == rhs_; },
                [](const std::vector<std::string>& lhs_)
                {
                    iCAX::Data::PropertyArray _Array;
                    for (auto& _pMesh : lhs_)
                    {
                        _Array.push_back(_pMesh);
                    }
                    return _Array;
                },
                [](const iCAX::Data::PropertyValue& lhs_)
                {
                    std::vector<std::string> _Meshes;
                    iCAX::Data::PropertyArray _Array = lhs_.To<iCAX::Data::PropertyArray>();
                    for (auto& _ObjValue : _Array)
                    {
                        _Meshes.push_back(_ObjValue.To<std::string>());
                    }
                    return _Meshes;
                });

            //! 场景组件，即该相机属于哪个场景
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<SceneComponent>, pScene, {});
            //! 变换组件
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<TransformComponent>, pTransform, {});
            //! 渲染层
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<LayerComponent>, pLayer, {});
            //! 材质
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<MeshRenderComponent>, pRender, {});
        };
    }
}