#include "RestirResampling.h"
#include "RestirGlobalResource.h"

void CRestirResamplingPass::Init()
{
	restirTemporalResamplingnSig.Reset(3);
	restirTemporalResamplingnSig[0].InitAsConstantBuffer(0);
	restirTemporalResamplingnSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 15);
	restirTemporalResamplingnSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 5);
	restirTemporalResamplingnSig.Finalize(L"restirTemporalResamplingnSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	{
		std::shared_ptr<SCompiledShaderCode> pCsShaderCode = GetGlobalResource().m_ShaderCompiler.Compile(L"Shaders/ReSTIRTemporalResampling.hlsl", L"TemporalResamplingCS", L"cs_6_5", nullptr, 0);
	}
}
