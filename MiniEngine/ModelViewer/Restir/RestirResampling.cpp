#include "RestirResampling.h"
#include "RestirGlobalResource.h"

using namespace Graphics;

void CRestirResamplingPass::Init()
{
	restirTemporalResamplingnSig.Reset(3);
	restirTemporalResamplingnSig[0].InitAsConstantBuffer(0);
	restirTemporalResamplingnSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 15);
	restirTemporalResamplingnSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 5);
	restirTemporalResamplingnSig.Finalize(L"restirTemporalResamplingnSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	{
		std::shared_ptr<SCompiledShaderCode> pCsShaderCode = GetGlobalResource().m_ShaderCompilerFxc.Compile(L"Shaders/ReSTIRTemporalResampling.hlsl", L"TemporalResamplingCS", L"cs_5_1", nullptr, 0);
		restirTemporalResamplingnPso.SetRootSignature(restirTemporalResamplingnSig);
		restirTemporalResamplingnPso.SetComputeShader(pCsShaderCode->GetBufferPointer(), pCsShaderCode->GetBufferSize());
		restirTemporalResamplingnPso.Finalize();
	}

	restirSpatialResamplingnSig.Reset(4);
	restirSpatialResamplingnSig[0].InitAsConstantBuffer(0);
	restirSpatialResamplingnSig[1].InitAsConstantBuffer(1);
	restirSpatialResamplingnSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8);
	restirSpatialResamplingnSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 5);
	restirSpatialResamplingnSig.Finalize(L"restirTemporalResamplingnSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	{
		std::shared_ptr<SCompiledShaderCode> pCsShaderCode = GetGlobalResource().m_ShaderCompilerFxc.Compile(L"Shaders/ReSTIRSpatialResampling.hlsl", L"SpatialResamplingCS", L"cs_5_1", nullptr, 0);
		restirSpatialResamplingnPso.SetRootSignature(restirSpatialResamplingnSig);
		restirSpatialResamplingnPso.SetComputeShader(pCsShaderCode->GetBufferPointer(), pCsShaderCode->GetBufferSize());
		restirSpatialResamplingnPso.Finalize();
	}

}

void CRestirResamplingPass::RestirResampling(ComputeContext& cptContext)
{
	TemporalResampling(cptContext);

	int cuurentFrameStartIndex = GetGlobalResource().getCurrentFrameBufferIndex();
	SpatialResampling(cptContext, (cuurentFrameStartIndex + 2) % 3, (cuurentFrameStartIndex + 1) % 3, (cuurentFrameStartIndex + 1) % 3, 0);
	SpatialResampling(cptContext, (cuurentFrameStartIndex + 1) % 3, (cuurentFrameStartIndex + 2) % 3, (cuurentFrameStartIndex + 1) % 3, 0);
	SpatialResampling(cptContext, (cuurentFrameStartIndex + 2) % 3, (cuurentFrameStartIndex + 1) % 3, (cuurentFrameStartIndex + 1) % 3, 0);
}

