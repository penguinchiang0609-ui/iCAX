#include "pch.h"
#include <windows.h>
#include <fstream>
#include "glScene.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

//!< 构造函数
iCAX::Render::glScene::glScene(IN const iCAX::Data::uuid& ID_)
    : m_ID(ID_)
{
}

//!< 获取ID
const iCAX::Data::uuid& iCAX::Render::glScene::GetID() const
{
    return m_ID;
}

//!< 设置画布
bool iCAX::Render::glScene::SetCanva(IN const SceneComponent& Component_, IN const size_t& nVerion_)
{
    if (m_Canva.nVersion == nVerion_)
    {
        return true;
    }
    auto _HWND = *static_cast<const HWND*>(Component_.GetMainWindow());
    if (_HWND != m_Canva.hwnd)
    {
        m_Canva.hwnd = _HWND;
        m_Canva.width = Component_.GetWidth();
        m_Canva.height = Component_.GetHeight();
        m_Canva.nVersion = nVerion_;

        // 创建 HDC / GLContext
        if (!CreateGLContext(m_Canva))
            return false;

        // 创建 FBO
        if (!CreateFBO(m_Canva))
        {
            DestroyGLContext(m_Canva);
            return false;
        }
    }
    else
    {
        m_Canva.width = Component_.GetWidth();
        m_Canva.height = Component_.GetHeight();
        m_Canva.nVersion = nVerion_;
    }

    return true;
}

//! 销毁画布
void iCAX::Render::glScene::DestroyCanva()
{
    // 销毁 FBO
    DestroyFBO(m_Canva);

    // 销毁 OpenGL 上下文
    DestroyGLContext(m_Canva);
}

void iCAX::Render::glScene::RemoveCanva()
{
    // 销毁 FBO
    DestroyFBO(m_Canva);

    // 销毁 OpenGL 上下文
    DestroyGLContext(m_Canva);
    m_Canva = {};//! 重新构造
}

//!< 设置主相机
void iCAX::Render::glScene::SetMainCamera(IN const CameraComponent& Camera_, IN const TransformComponent& Tranform, IN const size_t& nVerion_)
{
    if (!m_MainCamera)
    {
        m_MainCamera = std::make_shared<glCamera>();
    }
    m_MainCamera->Update(Camera_, Tranform, nVerion_);
}

//! 移除主相机
void iCAX::Render::glScene::RemoveMainCamera()
{
    m_MainCamera = nullptr;
}

//!< 设置环境
void iCAX::Render::glScene::SetEnviroment(IN const EnvironmentComponent& Component_, IN const size_t& nVerion_)
{
    if (!m_Env)
    {
        m_Env = std::make_shared<glEnvironment>();
    }
    m_Env->Update(Component_, nVerion_);
}

//!< 设置光源
void iCAX::Render::glScene::SetLight(IN const iCAX::Data::uuid& ID_, IN const LightComponent& Light_, IN const TransformComponent& Tranform, IN const size_t& nVerion_)
{
    if (m_mapLights.contains(ID_))
    {
        m_mapLights[ID_]->Update(Light_, Tranform, nVerion_);
    }
    else
    {
        auto _pLight = std::make_shared<glLight>();
        m_mapLights[ID_] = _pLight;
        _pLight->Update(Light_, Tranform, nVerion_);
    }
}

//!< 移除光源
void iCAX::Render::glScene::RemoveLight(IN const iCAX::Data::uuid& ID_)
{
    m_mapLights.erase(ID_);
}

//! 设置物体
void iCAX::Render::glScene::SetEntity(IN const iCAX::Data::uuid& ID_, IN const TransformComponent& Transform_, IN const LayerComponent& Layer_, IN const MeshFilterComponent& Meshes_, IN const MeshRenderComponent& Meterials_, IN const size_t& nVersions_)
{
    if (m_mapEntities.contains(ID_))
    {
        m_mapEntities[ID_]->Update(Transform_, Layer_, Meshes_, Meterials_, nVersions_);
    }
    else
    {
        auto _pEntity = std::make_shared<glEntity>();
        m_mapEntities[ID_] = _pEntity;
        _pEntity->Update(Transform_, Layer_, Meshes_, Meterials_, nVersions_);
    }
}

