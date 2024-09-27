#include "RestirRayTracer.h"
#include "DXSampleHelper.h"
#include "SponzaRenderer.h"
#include "RestirGlobalResource.h"
#include "Utility.h"
#include <iostream>


using namespace Graphics;
void CRestirRayTracer::Init()
{
    rtPsoDesc.filename = L"Shaders/RestirTestShader.hlsl";
    rtPsoDesc.rtShaders.push_back(SShader{ ERayShaderType::RAY_RGS ,L"InitialSamplingRayGen" });
    rtPsoDesc.rtShaders.push_back(SShader{ ERayShaderType::RAY_CHS ,L"InitialSamplingRayHit" });
    rtPsoDesc.rayTracingResources.m_nSRV = 3;
    rtPsoDesc.rayTracingResources.m_nUAV = 1;
    rtPsoDesc.rayTracingResources.m_nCBV = 1;

    ThrowIfFailed(g_Device->QueryInterface(IID_PPV_ARGS(&pRaytracingDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
    InitAccelerationStructure();
    CreateRTPipelineStateAndShaderTable();
    InitDesc();

    //{
    //    TempRootSignature.Reset(3, 0);
    //    TempRootSignature[0].InitAsConstantBuffer(0);
    //    TempRootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 3, D3D12_SHADER_VISIBILITY_ALL);
    //    TempRootSignature[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1, D3D12_SHADER_VISIBILITY_ALL);
    //    TempRootSignature.Finalize(L"TempRootSignature", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    //}
}

void CRestirRayTracer::GenerateInitialSampling(GraphicsContext& context)
{
    context.TransitionResource(g_SceneGBufferA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(g_SceneGBufferB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    context.TransitionResource(g_ReservoirRayDirection[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.FlushResourceBarriers();

    ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

    ComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
    pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));
    
    ID3D12DescriptorHeap* pDescriptorHeaps[] = { &pRaytracingDescriptorHeap->GetDescriptorHeap() };
    pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
    pRaytracingCommandList->SetPipelineState1(rayTracingPipelineState.m_pRtPipelineState);
    pRaytracingCommandList->SetComputeRootSignature(rayTracingPipelineState.m_pGlobalRootSig);
    
    pCommandList->SetComputeRootDescriptorTable(0, g_SceneSrvs);

    context.SetRootSignature(TempRootSignature);




    // srv uav cbv
    context.SetDynamicDescriptor(2, 0, g_SceneGBufferA.GetSRV());
    context.SetDynamicDescriptor(2, 1, g_SceneGBufferB.GetSRV());
    context.SetDynamicDescriptor(2, 2, rayTracingTlas.GetSRV());
    context.SetDynamicDescriptor(1, 0, g_ReservoirRayDirection[0].GetUAV());
    context.SetDynamicConstantBufferView(0, sizeof(SRestirSceneInfoTemp), &GetGlobalResource().restirSceneInfoTemp);
    context.RayTracingCommitDescTable();

    //pRaytracingCommandList->SetComputeRootDescriptorTable(4, g_OutputUAV);

    D3D12_DISPATCH_RAYS_DESC rayDispatchDesc = CreateRayTracingDesc(g_ReservoirRayDirection[0].GetWidth(), g_ReservoirRayDirection[0].GetHeight());
    pRaytracingCommandList->DispatchRays(&rayDispatchDesc);

    context.TransitionResource(g_ReservoirRayDirection[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    context.FlushResourceBarriers();
}
