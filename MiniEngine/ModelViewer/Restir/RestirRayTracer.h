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
	uint32_t m_nShaderNum[4];
	uint32_t m_slotDescNum[4];
	
	uint32_t m_byteBufferBindlessRootTableIndex;
	uint32_t m_tex2DBindlessRootTableIndex;
	uint32_t m_rootConstantRootTableIndex;

	CDxRayTracingPipelineState()
	{
		m_pGlobalRootSig = nullptr;
		m_pRtPipelineState = nullptr;
		m_nShaderNum[0] = m_nShaderNum[1] = m_nShaderNum[2] = m_nShaderNum[3] = 0;
		m_slotDescNum[0] = m_slotDescNum[1] = m_slotDescNum[2] = m_slotDescNum[3] = 0;
		
		m_byteBufferBindlessRootTableIndex = 0;
		m_tex2DBindlessRootTableIndex = 0;
		m_rootConstantRootTableIndex = 0;
	}
};

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

	ID3D12DescriptorHeap& GetDescriptorHeap() { return *m_pDescriptorHeap; }

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
	ID3D12DescriptorHeapPtr m_pDescriptorHeap;
	UINT m_descriptorsAllocated = 0;
	UINT m_descriptorSize;
	D3D12_CPU_DESCRIPTOR_HANDLE m_descriptorHeapCpuBase;
};

ID3D12RootSignaturePtr CreateRootSignature(ID3D12Device5Ptr pDevice, const D3D12_ROOT_SIGNATURE_DESC1& desc);

struct RayTraceMeshInfo
{
	uint32_t  m_indexOffsetBytes;
	uint32_t  m_uvAttributeOffsetBytes;
	uint32_t  m_normalAttributeOffsetBytes;
	uint32_t  m_tangentAttributeOffsetBytes;
	uint32_t  m_bitangentAttributeOffsetBytes;
	uint32_t  m_positionAttributeOffsetBytes;
	uint32_t  m_attributeStrideBytes;
	uint32_t  m_materialInstanceId;
};

struct SGlobalRootSignature
{
	void Init(ID3D12Device5Ptr pDevice, CDxRayTracingPipelineState* pDxRayTracingPipelineState);
	ID3D12RootSignaturePtr m_pRootSig;
	ID3D12RootSignature* m_pInterface = nullptr;
	D3D12_STATE_SUBOBJECT m_subobject = {};
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
	CDxRayTracingPipelineState rayTracingPipelineState;
	ID3D12ResourcePtr  tlasResource;

	ByteAddressBuffer ShaderTableBuffer;
	ID3D12Device5Ptr pRaytracingDevice;
	SRayTracingPSOCreateDesc rtPsoDesc;

	SGlobalRootSignature globalRootSignature;

	//RootSignature TempRootSignature;
	std::unique_ptr<DescriptorHeapStack> pRaytracingDescriptorHeap;

	ByteAddressBuffer rayTracingConstantBuffer;


	D3D12_GPU_DESCRIPTOR_HANDLE outputReservoirs[3];
	StructuredBuffer    hitShaderMeshInfoBuffer;
	D3D12_GPU_DESCRIPTOR_HANDLE raytracingResources; // t1 - t6
	D3D12_GPU_DESCRIPTOR_HANDLE bindlessTableHandle; // bindless
};