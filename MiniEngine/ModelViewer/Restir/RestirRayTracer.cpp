#include "RestirRayTracer.h"
#include "DXSampleHelper.h"
#include "SponzaRenderer.h"
#include "RestirGlobalResource.h"
#include "Utility.h"
#include <iostream>


using namespace Graphics;
void CRestirRayTracer::Init()
{
    rtPsoDesc.filename = L"Shaders/RestirInitialSampling.hlsl";
    rtPsoDesc.rtShaders.push_back(SShader{ ERayShaderType::RAY_RGS ,L"InitialSamplingRayGen" });
	rtPsoDesc.rtShaders.push_back(SShader{ ERayShaderType::RAY_CHS ,L"InitialSamplingRayHit" });
	rtPsoDesc.rtShaders.push_back(SShader{ ERayShaderType::RAY_CHS ,L"ShadowClosestHitMain" });
	rtPsoDesc.rtShaders.push_back(SShader{ ERayShaderType::RAY_MIH ,L"RayMiassMain" });
    rtPsoDesc.rayTracingResources.m_nSRV = 3;
    rtPsoDesc.rayTracingResources.m_nUAV = 1;
    rtPsoDesc.rayTracingResources.m_nCBV = 1;

    ThrowIfFailed(g_Device->QueryInterface(IID_PPV_ARGS(&pRaytracingDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
    InitAccelerationStructure();
    CreateRTPipelineStateAndShaderTable();
    InitDesc();
}

static D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE wrapMode, uint32_t registerIndex, uint32_t space)
{
	D3D12_STATIC_SAMPLER_DESC Result = {};

	Result.Filter = filter;
	Result.AddressU = wrapMode;
	Result.AddressV = wrapMode;
	Result.AddressW = wrapMode;
	Result.MipLODBias = 0.0f;
	Result.MaxAnisotropy = 1;
	Result.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	Result.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	Result.MinLOD = 0.0f;
	Result.MaxLOD = D3D12_FLOAT32_MAX;
	Result.ShaderRegister = registerIndex;
	Result.RegisterSpace = space;
	Result.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	return Result;
}

static const D3D12_STATIC_SAMPLER_DESC gStaticSamplerDescs[] =
{
	MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_POINT,        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  0, 1000),
	MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_POINT,        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1, 1000),
	MakeStaticSampler(D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP,  2, 1000),
	MakeStaticSampler(D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 3, 1000),
	MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,       D3D12_TEXTURE_ADDRESS_MODE_WRAP,  4, 1000),
	MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,       D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 5, 1000),
};

