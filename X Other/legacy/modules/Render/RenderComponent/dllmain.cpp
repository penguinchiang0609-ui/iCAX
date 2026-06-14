// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"

#include "Material.h"
#include "Shader.h"
#include "Texture2.h"
#include "Texture3.h"
#include "TextureCube.h"
#include "Mesh.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "MeshFilterComponent.h"
#include "MeshRenderComponent.h"
#include "SceneComponent.h"
#include "Text2Component.h"
#include "Text3Component.h"
#include "EnvironmentComponent.h"
#include "LayerComponent.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

