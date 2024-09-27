#pragma once
#include "RestirCommon.h"

class CRestirResamplingPass
{
public:
	void Init();
	void RestirResampling(ComputeContext& cptContext);
private:
	void TemporalResampling(ComputeContext& cptContext);
	void SpatialResampling(ComputeContext& cptContext, int read_reservoir_index, int write_reservoir_index, int world_pos_norm_index, int pass_index);

	RootSignature restirTemporalResamplingnSig;
	ComputePSO restirTemporalResamplingnPso;

	RootSignature restirSpatialResamplingnSig;
	ComputePSO restirSpatialResamplingnPso;
};