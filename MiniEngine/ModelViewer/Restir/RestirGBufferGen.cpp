#include "RestirGBufferGen.h"
#include "RestirGlobalResource.h"
#include "ShaderCompile.h"
#include "SponzaRenderer.h"
#include "ModelH3D.h"

using namespace Graphics;

void CGBufferGenPass::Init()
{
    SamplerDesc DefaultSamplerDesc;
    DefaultSamplerDesc.MaxAnisotropy = 8;

    m_GBufferGenSig.Reset(3, 1);
    m_GBufferGenSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_GBufferGenSig[0].InitAsConstantBuffer(0);
    m_GBufferGenSig[1].InitAsConstantBuffer(1);
    m_GBufferGenSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
    m_GBufferGenSig.Finalize(L"m_GBufferGenSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    D3D12_INPUT_ELEMENT_DESC vertElem[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    DXGI_FORMAT GBufferFormat[2] = { g_SceneGBufferA.GetFormat(),g_SceneGBufferB.GetFormat()};
    DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();

    // PSO
    {
        std::shared_ptr<SCompiledShaderCode> pVsShaderCode = GetGlobalResource().m_ShaderCompilerDxc.Compile(L"Shaders/GBufferGenVS.hlsl", L"main", L"vs_6_5", nullptr, 0);
        std::shared_ptr<SCompiledShaderCode> pPsShaderCode = GetGlobalResource().m_ShaderCompilerDxc.Compile(L"Shaders/GBufferGenPS.hlsl", L"main", L"ps_6_5", nullptr, 0);

       m_GBufferGenPso = GraphicsPSO(L"m_GBufferGenPso");
       m_GBufferGenPso.SetRootSignature(m_GBufferGenSig);
       m_GBufferGenPso.SetRasterizerState(RasterizerDefault);
       m_GBufferGenPso.SetBlendState(BlendDisable);
       m_GBufferGenPso.SetDepthStencilState(DepthStateReadWrite);
       m_GBufferGenPso.SetInputLayout(_countof(vertElem), vertElem);
       m_GBufferGenPso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
       m_GBufferGenPso.SetRenderTargetFormats(2, GBufferFormat, DepthFormat);
       m_GBufferGenPso.SetVertexShader(pVsShaderCode->GetBufferPointer(), pVsShaderCode->GetBufferSize());
       m_GBufferGenPso.SetPixelShader(pPsShaderCode->GetBufferPointer(), pPsShaderCode->GetBufferSize());
       m_GBufferGenPso.Finalize();
    }
}

void CGBufferGenPass::GenerateGBuffer(GraphicsContext& context, GlobalConstants& globals, std::vector<Renderer::MeshSorter::SortObject>& sortObjects)
{
    using ReSortObject = Renderer::MeshSorter::SortObject;

    D3D12_VIEWPORT Viewport;
    D3D12_RECT Scissor;

    Viewport.TopLeftX = 0.0f;
    Viewport.TopLeftY = 0.0f;
    Viewport.Width = (float)g_SceneDepthBuffer.GetWidth();
    Viewport.Height = (float)g_SceneDepthBuffer.GetHeight();
    Viewport.MaxDepth = 1.0f;
    Viewport.MinDepth = 0.0f;

    Scissor.left = 0;
    Scissor.right = g_SceneDepthBuffer.GetWidth();
    Scissor.top = 0;
    Scissor.bottom = g_SceneDepthBuffer.GetWidth();

    context.SetRootSignature(m_GBufferGenSig);
    context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());

    D3D12_CPU_DESCRIPTOR_HANDLE RTVs[2] = { g_SceneGBufferA.GetRTV() ,g_SceneGBufferB.GetRTV() };

    context.TransitionResource(g_SceneGBufferA, D3D12_RESOURCE_STATE_RENDER_TARGET);
    context.TransitionResource(g_SceneGBufferB, D3D12_RESOURCE_STATE_RENDER_TARGET);
    context.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    context.SetRenderTargets(2, RTVs, g_SceneDepthBuffer.GetDSV());

    context.ClearDepth(g_SceneDepthBuffer);
    context.ClearColor(g_SceneGBufferA);
    context.ClearColor(g_SceneGBufferB);

    context.SetViewportAndScissor(Viewport, Scissor);
    context.FlushResourceBarriers();

    context.SetPipelineState(m_GBufferGenPso);

    auto& m_Model = Sponza::GetModel();
    context.SetIndexBuffer(m_Model.GetIndexBuffer());
    context.SetVertexBuffer(0, m_Model.GetVertexBuffer());

    __declspec(align(16)) uint32_t materialIdx = 0xFFFFFFFFul;
    uint32_t VertexStride = m_Model.GetVertexStride();

    context.SetDynamicConstantBufferView(0, sizeof(GlobalConstants), &globals);

    for (uint32_t meshIndex = 0; meshIndex < m_Model.GetMeshCount(); meshIndex++)
    {
        const ModelH3D::Mesh& mesh = m_Model.GetMesh(meshIndex);

        uint32_t indexCount = mesh.indexCount;
        uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
        uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

        if (mesh.materialIndex != materialIdx)
        {
            materialIdx = mesh.materialIndex;
            context.SetDescriptorTable(Renderer::kMaterialSRVs, m_Model.GetSRVs(materialIdx));
        }

        context.DrawIndexed(indexCount, startIndex, baseVertex);
    }
}
