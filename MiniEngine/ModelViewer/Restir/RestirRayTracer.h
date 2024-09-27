#pragma once
#include "RestirCommon.h"
#include "ModelH3D.h"
#include "RestirGlobalResource.h"

enum class ERayShaderType
{
	RAY_RGS, // ray gen shader
	RAY_MIH, // ray miss shader 
	RAY_AHS, // any hit shader
	RAY_CHS, // close hit shader
};

struct SShader
{
	ERayShaderType m_eShaderType;
	const std::wstring m_entryPoint;
};

struct SShaderResources
{
	uint32_t m_nSRV = 0;
	uint32_t m_nUAV = 0;
	uint32_t m_nCBV = 0;
	uint32_t m_nSampler = 0;
	uint32_t m_rootConstant = 0;

	uint32_t m_useBindlessTex2D = false;
	uint32_t m_useByteAddressBuffer = false;

	uint32_t operator[](std::size_t index)
	{
		return *((uint32_t*)(this) + index);
	}
};

struct SRayTracingPSOCreateDesc
{
	std::wstring filename;
	std::vector<SShader>rtShaders;
	SShaderResources rayTracingResources;
};



class CDxRayTracingPipelineState
{
public:
	ID3D12RootSignaturePtr m_pGlobalRootSig;
	ID3D12StateObjectPtr m_pRtPipelineState;
	//ID3D12ResourcePtr m_pShaderTable;
	uint32_t m_nShaderNum[4];
	uint32_t m_slotDescNum[4];
	
	uint32_t m_byteBufferBindlessRootTableIndex;
	uint32_t m_tex2DBindlessRootTableIndex;
	uint32_t m_rootConstantRootTableIndex;

	CDxRayTracingPipelineState()
	{
		m_pGlobalRootSig = nullptr;
		//m_pShaderTable = nullptr;
		m_pRtPipelineState = nullptr;
		m_nShaderNum[0] = m_nShaderNum[1] = m_nShaderNum[2] = m_nShaderNum[3] = 0;
		m_slotDescNum[0] = m_slotDescNum[1] = m_slotDescNum[2] = m_slotDescNum[3] = 0;
		
		m_byteBufferBindlessRootTableIndex = 0;
		m_tex2DBindlessRootTableIndex = 0;
		m_rootConstantRootTableIndex = 0;
	}
};

//class CRayTracingTLAS : public GpuResource
//{
//public:
//	CRayTracingTLAS() {};
//	CRayTracingTLAS(ID3D12Resource* pResource, D3D12_RESOURCE_STATES CurrentState, D3D12_CPU_DESCRIPTOR_HANDLE tlasSRVCpuHandle)
//		:GpuResource(pResource, CurrentState)
//		, m_SRVHandle(tlasSRVCpuHandle) {};
//
//	const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV(void) const { return m_SRVHandle; }
//private:
//
//	D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
//};

class DescriptorHeapStack
{
public:
	DescriptorHeapStack(ID3D12Device& device, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT NodeMask) :
		m_device(device)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = numDescriptors;
		desc.Type = type;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NodeMask = NodeMask;
		device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pDescriptorHeap));

		m_descriptorSize = device.GetDescriptorHandleIncrementSize(type);
		m_descriptorHeapCpuBase = m_pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	}

	ID3D12DescriptorHeap& GetDescriptorHeap() { return *m_pDescriptorHeap.Get(); }

	void AllocateDescriptor(_Out_ D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle, _Out_ UINT& descriptorHeapIndex)
	{
		descriptorHeapIndex = m_descriptorsAllocated;
		cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_descriptorHeapCpuBase, descriptorHeapIndex, m_descriptorSize);
		m_descriptorsAllocated++;
	}

	UINT AllocateBufferSrv(_In_ ID3D12Resource& resource)
	{
		UINT descriptorHeapIndex;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		AllocateDescriptor(cpuHandle, descriptorHeapIndex);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = (UINT)(resource.GetDesc().Width / sizeof(UINT32));
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		m_device.CreateShaderResourceView(&resource, &srvDesc, cpuHandle);
		return descriptorHeapIndex;
	}

	UINT AllocateBufferUav(_In_ ID3D12Resource& resource)
	{
		UINT descriptorHeapIndex;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		AllocateDescriptor(cpuHandle, descriptorHeapIndex);
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = (UINT)(resource.GetDesc().Width / sizeof(UINT32));
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
		uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;

		m_device.CreateUnorderedAccessView(&resource, nullptr, &uavDesc, cpuHandle);
		return descriptorHeapIndex;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT descriptorIndex)
	{
		return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_descriptorSize);
	}
private:
	ID3D12Device& m_device;
	ComPtr<ID3D12DescriptorHeap> m_pDescriptorHeap;
	UINT m_descriptorsAllocated = 0;
	UINT m_descriptorSize;
	D3D12_CPU_DESCRIPTOR_HANDLE m_descriptorHeapCpuBase;
};



class CRestirRayTracer
{
public:
	CRestirRayTracer() {};
	void Init();
	void GenerateInitialSampling(GraphicsContext& context);
private:
	void CreateRTPipelineStateAndShaderTable();
	void InitAccelerationStructure();
	void InitDesc();

	D3D12_DISPATCH_RAYS_DESC CreateRayTracingDesc(uint32_t width, uint32_t height);
	//CRayTracingTLAS rayTracingTlas;
	CDxRayTracingPipelineState rayTracingPipelineState;
	ID3D12ResourcePtr  tlasResource;

	ByteAddressBuffer ShaderTableBuffer;
	ID3D12Device5Ptr pRaytracingDevice;
	SRayTracingPSOCreateDesc rtPsoDesc;

	//RootSignature TempRootSignature;
	std::unique_ptr<DescriptorHeapStack> pRaytracingDescriptorHeap;

	D3D12_GPU_DESCRIPTOR_HANDLE gBufferARTCopySRV;
	D3D12_GPU_DESCRIPTOR_HANDLE gBufferBRTCopySRV;

	D3D12_GPU_DESCRIPTOR_HANDLE rtCopyReservoirRayDirection[3];
	D3D12_GPU_DESCRIPTOR_HANDLE rtCopyReservoirRayRadiance[3];
	D3D12_GPU_DESCRIPTOR_HANDLE rtCopyReservoirRayDistance[3]; 
	D3D12_GPU_DESCRIPTOR_HANDLE rtCopyReservoirRayNormal[3];
	D3D12_GPU_DESCRIPTOR_HANDLE rtCopyReservoirRayWeights[3];
	D3D12_GPU_DESCRIPTOR_HANDLE rtCopyDownSampledWorldPosition[3];
	D3D12_GPU_DESCRIPTOR_HANDLE rtCopyDownSampledWorldNormal[3];
};