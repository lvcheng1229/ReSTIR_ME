#define ENABLE_PIX_FRAME_CAPTURE 0
#define PIX_CAPUTRE_LOAD_FROM_DLL 0

#include "Restir/RestirGBufferGen.h"
#include "Restir/RestirGlobalResource.h"
#include "Restir/RestirRayTracer.h"
#include "Restir/RestirResampling.h"
#include "Restir/RestirIntegrateAndDenoise.h"

#if ENABLE_PIX_FRAME_CAPTURE
#if !PIX_CAPUTRE_LOAD_FROM_DLL
#include "pix3.h"
#endif
#endif

#if ENABLE_PIX_FRAME_CAPTURE && PIX_CAPUTRE_LOAD_FROM_DLL

#define PIX_CAPTURE_TIMING (1 << 0)
#define PIX_CAPTURE_GPU (1 << 1)
#define PIX_CAPTURE_FUNCTION_SUMMARY (1 << 2)
#define PIX_CAPTURE_FUNCTION_DETAILS (1 << 3)
#define PIX_CAPTURE_CALLGRAPH (1 << 4)
#define PIX_CAPTURE_INSTRUCTION_TRACE (1 << 5)
#define PIX_CAPTURE_SYSTEM_MONITOR_COUNTERS (1 << 6)
#define PIX_CAPTURE_VIDEO (1 << 7)
#define PIX_CAPTURE_AUDIO (1 << 8)
#define PIX_CAPTURE_GPU_TRACE (1 << 9)
#define PIX_CAPTURE_RESERVED (1 << 15)

union PIXCaptureParameters {
    enum PIXCaptureStorage {
        Memory = 0,
        MemoryCircular = 1, // Xbox only
        FileCircular = 2, // PC only
    };

    struct GpuCaptureParameters {
        PCWSTR FileName;
    } GpuCaptureParameters;

    struct TimingCaptureParameters {
        PCWSTR FileName;
        UINT32 MaximumToolingMemorySizeMb;
        PIXCaptureStorage CaptureStorage;

        BOOL CaptureGpuTiming;

        BOOL CaptureCallstacks;
        BOOL CaptureCpuSamples;
        UINT32 CpuSamplesPerSecond;

        BOOL CaptureFileIO;

        BOOL CaptureVirtualAllocEvents;
        BOOL CaptureHeapAllocEvents;
        BOOL CaptureXMemEvents; // Xbox only
        BOOL CapturePixMemEvents; // Xbox only
    } TimingCaptureParameters;

    struct GpuTraceParameters // Xbox Series and newer only
    {
        PWSTR FileName;
        UINT32 MaximumToolingMemorySizeMb;

        BOOL CaptureGpuOccupancy;

    } GpuTraceParameters;
};

typedef PIXCaptureParameters* PPIXCaptureParameters;

typedef HRESULT(__stdcall* PIXBeginCapture2_API)(DWORD captureFlags, const PPIXCaptureParameters captureParameters);
typedef HRESULT(__stdcall* PIXEndCapture_API)(BOOL discard);

static PIXBeginCapture2_API PIXBeginCapture2 = nullptr;
static PIXEndCapture_API PIXEndCapture = nullptr;

inline HRESULT PIXBeginCapture(DWORD captureFlags, const PPIXCaptureParameters captureParameters) { return PIXBeginCapture2(captureFlags, captureParameters); }

#endif

using namespace GameCore;
using namespace Math;
using namespace Graphics;
using namespace std;

using Renderer::MeshSorter;

class RestirApp : public GameCore::IGameApp
{
public:

    RestirApp(void);

    virtual void Startup( void ) override;
    virtual void Cleanup( void ) override;

    virtual void Update( float deltaT ) override;
    virtual void RenderScene( void ) override;

private:

    Camera m_Camera;
    unique_ptr<CameraController> m_CameraController;

    D3D12_VIEWPORT m_MainViewport;
    D3D12_RECT m_MainScissor;

    ModelInstance m_ModelInst;
    ShadowCamera m_SunShadowCamera;
    CRestirRayTracer restirRayTracer;
    CGBufferGenPass m_GBufferGenPass;
    CRestirResamplingPass restirResamplingPass;
    CRestirIntegrateAndDenoise integrateAndDenoise;

    Math::Matrix4 pre_view_matrix;
#if ENABLE_PIX_FRAME_CAPTURE
    HMODULE m_pixModule;
#endif
};

