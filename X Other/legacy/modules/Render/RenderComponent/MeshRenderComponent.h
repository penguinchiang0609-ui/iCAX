#pragma once
#include "Render.h"
#include "Math/Point3.h"
#include "Math/Dir3.h"
#include "Math/RGBA32.h"
#include "Database/Component.h"
#include "Database/ComponentHelper.h"
#include "Database/IEntity.h"
#include "Mesh.h"

using namespace iCAX::Data;
using namespace iCAX::Math;

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief 渲染组建
        */
        class _RENDERCOMPONENT_EXP MeshRenderComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(MeshRenderComponent);

            //!< Materials
            DECLARED_ICAX_FIELD(MeshRenderComponent, std::vector<std::string>, Materials, std::vector<std::string>(),
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
        };
    }
}