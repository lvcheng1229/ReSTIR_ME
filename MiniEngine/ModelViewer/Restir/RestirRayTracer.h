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

class CRayTracingTLAS : public GpuResource
{
public:
	CRayTracingTLAS() {};
	CRayTracingTLAS(ID3D12Resource* pResource, D3D12_RESOURCE_STATES CurrentState, D3D12_CPU_DESCRIPTOR_HANDLE tlasSRVCpuHandle)
		:GpuResource(pResource, CurrentState)
		, m_SRVHandle(tlasSRVCpuHandle) {};

	const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV(void) const { return m_SRVHandle; }
private:

	D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
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

	D3D12_DISPATCH_RAYS_DESC CreateRayTracingDesc(uint32_t width, uint32_t height);
	CRayTracingTLAS rayTracingTlas;
	CDxRayTracingPipelineState rayTracingPipelineState;

	ByteAddressBuffer ShaderTableBuffer;
	ID3D12Device5Ptr pRaytracingDevice;
	SRayTracingPSOCreateDesc rtPsoDesc;

	RootSignature TempRootSignature;
};