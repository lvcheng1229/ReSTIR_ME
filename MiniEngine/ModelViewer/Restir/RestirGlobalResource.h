#pragma once
#include "RestirCommon.h"
#include "ShaderCompile.h"

struct SRestirSceneInfo
{
	DirectX::XMFLOAT3 lightDirection;
	float padding1;

	DirectX::XMFLOAT4 restirTextureSize;
	DirectX::XMFLOAT4 fullScreenTextureSize;
};

struct SGlobalResource
{
	CShaderCompiler m_ShaderCompiler;
	SRestirSceneInfo restirSceneInfo;
	ModelInstance* pModelInst;
};

SGlobalResource& GetGlobalResource();
void InitGlobalResource();