#include "RestirGBufferGen.h"
#include "RestirGlobalResource.h"
#include "ShaderCompile.h"

using namespace Graphics;

void CGBufferGenPass::Init()
{
    m_GBufferGenSig.Reset(4, 0);
    m_GBufferGenSig[0].InitAsConstantBuffer(0);
    m_GBufferGenSig[1].InitAsConstantBuffer(1);
    m_GBufferGenSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    m_GBufferGenSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    m_GBufferGenSig.Finalize(L"m_GBufferGenSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    D3D12_INPUT_ELEMENT_DESC vertElem[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    DXGI_FORMAT GBufferFormat[1] = { g_SceneGBufferA.GetFormat() };
    DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();

    // PSO
    {
        std::shared_ptr<SCompiledShaderCode> pVsShaderCode = GetGlobalResource().m_ShaderCompiler.Compile(L"Shaders/GBufferGenVS.hlsl", L"main", L"vs_6_5", nullptr, 0);
        std::shared_ptr<SCompiledShaderCode> pPsShaderCode = GetGlobalResource().m_ShaderCompiler.Compile(L"Shaders/GBufferGenPS.hlsl", L"main", L"ps_6_5", nullptr, 0);

       m_GBufferGenPso = GraphicsPSO(L"m_GBufferGenPso");
       m_GBufferGenPso.SetRootSignature(m_GBufferGenSig);
       m_GBufferGenPso.SetRasterizerState(RasterizerDefault);
       m_GBufferGenPso.SetBlendState(BlendDisable);
       m_GBufferGenPso.SetDepthStencilState(DepthStateReadOnly);
       m_GBufferGenPso.SetInputLayout(_countof(vertElem), vertElem);
       m_GBufferGenPso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
       m_GBufferGenPso.SetRenderTargetFormats(1, GBufferFormat, DepthFormat);
       m_GBufferGenPso.SetVertexShader(pVsShaderCode->GetBufferPointer(), pVsShaderCode->GetBufferSize());
       m_GBufferGenPso.SetPixelShader(pPsShaderCode->GetBufferPointer(), pPsShaderCode->GetBufferSize());
       m_GBufferGenPso.Finalize();
    }
}

void CGBufferGenPass::GenerateGBuffer(GraphicsContext& context, GlobalConstants& globals)
{
}
