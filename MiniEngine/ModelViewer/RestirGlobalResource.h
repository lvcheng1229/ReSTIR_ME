#pragma once
#include "RestirCommon.h"
#include "ShaderCompile.h"

struct SGlobalResource
{
	CShaderCompiler m_ShaderCompiler;
};

SGlobalResource& GetGlobalResource();
void InitGlobalResource();