#pragma once
#include "RestirCommon.h"

class CRestirResamplingPass
{
public:
	void Init();
private:
	RootSignature restirTemporalResamplingnSig;
	GraphicsPSO restirTemporalResamplingnPso;
};