void CRestirResamplingPass::TemporalResampling(ComputeContext& cptContext)
{
	cptContext.SetRootSignature(restirTemporalResamplingnSig);
	cptContext.SetPipelineState(restirTemporalResamplingnPso);

	int cuurentFrameStartIndex = GetGlobalResource().getCurrentFrameBufferIndex();
	for (int index = 0; index < 2; index++)
	{
		//0: History 1: CurrentFrameRayTracingResult
		int bufferIndex = (cuurentFrameStartIndex + index) % 3;
		cptContext.TransitionResource(g_ReservoirRayDirection[bufferIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(g_ReservoirRayRadiance[bufferIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(g_ReservoirRayDistance[bufferIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(g_ReservoirRayNormal[bufferIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(g_ReservoirRayWeights[bufferIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(g_DownSampledWorldPosition[bufferIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(g_DownSampledWorldNormal[bufferIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	cptContext.TransitionResource(g_ReservoirRayDirection[cuurentFrameStartIndex + 2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_ReservoirRayRadiance[cuurentFrameStartIndex + 2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_ReservoirRayDistance[cuurentFrameStartIndex + 2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_ReservoirRayNormal[cuurentFrameStartIndex + 2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_ReservoirRayWeights[cuurentFrameStartIndex + 2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_DownSampledWorldPosition[cuurentFrameStartIndex + 2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_DownSampledWorldNormal[cuurentFrameStartIndex + 2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	
	cptContext.FlushResourceBarriers();

	// CBV
	cptContext.SetDynamicConstantBufferView(0, sizeof(SRestirSceneInfo), &GetGlobalResource().restirSceneInfo);

	// SRV
	cptContext.SetDynamicDescriptor(1, 0, g_ReservoirRayDirection[cuurentFrameStartIndex + 0].GetSRV());
	cptContext.SetDynamicDescriptor(1, 1, g_ReservoirRayRadiance[cuurentFrameStartIndex + 0].GetSRV());
	cptContext.SetDynamicDescriptor(1, 2, g_ReservoirRayDistance[cuurentFrameStartIndex + 0].GetSRV());
	cptContext.SetDynamicDescriptor(1, 3, g_ReservoirRayNormal[cuurentFrameStartIndex + 0].GetSRV());
	cptContext.SetDynamicDescriptor(1, 4, g_ReservoirRayWeights[cuurentFrameStartIndex + 0].GetSRV());
	cptContext.SetDynamicDescriptor(1, 5, g_DownSampledWorldPosition[cuurentFrameStartIndex + 0].GetSRV());
	cptContext.SetDynamicDescriptor(1, 6, g_DownSampledWorldNormal[cuurentFrameStartIndex + 0].GetSRV());

	// current frame index
	int current_frame_buffer_index = (cuurentFrameStartIndex + 1) % 3;
	cptContext.SetDynamicDescriptor(1, 7, g_ReservoirRayDirection[current_frame_buffer_index].GetSRV());
	cptContext.SetDynamicDescriptor(1, 8, g_ReservoirRayRadiance[current_frame_buffer_index].GetSRV());
	cptContext.SetDynamicDescriptor(1, 9, g_ReservoirRayDistance[current_frame_buffer_index].GetSRV());
	cptContext.SetDynamicDescriptor(1, 10, g_ReservoirRayNormal[current_frame_buffer_index].GetSRV());
	cptContext.SetDynamicDescriptor(1, 11, g_ReservoirRayWeights[current_frame_buffer_index].GetSRV());
	cptContext.SetDynamicDescriptor(1, 12, g_DownSampledWorldPosition[current_frame_buffer_index].GetSRV());
	cptContext.SetDynamicDescriptor(1, 13, g_DownSampledWorldNormal[current_frame_buffer_index].GetSRV());

	cptContext.SetDynamicDescriptor(1, 14, GetGlobalResource().STBNScalar.GetSRV());

	// SRV
	int write_buffer_index = (cuurentFrameStartIndex + 2) % 3;
	cptContext.SetDynamicDescriptor(2, 0, g_ReservoirRayDirection[write_buffer_index].GetUAV());
	cptContext.SetDynamicDescriptor(2, 1, g_ReservoirRayRadiance[write_buffer_index].GetUAV());
	cptContext.SetDynamicDescriptor(2, 2, g_ReservoirRayDistance[write_buffer_index].GetUAV());
	cptContext.SetDynamicDescriptor(2, 3, g_ReservoirRayNormal[write_buffer_index].GetUAV());
	cptContext.SetDynamicDescriptor(2, 4, g_ReservoirRayWeights[write_buffer_index].GetUAV());

	cptContext.Dispatch(DeviceAndRoundUp(GetGlobalResource().restirSceneInfo.g_restir_texturesize.x, 16), DeviceAndRoundUp(GetGlobalResource().restirSceneInfo.g_restir_texturesize.y, 16), 1);
}

void CRestirResamplingPass::SpatialResampling(ComputeContext& cptContext, int read_reservoir_index, int write_reservoir_index, int world_pos_norm_index, int pass_index)
{
	cptContext.SetRootSignature(restirSpatialResamplingnSig);
	cptContext.SetPipelineState(restirSpatialResamplingnPso);

	cptContext.TransitionResource(g_ReservoirRayDirection[read_reservoir_index], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(g_ReservoirRayRadiance[read_reservoir_index], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(g_ReservoirRayDistance[read_reservoir_index], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(g_ReservoirRayNormal[read_reservoir_index], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(g_ReservoirRayWeights[read_reservoir_index], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	
	cptContext.TransitionResource(g_DownSampledWorldPosition[world_pos_norm_index], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(g_DownSampledWorldNormal[world_pos_norm_index], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	cptContext.TransitionResource(g_ReservoirRayDirection[write_reservoir_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_ReservoirRayRadiance[write_reservoir_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_ReservoirRayDistance[write_reservoir_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_ReservoirRayNormal[write_reservoir_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(g_ReservoirRayWeights[write_reservoir_index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	cptContext.FlushResourceBarriers();

	struct SSpatialResamplingIndex
	{
		int spatial_resampling_index;
		int padding[7];
	};

	SSpatialResamplingIndex spatial_resampling_index;
	spatial_resampling_index.spatial_resampling_index = pass_index;

	cptContext.SetDynamicConstantBufferView(0, sizeof(SRestirSceneInfo), &GetGlobalResource().restirSceneInfo);
	cptContext.SetDynamicConstantBufferView(1, sizeof(SSpatialResamplingIndex), &spatial_resampling_index);

	cptContext.SetDynamicDescriptor(2, 0, g_ReservoirRayDirection[read_reservoir_index].GetSRV());
	cptContext.SetDynamicDescriptor(2, 1, g_ReservoirRayRadiance[read_reservoir_index].GetSRV());
	cptContext.SetDynamicDescriptor(2, 2, g_ReservoirRayDistance[read_reservoir_index].GetSRV());
	cptContext.SetDynamicDescriptor(2, 3, g_ReservoirRayNormal[read_reservoir_index].GetSRV());
	cptContext.SetDynamicDescriptor(2, 4, g_ReservoirRayWeights[read_reservoir_index].GetSRV());
									
	cptContext.SetDynamicDescriptor(2, 5, g_DownSampledWorldPosition[world_pos_norm_index].GetSRV());
	cptContext.SetDynamicDescriptor(2, 6, g_DownSampledWorldNormal[world_pos_norm_index].GetSRV());
									
	cptContext.SetDynamicDescriptor(2, 7, GetGlobalResource().STBNScalar.GetSRV());

	cptContext.SetDynamicDescriptor(3, 0, g_ReservoirRayDirection[write_reservoir_index].GetUAV());
	cptContext.SetDynamicDescriptor(3, 1, g_ReservoirRayRadiance[write_reservoir_index].GetUAV());
	cptContext.SetDynamicDescriptor(3, 2, g_ReservoirRayDistance[write_reservoir_index].GetUAV());
	cptContext.SetDynamicDescriptor(3, 3, g_ReservoirRayNormal[write_reservoir_index].GetUAV());
	cptContext.SetDynamicDescriptor(3, 4, g_ReservoirRayWeights[write_reservoir_index].GetUAV());

	cptContext.Dispatch(DeviceAndRoundUp(GetGlobalResource().restirSceneInfo.g_restir_texturesize.x , 16), DeviceAndRoundUp( GetGlobalResource().restirSceneInfo.g_restir_texturesize.y , 16), 1);
}
