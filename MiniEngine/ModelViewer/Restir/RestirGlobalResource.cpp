#include "RestirGlobalResource.h"

static SGlobalResource globalResource;

SGlobalResource& GetGlobalResource()
{
	return globalResource;
}

void InitGlobalResource()
{
	globalResource.m_ShaderCompilerFxc.Init(true);
	globalResource.m_ShaderCompilerDxc.Init(false);
	globalResource.currentFrameIndex = 0;
	
	const std::wstring stbnsPath = L"Resource/STBNS_128x128x64.DDS";
	globalResource.STBNScalar = TextureManager::LoadDDSFromFile(stbnsPath);

	const std::wstring stbnvec2Path = L"Resource/STBNVec2_128x128x64.DDS";
	globalResource.STBNVec2 = TextureManager::LoadDDSFromFile(stbnvec2Path);
}
