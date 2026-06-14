#pragma once
#include "Render.h"
#include "Database/ComponentBase.h"
#include "Database/ComponentHelper.h"
#include "Data/uuid.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Render
    {
        using WindowHandle = void*; // 内部仍可存 HWND
        /*
        * @brief 渲染场景
        * @details
        *   场景ID
        *   每个需要支持渲染的场景，都会有且仅有一个渲染RenderSceneComponent
        *   当多个World需要叠加渲染的时候。他们公用同一个SceneID即可
        *   IsHwndOwner表征是否为渲染窗口所有者，如果为所有者，则组件销毁时会释放渲染上下文。如果不是拥有者，则只是释放组件所在世界的渲染信息
        */
        class _RENDERCOMPONENT_EXP SceneComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(SceneComponent);

            DECLARED_ICAX_FIELD(SceneComponent, iCAX::Data::uuid, SceneID, iCAX::Data::uuid(),
                [](const iCAX::Data::uuid& l, const iCAX::Data::uuid& r) { return l == r; },
                [](const iCAX::Data::uuid& v) { return v; },
                [](const PropertyValue& v) { return v.To<iCAX::Data::uuid>(); });

            DECLARED_ICAX_FIELD(SceneComponent, bool, IsHwndOwner, true,
                [](const bool& l, const bool& r) { return l == r; },
                [](const bool& v) { return v; },
                [](const PropertyValue& v) { return v.To<bool>(); });

            //! 本地直接声明的变量不支持序列化与反序列化以及撤销还原等操作
            //! 窗口句柄等本就无需持久化以及撤销还原
        public:
            DECLARED_ICAX_VOLATILE_FIELD(WindowHandle, MainWindow, nullptr, [](const WindowHandle& _lhs, const WindowHandle& _rhs) {return _lhs == _rhs; });
            DECLARED_ICAX_VOLATILE_FIELD(int, Width, 0, [](const int& _lhs, const int& _rhs) {return _lhs == _rhs; });
            DECLARED_ICAX_VOLATILE_FIELD(int, Height, 0, [](const int& _lhs, const int& _rhs) {return _lhs == _rhs; });
        };
    }
}