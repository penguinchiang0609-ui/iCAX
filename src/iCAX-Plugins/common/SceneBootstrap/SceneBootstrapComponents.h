#pragma once
#include <Database/ComponentHelper.h>

namespace iCAX::Common
{
    // Generic scene-start marker. Product modules may use it without depending
    // on a CAM, CAD, or manufacturing product.
    class CSceneBootstrapComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CSceneBootstrapComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CSceneBootstrapComponent)
    };
}
