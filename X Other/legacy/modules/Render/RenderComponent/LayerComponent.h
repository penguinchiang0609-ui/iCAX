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
        * @brief MeshFilter
        */
        class _RENDERCOMPONENT_EXP LayerComponent  final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(LayerComponent);

            DECLARED_ICAX_FIELD(LayerComponent, int, LayerNo, 0,
                [](const int& l, const int& r) { return l == r; },
                [](const int & v) { return v; },
                [](const PropertyValue& v) { return v.To<int>(); });
        };
    }
}