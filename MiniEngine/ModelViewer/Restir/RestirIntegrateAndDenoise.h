#pragma once
#include "RestirCommon.h"

class CRestirIntegrateAndDenoise
{
public:
	void Init();
	void IntegrateAndDenoise(ComputeContext& cptContext);
private:

	void RestirIntegrate(ComputeContext& cptContext);
	void RestirDenoise(ComputeContext& cptContext);

	RootSignature restirIntegrateSig;
	ComputePSO restirIntegratePso;

	RootSignature restirDenoiseSig;
	ComputePSO restirDenoisePso;
};