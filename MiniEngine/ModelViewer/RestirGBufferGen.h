#pragma once
#include "RestirCommon.h"

class CGBufferGenPass
{
public:
	void Init();
	void GenerateGBuffer(GraphicsContext& context, GlobalConstants& globals, std::vector<Renderer::MeshSorter::SortObject>& sortObjects);
private:		
	RootSignature m_GBufferGenSig;
	GraphicsPSO m_GBufferGenPso;
};