//! 移除实体
void iCAX::Render::glScene::RemoveEntity(IN const iCAX::Data::uuid& ID_)
{
    m_mapEntities.erase(ID_);
}

//!< 渲染
void iCAX::Render::glScene::Render()
{
    if (!m_Canva.fbo)//! 画布被销毁或者未初始化
    {
        return;
    }

    if (!m_MainCamera)//! 主相机被销毁或未设置
    {
        return;
    }

    if (!wglMakeCurrent(m_Canva.hdc, m_Canva.glContext))
        return;//!< 切换当前 GLContext

    glBindFramebuffer(GL_FRAMEBUFFER, m_Canva.fbo);//!< 绑定 FBO
    glViewport(0, 0, m_Canva.width, m_Canva.height);//!< 设置视口

    // 清屏
    glClearColor(m_Canva.background.r, m_Canva.background.g, m_Canva.background.b, m_Canva.background.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 遍历 data.scene->RenderObjects，逐个渲染
    // TODO：此处需要改成批量渲染，即按照材质分组，然后按照mesh进行分组，逐个组渲染
    //for (size_t i = 0; i < m_vecEntities.size(); i++)
    //{
    //    m_vecEntities[i]->Render(m_MainCamera, m_vecLights, m_Env);
    //}

    //!< 按材质分组，然后逐组渲染，减少材质的重复设置
    std::unordered_map<std::shared_ptr<glMaterial>, std::unordered_map<std::shared_ptr<glMesh>, std::vector<const glObject*>>> _Groups;
    for (const auto& [_ID, _pEntity] : m_mapEntities)
    {
        for (const auto& _Obejct : _pEntity->SubObjects())
        {
            if (!m_MainCamera->IsVisiable(_Obejct.nLayerNo))//!< 不可见，就不追加到待渲染列表
            {
                continue;
            }
            if (!_Groups.contains(_Obejct.pMaterial))
            {
                _Groups[_Obejct.pMaterial] = std::unordered_map<std::shared_ptr<glMesh>, std::vector<const glObject*>>();
            }
            if (_Groups[_Obejct.pMaterial].contains(_Obejct.pMesh))
            {
                _Groups[_Obejct.pMaterial][_Obejct.pMesh] = std::vector<const glObject*>();
            }
            _Groups[_Obejct.pMaterial][_Obejct.pMesh].push_back(&_Obejct);
        }
    }

    //!< TODO：根据_MeshPair中_vecObjects的数量动态判定是否启用GPU Instance
    for (const auto& [_pMaterial,_MeshPair] : _Groups)
    {
        _pMaterial->Use();
        auto _pShader = _pMaterial->Shader();
        //! 绑定相机
        m_MainCamera->BindToShader(*_pShader);
        //! 绑定光源
        int _nLightIndex = 0;
        for (auto& [_ID, _pLight] : m_mapLights)
        {
            _pLight->BindToShader(_nLightIndex++, *_pShader);
        }
        //! 绑定环境
        if (m_Env)
        {
            m_Env->BindToShader(*_pShader);
        }
        for (const auto& [_pMesh, _vecObjects] : _MeshPair)
        {
            for (const auto& _pObject : _vecObjects)
            {
                _pShader->SetUniform("u_Model", _pObject->Model2WorldMatrix);
                _pObject->pMesh->Draw();
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 将 FBO 渲染到窗口
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Canva.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, m_Canva.width, m_Canva.height, 0, 0, m_Canva.width, m_Canva.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    SwapBuffers(m_Canva.hdc);//!< 双缓冲，切换视窗

    if (!m_strFrame2ImagePath.empty())
    {
        SaveToImage();
        m_strFrame2ImagePath.clear();
    }
}

//!< 设置下一帧保存图片位置
void iCAX::Render::glScene::SetNextFrameImagePath(IN const std::string& strPath_)
{
    m_strFrame2ImagePath = strPath_;
}

//!< 清理资源
void iCAX::Render::glScene::Cleanup()
{
    // 先清理实体和光源
    m_mapEntities.clear();
    m_mapLights.clear();

    // 清理相机和环境
    m_MainCamera.reset();
    m_Env.reset();

    // 销毁 FBO
    DestroyFBO(m_Canva);

    // 销毁 OpenGL 上下文
    DestroyGLContext(m_Canva);

    // 重置画布信息
    m_Canva = CanvaData{};
}

bool iCAX::Render::glScene::CreateGLContext(CanvaData& data)
{
    // 如果没有窗口，则创建一个隐藏的窗口用于离屏渲染
    if (data.hwnd == nullptr)
    {
        WNDCLASSA wc = { 0 };
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "HiddenGLWindow";

        static bool registered = false;
        if (!registered)
        {
            RegisterClassA(&wc);
            registered = true;
        }

        data.hwnd = CreateWindowA("HiddenGLWindow", "Offscreen", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    }

    data.hdc = GetDC(data.hwnd);
    if (!data.hdc) return false;

    PIXELFORMATDESCRIPTOR pfd = { sizeof(PIXELFORMATDESCRIPTOR), 1 };
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;

    int pf = ChoosePixelFormat(data.hdc, &pfd);
    if (pf == 0) return false;
    if (!SetPixelFormat(data.hdc, pf, &pfd)) return false;

    data.glContext = wglCreateContext(data.hdc);
    if (!data.glContext) return false;

    if (!wglMakeCurrent(data.hdc, data.glContext)) return false;

    return true;
}

void iCAX::Render::glScene::DestroyGLContext(CanvaData& data)
{
    if (data.glContext)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(data.glContext);
        data.glContext = nullptr;
    }
    if (data.hdc && data.hwnd)
    {
        ReleaseDC(data.hwnd, data.hdc);
        data.hdc = nullptr;
    }
}

bool iCAX::Render::glScene::CreateFBO(CanvaData& data)
{
    glGenFramebuffers(1, &data.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, data.fbo);

    glGenTextures(1, &data.colorTex);
    glBindTexture(GL_TEXTURE_2D, data.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, data.width, data.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, data.colorTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "FBO incomplete" << std::endl;
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void iCAX::Render::glScene::DestroyFBO(CanvaData& data)
{
    if (data.fbo) glDeleteFramebuffers(1, &data.fbo);
    if (data.colorTex) glDeleteTextures(1, &data.colorTex);
    data.fbo = 0;
    data.colorTex = 0;
}

void iCAX::Render::glScene::SaveToImage()
{
    if (m_Canva.width <= 0 || m_Canva.height <= 0)
        return;

    // 绑定 FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_Canva.fbo);

    // 读取像素
    std::vector<unsigned char> pixels(m_Canva.width * m_Canva.height * 3);
    glReadPixels(0, 0, m_Canva.width, m_Canva.height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // 翻转 Y 轴
    for (int y = 0; y < m_Canva.height / 2; ++y)
    {
        for (int x = 0; x < m_Canva.width * 3; ++x)
        {
            std::swap(pixels[y * m_Canva.width * 3 + x],
                pixels[(m_Canva.height - 1 - y) * m_Canva.width * 3 + x]);
        }
    }

    // 输出 PNG
    int ret = stbi_write_png(m_strFrame2ImagePath.c_str(),
        m_Canva.width,
        m_Canva.height,
        3,             // RGB
        pixels.data(),
        m_Canva.width * 3);

    // 解绑 FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //return ret != 0;
}