void SGlobalRootSignature::Init(ID3D12Device5Ptr pDevice, CDxRayTracingPipelineState* pDxRayTracingPipelineState)
{
	std::vector<D3D12_ROOT_PARAMETER1> rootParams;
	std::vector<D3D12_DESCRIPTOR_RANGE1> descRanges;

	rootParams.resize(5);
	descRanges.resize(2);

	int descRangeIndex = 0;
	int rootIndex = 0;

	// Root SRV : TLAS
	{
		rootParams[rootIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[rootIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[rootIndex].Descriptor.ShaderRegister = 0;
		rootParams[rootIndex].Descriptor.RegisterSpace = 0;

		rootIndex++;
	}

	// Root Desc Table SRV:  t1 - t6
	{
		D3D12_DESCRIPTOR_RANGE1 descRange;
		descRange.NumDescriptors = 6;
		descRange.RegisterSpace = 0;
		descRange.OffsetInDescriptorsFromTableStart = 0;
		descRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
		descRange.BaseShaderRegister = 1;
		descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
		descRanges[descRangeIndex] = descRange;

		rootParams[rootIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[rootIndex].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[rootIndex].DescriptorTable.pDescriptorRanges = &descRanges[descRangeIndex];

		descRangeIndex++;
		rootIndex++;
	}
	

	// Root Desc Table UAV:
	{
		
		D3D12_DESCRIPTOR_RANGE1 descRange;
		descRange.NumDescriptors = 7;
		descRange.RegisterSpace = 0;
		descRange.OffsetInDescriptorsFromTableStart = 0;
		descRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
		descRange.BaseShaderRegister = 0;
		descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
		descRanges[descRangeIndex] = descRange;

		rootParams[rootIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[rootIndex].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[rootIndex].DescriptorTable.pDescriptorRanges = &descRanges[descRangeIndex];

		descRangeIndex++;
		rootIndex++;
	}

	//Root Constant: CBV
	{
		rootParams[rootIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[rootIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[rootIndex].Descriptor.ShaderRegister = 0;
		rootParams[rootIndex].Descriptor.RegisterSpace = 0;

		rootIndex++;
	}

	{
		D3D12_DESCRIPTOR_RANGE1 bindlessTexDescRange;
		bindlessTexDescRange.BaseShaderRegister = 0;
		bindlessTexDescRange.NumDescriptors = -1;
		bindlessTexDescRange.RegisterSpace = 2;
		bindlessTexDescRange.OffsetInDescriptorsFromTableStart = 0;
		bindlessTexDescRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		bindlessTexDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(D3D12_DESCRIPTOR_RANGE_TYPE_SRV);

		rootParams[rootIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[rootIndex].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[rootIndex].DescriptorTable.pDescriptorRanges = &bindlessTexDescRange;

		rootIndex++;
	}


	D3D12_ROOT_SIGNATURE_DESC1 rootSigDesc = {};
	rootSigDesc.NumParameters = rootParams.size();
	rootSigDesc.pParameters = rootParams.data();
	rootSigDesc.NumStaticSamplers = 6;
	rootSigDesc.pStaticSamplers = gStaticSamplerDescs;
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	m_pRootSig = CreateRootSignature(pDevice, rootSigDesc);
	m_pInterface = m_pRootSig.GetInterfacePtr();
	m_subobject.pDesc = &m_pInterface;
	m_subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
}

void CRestirRayTracer::GenerateInitialSampling(GraphicsContext& context)
{
	int cuurentFrameStartIndex = GetGlobalResource().getCurrentFrameBufferIndex();
	int write_buffer_index = (cuurentFrameStartIndex + 1) % 3;

    context.WriteBuffer(rayTracingConstantBuffer, 0, &GetGlobalResource().restirSRayTracingInfo, sizeof(SRestirRayTracingInfo));
    context.TransitionResource(rayTracingConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    context.TransitionResource(g_SceneGBufferA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(g_SceneGBufferB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    context.TransitionResource(g_ReservoirRayDirection[write_buffer_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.TransitionResource(g_ReservoirRayRadiance[write_buffer_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.TransitionResource(g_ReservoirRayDistance[write_buffer_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.TransitionResource(g_ReservoirRayNormal[write_buffer_index] , D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.TransitionResource(g_ReservoirRayWeights[write_buffer_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.TransitionResource(g_DownSampledWorldPosition[write_buffer_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.TransitionResource(g_DownSampledWorldNormal[write_buffer_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.FlushResourceBarriers();

	context.ClearUAV(g_ReservoirRayDirection[write_buffer_index]);
	context.ClearUAV(g_ReservoirRayRadiance[write_buffer_index]);
	context.ClearUAV(g_ReservoirRayDistance[write_buffer_index]);
	context.ClearUAV(g_ReservoirRayNormal[write_buffer_index]);
	context.ClearUAV(g_ReservoirRayWeights[write_buffer_index]);
	context.ClearUAV(g_DownSampledWorldPosition[write_buffer_index]);
	context.ClearUAV(g_DownSampledWorldNormal[write_buffer_index]);

    ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

    ComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
    pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

    ID3D12DescriptorHeap* pDescriptorHeaps[] = { &pRaytracingDescriptorHeap->GetDescriptorHeap() };
    pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
	pCommandList->SetComputeRootSignature(rayTracingPipelineState.m_pGlobalRootSig);
    
    // SRV -> UAV -> CBV
    pRaytracingCommandList->SetComputeRootShaderResourceView(0, tlasResource->GetGPUVirtualAddress());
    pCommandList->SetComputeRootDescriptorTable(1, raytracingResources);
	pCommandList->SetComputeRootDescriptorTable(2, outputReservoirs[write_buffer_index]);
	pCommandList->SetComputeRootConstantBufferView(3, rayTracingConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(4, bindlessTableHandle);

    D3D12_DISPATCH_RAYS_DESC rayDispatchDesc = CreateRayTracingDesc(g_ReservoirRayDirection[0].GetWidth(), g_ReservoirRayDirection[0].GetHeight());
    pRaytracingCommandList->SetPipelineState1(rayTracingPipelineState.m_pRtPipelineState);
    pRaytracingCommandList->DispatchRays(&rayDispatchDesc);

    context.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, nullptr);
}


