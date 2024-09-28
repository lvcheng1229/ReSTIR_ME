//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  James Stanard
//

#pragma once

#include <d3d12.h>

class GraphicsContext;
class ShadowCamera;
class ModelH3D;
class ExpVar;

namespace Math
{
    class Camera;
    class Vector3;
}

namespace Sponza
{
    void Startup( Math::Camera& camera );
    void Cleanup( void );

    void RenderScene(
        GraphicsContext& gfxContext,
        const Math::Camera& camera,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissor,
        bool skipDiffusePass = false,
        bool skipShadowMap = false );

    const ModelH3D& GetModel();

    extern Math::Vector3 m_SunDirection;
    extern ShadowCamera m_SunShadow;
    extern ExpVar m_AmbientIntensity;
    extern ExpVar m_SunLightIntensity;
    extern std::vector<bool> m_pMaterialIsCutout;
    enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };

    extern NumVar m_SunOrientation;
    extern NumVar m_SunInclination;

}