RestirApp::RestirApp(void)
{
#if ENABLE_PIX_FRAME_CAPTURE
#if PIX_CAPUTRE_LOAD_FROM_DLL
    m_pixModule = LoadLibrary(m_deviceInifConfig.m_pixCaptureDllPath.c_str());
    PIXBeginCapture2 = (PIXBeginCapture2_API)GetProcAddress(m_pixModule, "PIXBeginCapture2");
    PIXEndCapture = (PIXEndCapture_API)GetProcAddress(m_pixModule, "PIXEndCapture");
#else
    m_pixModule = PIXLoadLatestWinPixGpuCapturerLibrary();
#endif
#endif
}

CREATE_APPLICATION( RestirApp )

ExpVar g_SunLightIntensity("Viewer/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
NumVar g_SunOrientation("Viewer/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f );
NumVar g_SunInclination("Viewer/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f );

void ChangeIBLSet(EngineVar::ActionType);
void ChangeIBLBias(EngineVar::ActionType);

DynamicEnumVar g_IBLSet("Viewer/Lighting/Environment", ChangeIBLSet);
std::vector<std::pair<TextureRef, TextureRef>> g_IBLTextures;
NumVar g_IBLBias("Viewer/Lighting/Gloss Reduction", 2.0f, 0.0f, 10.0f, 1.0f, ChangeIBLBias);

void ChangeIBLSet(EngineVar::ActionType)
{
    int setIdx = g_IBLSet - 1;
    if (setIdx < 0)
    {
        Renderer::SetIBLTextures(nullptr, nullptr);
    }
    else
    {
        auto texturePair = g_IBLTextures[setIdx];
        Renderer::SetIBLTextures(texturePair.first, texturePair.second);
    }
}

void ChangeIBLBias(EngineVar::ActionType)
{
    Renderer::SetIBLBias(g_IBLBias);
}

#include <direct.h> // for _getcwd() to check data root path

void LoadIBLTextures()
{
    char CWD[256];
    _getcwd(CWD, 256);

    Utility::Printf("Loading IBL environment maps\n");

    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile(L"Textures/*_diffuseIBL.dds", &ffd);

    g_IBLSet.AddEnum(L"None");

    if (hFind != INVALID_HANDLE_VALUE) do
    {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

       std::wstring diffuseFile = ffd.cFileName;
       std::wstring baseFile = diffuseFile; 
       baseFile.resize(baseFile.rfind(L"_diffuseIBL.dds"));
       std::wstring specularFile = baseFile + L"_specularIBL.dds";

       TextureRef diffuseTex = TextureManager::LoadDDSFromFile(L"Textures/" + diffuseFile);
       if (diffuseTex.IsValid())
       {
           TextureRef specularTex = TextureManager::LoadDDSFromFile(L"Textures/" + specularFile);
           if (specularTex.IsValid())
           {
               g_IBLSet.AddEnum(baseFile);
               g_IBLTextures.push_back(std::make_pair(diffuseTex, specularTex));
           }
       }
    }
    while (FindNextFile(hFind, &ffd) != 0);

    FindClose(hFind);

    Utility::Printf("Found %u IBL environment map sets\n", g_IBLTextures.size());

    if (g_IBLTextures.size() > 0)
        g_IBLSet.Increment();
}

void RestirApp::Startup( void )
{
    MotionBlur::Enable = false;
    TemporalEffects::EnableTAA = false;
    FXAA::Enable = false;
    PostEffects::EnableHDR = true;
    PostEffects::EnableAdaptation = true;
    SSAO::Enable = true;

    Renderer::Initialize();

    InitGlobalResource();
    m_GBufferGenPass.Init();

    LoadIBLTextures();

    std::wstring gltfFileName;

    bool forceRebuild = false;
    uint32_t rebuildValue;
    if (CommandLineArgs::GetInteger(L"rebuild", rebuildValue))
        forceRebuild = rebuildValue != 0;

    if (CommandLineArgs::GetString(L"model", gltfFileName) == false)
    {
#ifdef LEGACY_RENDERER
        //Sponza::Startup(m_Camera);
#else
        // workaround
        Sponza::Startup(m_Camera);

        m_ModelInst = Renderer::LoadModel(L"Sponza/PBR/sponza2.gltf", forceRebuild);
        m_ModelInst.Resize(100.0f * m_ModelInst.GetRadius());
        OrientedBox obb = m_ModelInst.GetBoundingBox();
        float modelRadius = Length(obb.GetDimensions()) * 0.5f;
        const Vector3 eye = obb.GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
        m_Camera.SetEyeAtUp( eye, Vector3(kZero), Vector3(kYUnitVector) );
#endif
    }
    else
    {
        m_ModelInst = Renderer::LoadModel(gltfFileName, forceRebuild);
        m_ModelInst.LoopAllAnimations();
        m_ModelInst.Resize(10.0f);

        MotionBlur::Enable = false;
    }

    m_Camera.SetZRange(1.0f, 10000.0f);
    if (gltfFileName.size() == 0)
        m_CameraController.reset(new FlyingFPSCamera(m_Camera, Vector3(kYUnitVector)));
    else
        m_CameraController.reset(new OrbitCamera(m_Camera, m_ModelInst.GetBoundingSphere(), Vector3(kYUnitVector)));

    GetGlobalResource().pModelInst = &m_ModelInst;
    restirRayTracer.Init();
    restirResamplingPass.Init();
}

void RestirApp::Cleanup( void )
{
    m_ModelInst = nullptr;

    g_IBLTextures.clear();

//#ifdef LEGACY_RENDERER
    Sponza::Cleanup();
//#endif

    Renderer::Shutdown();
}

namespace Graphics
{
    extern EnumVar DebugZoom;
}

void RestirApp::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    GetGlobalResource().currentFrameIndex++;
    
    if (GameInput::IsFirstPressed(GameInput::kLShoulder))
        DebugZoom.Decrement();
    else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
        DebugZoom.Increment();

    m_CameraController->Update(deltaT);

    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");

    m_ModelInst.Update(gfxContext, deltaT);

    gfxContext.Finish();

    m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    m_MainViewport.MinDepth = 0.0f;
    m_MainViewport.MaxDepth = 1.0f;

    m_MainScissor.left = 0;
    m_MainScissor.top = 0;
    m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

   
}

void RestirApp::RenderScene( void )
{
#if ENABLE_PIX_FRAME_CAPTURE
    static int PixFrameIndex = 0;

    if (PixFrameIndex == 14)
    {
#if ENABLE_PIX_FRAME_CAPTURE
#if PIX_CAPUTRE_LOAD_FROM_DLL
        std::wstring pixPath = WstringConverter().from_bytes(m_deviceInifConfig.m_pixCaptureSavePath);
        PIXCaptureParameters pixCaptureParameters;
        pixCaptureParameters.GpuCaptureParameters.FileName = pixPath.c_str();
        PIXBeginCapture(PIX_CAPTURE_GPU, &pixCaptureParameters);
#else
        std::wstring pixPath = L"H:/ReSTIR_ME/MiniEngine/Build/x64/Debug/Output/Restir/pix.wpix";
        PIXCaptureParameters pixCaptureParameters;
        pixCaptureParameters.GpuCaptureParameters.FileName = pixPath.c_str();
        PIXBeginCapture(PIX_CAPTURE_GPU, &pixCaptureParameters);
#endif
#endif
    }

    if (PixFrameIndex == 15)
    {
        PIXEndCapture(false);
    }

    PixFrameIndex++;
#endif
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    const D3D12_VIEWPORT& viewport = m_MainViewport;
    const D3D12_RECT& scissor = m_MainScissor;

    ParticleEffectManager::Update(gfxContext.GetComputeContext(), Graphics::GetFrameTime());

    if (m_ModelInst.IsNull())
    {
#ifdef LEGACY_RENDERER
        Sponza::RenderScene(gfxContext, m_Camera, viewport, scissor);
#endif
    }
    else
    {
        // Update global constants
        float costheta = cosf(g_SunOrientation);
        float sintheta = sinf(g_SunOrientation);
        float cosphi = cosf(g_SunInclination * 3.14159f * 0.5f);
        float sinphi = sinf(g_SunInclination * 3.14159f * 0.5f);

        Vector3 SunDirection = Normalize(Vector3( costheta * cosphi, sinphi, sintheta * cosphi ));
        Vector3 ShadowBounds = Vector3(m_ModelInst.GetRadius());
        //m_SunShadowCamera.UpdateMatrix(-SunDirection, m_ModelInst.GetCenter(), ShadowBounds,
        m_SunShadowCamera.UpdateMatrix(-SunDirection, Vector3(0, -500.0f, 0), Vector3(5000, 3000, 3000),
            (uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

        {
            GetGlobalResource().restirSceneInfoTemp.lightDirection = DirectX::XMFLOAT3(SunDirection.GetX(), SunDirection.GetY(), SunDirection.GetZ()) ;
            GetGlobalResource().restirSceneInfoTemp.fullScreenTextureSize = DirectX::XMFLOAT4(g_SceneGBufferA.GetWidth(), g_SceneGBufferA.GetHeight(), 1.0 / g_SceneGBufferA.GetWidth(), 1.0 / g_SceneGBufferA.GetHeight());
            GetGlobalResource().restirSceneInfoTemp.restirTextureSize = DirectX::XMFLOAT4(g_ReservoirRayDirection[0].GetWidth(), g_ReservoirRayDirection[0].GetHeight(), 1.0 / g_ReservoirRayDirection[0].GetWidth(), 1.0 / g_ReservoirRayDirection[0].GetHeight());

            GetGlobalResource().restirSceneInfo.PreViewProjMatrix = pre_view_matrix;
            GetGlobalResource().restirSceneInfo.g_restir_texturesize = DirectX::XMFLOAT2(g_ReservoirRayDirection[0].GetWidth(), g_ReservoirRayDirection[0].GetHeight());
            GetGlobalResource().restirSceneInfo.g_full_screen_texsize = DirectX::XMFLOAT2(g_SceneGBufferA.GetWidth(), g_SceneGBufferA.GetHeight());
            GetGlobalResource().restirSceneInfo.g_sun_direction = DirectX::XMFLOAT3(SunDirection.GetX(), SunDirection.GetY(), SunDirection.GetZ());
            GetGlobalResource().restirSceneInfo.g_current_frame_index++;
            GetGlobalResource().restirSceneInfo.g_sun_intensity = DirectX::XMFLOAT3(10.0f, 10.0f, 10.0f);
            GetGlobalResource().restirSceneInfo.g_camera_worldpos = DirectX::XMFLOAT3(m_Camera.GetPosition().GetX(), m_Camera.GetPosition().GetY(), m_Camera.GetPosition().GetZ());

            pre_view_matrix = m_Camera.GetViewProjMatrix();
        }

        GlobalConstants globals;
        globals.ViewProjMatrix = m_Camera.GetViewProjMatrix();
        globals.SunShadowMatrix = m_SunShadowCamera.GetShadowMatrix();
        globals.CameraPos = m_Camera.GetPosition();
        globals.SunDirection = SunDirection;
        globals.SunIntensity = Vector3(Scalar(g_SunLightIntensity));

        // generate gbuffer
        {
            MeshSorter gBufferGenwSorter(MeshSorter::kRestirGBuffer);
            gBufferGenwSorter.SetCamera(m_Camera);
            gBufferGenwSorter.SetViewport(viewport);
            gBufferGenwSorter.SetScissor(scissor);
            m_ModelInst.Render(gBufferGenwSorter);
            gBufferGenwSorter.Sort();

            globals.ViewProjMatrix = m_Camera.GetViewProjMatrix();
            globals.CameraPos = m_Camera.GetPosition();

            m_GBufferGenPass.GenerateGBuffer(gfxContext, globals, gBufferGenwSorter.GetSortObject());
        }

        {
            restirRayTracer.GenerateInitialSampling(gfxContext);
        }

        // Begin rendering depth
        gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
        gfxContext.ClearDepth(g_SceneDepthBuffer);

        MeshSorter sorter(MeshSorter::kDefault);
		sorter.SetCamera(m_Camera);
		sorter.SetViewport(viewport);
		sorter.SetScissor(scissor);
		sorter.SetDepthStencilTarget(g_SceneDepthBuffer);
		sorter.AddRenderTarget(g_SceneColorBuffer);

        m_ModelInst.Render(sorter);

        sorter.Sort();

        {
            ScopedTimer _prof(L"Depth Pre-Pass", gfxContext);
            sorter.RenderMeshes(MeshSorter::kZPass, gfxContext, globals);
        }

        SSAO::Render(gfxContext, m_Camera);

        {
            ScopedTimer _outerprof(L"Main Render", gfxContext);


            {
                ScopedTimer _prof(L"Sun Shadow Map", gfxContext);

                MeshSorter shadowSorter(MeshSorter::kShadows);
				shadowSorter.SetCamera(m_SunShadowCamera);
				shadowSorter.SetDepthStencilTarget(g_ShadowBuffer);

                m_ModelInst.Render(shadowSorter);

                shadowSorter.Sort();
                shadowSorter.RenderMeshes(MeshSorter::kZPass, gfxContext, globals);
            }







            gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
            gfxContext.ClearColor(g_SceneColorBuffer);

            {
                ScopedTimer _prof(L"Render Color", gfxContext);

                gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
                gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
                gfxContext.SetViewportAndScissor(viewport, scissor);

                sorter.RenderMeshes(MeshSorter::kOpaque, gfxContext, globals);
            }

            Renderer::DrawSkybox(gfxContext, m_Camera, viewport, scissor);

            sorter.RenderMeshes(MeshSorter::kTransparent, gfxContext, globals);
        }
    }

    gfxContext.Finish();
}
