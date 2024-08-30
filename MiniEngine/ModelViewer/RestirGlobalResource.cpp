#include "RestirGlobalResource.h"

static SGlobalResource globalResource;

SGlobalResource& GetGlobalResource()
{
	return globalResource;
}

void InitGlobalResource()
{
	globalResource.m_ShaderCompiler.Init(false);
}
