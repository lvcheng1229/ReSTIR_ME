#include "RestirRayTracer.h"
#include "DXSampleHelper.h"
#include "SponzaRenderer.h"
#include "RestirGlobalResource.h"
#include "Utility.h"
#include <iostream>

using namespace Graphics;
static constexpr uint32_t MaxPayloadSizeInBytes = 32 * sizeof(float);
static constexpr uint32_t ShaderTableEntrySize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;


ID3D12ResourcePtr  g_bvh_topLevelAccelerationStructure;
std::vector<ID3D12ResourcePtr>   g_bvh_bottomLevelAccelerationStructures;
ComPtr<ID3D12RootSignature> g_GlobalRaytracingRootSignature;


#define BINDLESS_BYTE_ADDRESS_BUFFER_SPACE 1 /*space2*/
#define BINDLESS_TEXTURE_SPACE 2 /*space3*/

static D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE wrapMode, uint32_t registerIndex, uint32_t space)
{
    D3D12_STATIC_SAMPLER_DESC Result = {};

    Result.Filter = filter;
    Result.AddressU = wrapMode;
    Result.AddressV = wrapMode;
    Result.AddressW = wrapMode;
    Result.MipLODBias = 0.0f;
    Result.MaxAnisotropy = 1;
    Result.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    Result.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    Result.MinLOD = 0.0f;
    Result.MaxLOD = D3D12_FLOAT32_MAX;
    Result.ShaderRegister = registerIndex;
    Result.RegisterSpace = space;
    Result.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    return Result;
}

static const D3D12_STATIC_SAMPLER_DESC gStaticSamplerDescs[] =
{
    MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_POINT,        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  0, 1000),
    MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_POINT,        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1, 1000),
    MakeStaticSampler(D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP,  2, 1000),
    MakeStaticSampler(D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 3, 1000),
    MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,       D3D12_TEXTURE_ADDRESS_MODE_WRAP,  4, 1000),
    MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,       D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 5, 1000),
};

static ID3D12RootSignaturePtr CreateRootSignature(ID3D12Device5Ptr pDevice, const D3D12_ROOT_SIGNATURE_DESC1& desc)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedRootDesc;
    versionedRootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedRootDesc.Desc_1_1 = desc;

    ID3DBlobPtr pSigBlob;
    ID3DBlobPtr pErrorBlob;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&versionedRootDesc, &pSigBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        std::string msg = ConvertBlobToString(pErrorBlob.GetInterfacePtr());
        std::cout << msg;
        ThrowIfFailed(-1);
    }
    ID3D12RootSignaturePtr pRootSig;
    ThrowIfFailed(pDevice->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig)));
    return pRootSig;
};

void CRestirRayTracer::InitAccelerationStructure()
{
   //g_pRaytracingDescriptorHeap = std::unique_ptr<DescriptorHeapStack>(new DescriptorHeapStack(*g_Device, 200, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 0));
   
   const ModelH3D& model = Sponza::GetModel();
   //const ModelH3D& model = *(GetGlobalResource().pModelInst);
   
   //
   // Define the top level acceleration structure
   //
   const UINT numMeshes = model.m_Header.meshCount;
   const UINT numInstances = numMeshes;
   
   // You can toggle between all meshes in one BLAS or one instance per mesh. Typically, to save memory, you would instance a BLAS multiple
   // times when geometry is duplicated.
   ASSERT(numInstances == 1 || numInstances == numMeshes);
   
   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo;
   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelAccelerationStructureDesc = {};
   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& topLevelInputs = topLevelAccelerationStructureDesc.Inputs;
   topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
   topLevelInputs.NumDescs = numInstances;
   topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
   topLevelInputs.pGeometryDescs = nullptr;
   topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
   pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
   
   const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlag = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
   std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(model.m_Header.meshCount);
   UINT64 scratchBufferSizeNeeded = topLevelPrebuildInfo.ScratchDataSizeInBytes;
   
   ByteAddressBuffer tlasScratchBuffer;
   tlasScratchBuffer.Create(L"Acceleration Structure Scratch Buffer", (UINT)scratchBufferSizeNeeded, 1);
   
   D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
   auto tlasBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
   g_Device->CreateCommittedResource(
       &defaultHeapProps,
       D3D12_HEAP_FLAG_NONE,
       &tlasBufferDesc,
       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
       nullptr,
       IID_PPV_ARGS(&g_bvh_topLevelAccelerationStructure));
   
   topLevelAccelerationStructureDesc.DestAccelerationStructureData = g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
   topLevelAccelerationStructureDesc.ScratchAccelerationStructureData = tlasScratchBuffer.GetGpuVirtualAddress();
   
   //
   // Define the bottom level acceleration structures
   //
   
   for (UINT i = 0; i < numMeshes; i++)
   {
       auto& mesh = model.m_pMesh[i];
   
       D3D12_RAYTRACING_GEOMETRY_DESC& desc = geometryDescs[i];
       desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
       desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
   
       D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& trianglesDesc = desc.Triangles;
       trianglesDesc.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
       trianglesDesc.VertexCount = mesh.vertexCount;
       trianglesDesc.VertexBuffer.StartAddress = model.GetVertexBuffer().BufferLocation + (mesh.vertexDataByteOffset + (UINT)mesh.attrib[ModelH3D::attrib_position].offset);
       trianglesDesc.IndexBuffer = model.GetIndexBuffer().BufferLocation + mesh.indexDataByteOffset;
       trianglesDesc.VertexBuffer.StrideInBytes = mesh.vertexStride;
       trianglesDesc.IndexCount = mesh.indexCount;
       trianglesDesc.IndexFormat = DXGI_FORMAT_R16_UINT;
       trianglesDesc.Transform3x4 = 0;
   }
   
   g_bvh_bottomLevelAccelerationStructures.resize(numInstances);
   std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> blasDescs(numInstances);
   std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(numInstances);
   std::vector<ByteAddressBuffer> blasScratchBuffers(numInstances);
   
   for (UINT i = 0; i < numInstances; i++)
   {
       D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& blasDesc = blasDescs[i];
       blasDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
       blasDesc.Inputs.NumDescs = (numInstances == numMeshes) ? 1 : numMeshes;
       blasDesc.Inputs.pGeometryDescs = &geometryDescs[i];
       blasDesc.Inputs.Flags = buildFlag;
       blasDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
   
       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo;
       pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&blasDesc.Inputs, &bottomLevelPrebuildInfo);
   
       auto& blas = g_bvh_bottomLevelAccelerationStructures[i];
   
       auto blasBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
       g_Device->CreateCommittedResource(
           &defaultHeapProps,
           D3D12_HEAP_FLAG_NONE,
           &blasBufferDesc,
           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
           nullptr,
           IID_PPV_ARGS(&blas));
   
       blasDesc.DestAccelerationStructureData = blas->GetGPUVirtualAddress();
   
       blasScratchBuffers[i].Create(L"BLAS build scratch buffer", (UINT)bottomLevelPrebuildInfo.ScratchDataSizeInBytes, 1);
       blasDesc.ScratchAccelerationStructureData = blasScratchBuffers[i].GetGpuVirtualAddress();
       {
           //UINT descriptorHeapIndex;
           D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
   
           D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
           uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
           uavDesc.Buffer.NumElements = (UINT)(blas->GetDesc().Width / sizeof(UINT32));
           uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
           uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
   
           Graphics::g_Device->CreateUnorderedAccessView(blas, nullptr, &uavDesc, cpuHandle);
       }
   
       D3D12_RAYTRACING_INSTANCE_DESC& instanceDesc = instanceDescs[i];
       // Identity matrix
       ZeroMemory(instanceDesc.Transform, sizeof(instanceDesc.Transform));
       instanceDesc.Transform[0][0] = 1.0f;
       instanceDesc.Transform[1][1] = 1.0f;
       instanceDesc.Transform[2][2] = 1.0f;
   
       instanceDesc.AccelerationStructure = blas->GetGPUVirtualAddress();
       instanceDesc.Flags = 0;
       instanceDesc.InstanceID = 0;
       instanceDesc.InstanceMask = 1;
       instanceDesc.InstanceContributionToHitGroupIndex = i;
   }
   
   // Upload the instance data
   
   ByteAddressBuffer instanceDataBuffer;
   instanceDataBuffer.Create(L"Instance Data Buffer", numInstances, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), instanceDescs.data());
   
   topLevelInputs.InstanceDescs = instanceDataBuffer.GetGpuVirtualAddress();
   topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
   
   // TLAS Desc
   {
       D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
       srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
       srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
       srvDesc.RaytracingAccelerationStructure.Location = g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
   
       //UINT descriptorHeapIndex;
       D3D12_CPU_DESCRIPTOR_HANDLE tlasSRVCpuHandle = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
   
       Graphics::g_Device->CreateShaderResourceView(nullptr, &srvDesc, tlasSRVCpuHandle);
       rayTracingTlas = CRayTracingTLAS(g_bvh_topLevelAccelerationStructure, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, tlasSRVCpuHandle);
   }
   
   // Build the acceleration structures
   
   GraphicsContext& gfxContext = GraphicsContext::Begin(L"Build Acceleration Structures");
   
   ComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
   gfxContext.GetCommandList()->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));
   
   auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
   for (UINT i = 0; i < blasDescs.size(); i++)
   {
       pRaytracingCommandList->BuildRaytracingAccelerationStructure(&blasDescs[i], 0, nullptr);
   }
   pRaytracingCommandList->ResourceBarrier(1, &uavBarrier);
   pRaytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelAccelerationStructureDesc, 0, nullptr);
   
   gfxContext.Finish(true);
}



struct SGlobalRootSignature
{
    void Init(ID3D12Device5Ptr pDevice, SShaderResources rayTracingResources, CDxRayTracingPipelineState* pDxRayTracingPipelineState)
    {
        std::vector<D3D12_ROOT_PARAMETER1> rootParams;
        std::vector<D3D12_DESCRIPTOR_RANGE1> descRanges;

        uint32_t totalRootNum = 0;
        for (uint32_t descRangeIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; descRangeIndex <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; descRangeIndex++)
        {
            uint32_t rangeNum = rayTracingResources[descRangeIndex];
            if (rangeNum > 0)
            {
                totalRootNum++;
            }
        }

        uint32_t rootParamSize = totalRootNum + rayTracingResources.m_rootConstant;

        if (rayTracingResources.m_useBindlessTex2D)
        {
            rootParamSize++;
        }

        if (rayTracingResources.m_useByteAddressBuffer)
        {
            rootParamSize++;
        }

        rootParams.resize(rootParamSize);
        descRanges.resize(totalRootNum);

        uint32_t rootTabbleIndex = 0;

        for (uint32_t descRangeIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; descRangeIndex <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; descRangeIndex++)
        {
            if (descRangeIndex == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
            {
                rootParams[rootTabbleIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                rootParams[rootTabbleIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParams[rootTabbleIndex].Descriptor.ShaderRegister = 0;
                rootParams[rootTabbleIndex].Descriptor.RegisterSpace = 0;
                rootTabbleIndex++;
            }
            else
            {
                uint32_t rangeNum = rayTracingResources[descRangeIndex];
                if (rangeNum > 0)
                {
                    D3D12_DESCRIPTOR_RANGE1 descRange;
                    descRange.BaseShaderRegister = 0;
                    descRange.NumDescriptors = rangeNum;
                    descRange.RegisterSpace = 0;
                    descRange.OffsetInDescriptorsFromTableStart = 0;
                    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(descRangeIndex);
                    descRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
                    descRanges[rootTabbleIndex] = descRange;

                    rootParams[rootTabbleIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    rootParams[rootTabbleIndex].DescriptorTable.NumDescriptorRanges = 1;
                    rootParams[rootTabbleIndex].DescriptorTable.pDescriptorRanges = &descRanges[rootTabbleIndex];

                    rootTabbleIndex++;
                }
            }
        }

        // bindless byte address buffer
        if (rayTracingResources.m_useByteAddressBuffer)
        {
            D3D12_DESCRIPTOR_RANGE1 bindlessByteAddressDescRange;
            bindlessByteAddressDescRange.BaseShaderRegister = 0;
            bindlessByteAddressDescRange.NumDescriptors = -1;
            bindlessByteAddressDescRange.RegisterSpace = BINDLESS_BYTE_ADDRESS_BUFFER_SPACE;
            bindlessByteAddressDescRange.OffsetInDescriptorsFromTableStart = 0;
            bindlessByteAddressDescRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            bindlessByteAddressDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(D3D12_DESCRIPTOR_RANGE_TYPE_SRV);

            // bindless index buffer and vertex
            rootParams[rootTabbleIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[rootTabbleIndex].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[rootTabbleIndex].DescriptorTable.pDescriptorRanges = &bindlessByteAddressDescRange;
            pDxRayTracingPipelineState->m_byteBufferBindlessRootTableIndex = rootTabbleIndex;
            rootTabbleIndex++;
        }

        // bindless texture 
        if (rayTracingResources.m_useBindlessTex2D)
        {
            D3D12_DESCRIPTOR_RANGE1 bindlessTexDescRange;
            bindlessTexDescRange.BaseShaderRegister = 0;
            bindlessTexDescRange.NumDescriptors = -1;
            bindlessTexDescRange.RegisterSpace = BINDLESS_TEXTURE_SPACE;
            bindlessTexDescRange.OffsetInDescriptorsFromTableStart = 0;
            bindlessTexDescRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            bindlessTexDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(D3D12_DESCRIPTOR_RANGE_TYPE_SRV);

            rootParams[rootTabbleIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[rootTabbleIndex].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[rootTabbleIndex].DescriptorTable.pDescriptorRanges = &bindlessTexDescRange;

            pDxRayTracingPipelineState->m_tex2DBindlessRootTableIndex = rootTabbleIndex;
            rootTabbleIndex++;
        }



        D3D12_ROOT_SIGNATURE_DESC1 rootSigDesc = {};
        rootSigDesc.NumParameters = rootParams.size();
        rootSigDesc.pParameters = rootParams.data();
        rootSigDesc.NumStaticSamplers = 6;
        rootSigDesc.pStaticSamplers = gStaticSamplerDescs;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        m_pRootSig = CreateRootSignature(pDevice, rootSigDesc);
        m_pInterface = m_pRootSig.GetInterfacePtr();
        m_subobject.pDesc = &m_pInterface;
        m_subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    }

    ID3D12RootSignaturePtr m_pRootSig;
    ID3D12RootSignature* m_pInterface = nullptr;
    D3D12_STATE_SUBOBJECT m_subobject = {};
};

struct SDxilLibrary
{
    SDxilLibrary(std::shared_ptr<SCompiledShaderCode> pBlob, const WCHAR* entryPoint[], uint32_t entryPointCount)
    {
        m_stateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        m_stateSubobject.pDesc = &m_dxilLibDesc;

        m_dxilLibDesc = {};
        m_exportDesc.resize(entryPointCount);
        m_exportName.resize(entryPointCount);
        if (pBlob)
        {
            m_dxilLibDesc.DXILLibrary.pShaderBytecode = pBlob->GetBufferPointer();
            m_dxilLibDesc.DXILLibrary.BytecodeLength = pBlob->GetBufferSize();
            m_dxilLibDesc.NumExports = entryPointCount;
            m_dxilLibDesc.pExports = m_exportDesc.data();

            for (uint32_t i = 0; i < entryPointCount; i++)
            {
                m_exportName[i] = entryPoint[i];
                m_exportDesc[i].Name = m_exportName[i].c_str();
                m_exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
                m_exportDesc[i].ExportToRename = nullptr;
            }
        }
    };

    SDxilLibrary() : SDxilLibrary(nullptr, nullptr, 0) {}

    D3D12_DXIL_LIBRARY_DESC m_dxilLibDesc = {};
    D3D12_STATE_SUBOBJECT m_stateSubobject{};
    std::vector<D3D12_EXPORT_DESC> m_exportDesc;
    std::vector<std::wstring> m_exportName;
};

struct SPipelineConfig
{
    SPipelineConfig(uint32_t maxTraceRecursionDepth)
    {
        m_config.MaxTraceRecursionDepth = maxTraceRecursionDepth;

        m_subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        m_subobject.pDesc = &m_config;
    }

    D3D12_RAYTRACING_PIPELINE_CONFIG m_config = {};
    D3D12_STATE_SUBOBJECT m_subobject = {};
};

void CRestirRayTracer::CreateRTPipelineStateAndShaderTable()
{
    
   SShaderResources rayTracingResources = rtPsoDesc.rayTracingResources;
   const std::wstring shaderPath = rtPsoDesc.filename;
   std::vector<SShader>rtShaders = rtPsoDesc.rtShaders;
   
   std::vector<LPCWSTR>pEntryPoint;
   std::vector<std::wstring>pHitGroupExports;
   uint32_t hitProgramNum = 0;
   
   for (auto& rtShader : rtShaders)
   {
       pEntryPoint.emplace_back(rtShader.m_entryPoint.c_str());
       switch (rtShader.m_eShaderType)
       {
       case ERayShaderType::RAY_AHS:
       case ERayShaderType::RAY_CHS:
           hitProgramNum++;
           pHitGroupExports.emplace_back(std::wstring(rtShader.m_entryPoint + std::wstring(L"Export")));
           break;
       };
   }
   
   uint32_t subObjectsNum = 1 + hitProgramNum + 2 + 1 + 1; // dxil subobj + hit program number + shader config * 2 + pipeline config + global root signature * 1
   
   std::vector<D3D12_STATE_SUBOBJECT> stateSubObjects;
   std::vector<D3D12_HIT_GROUP_DESC>hitgroupDesc;
   hitgroupDesc.resize(hitProgramNum);
   stateSubObjects.resize(subObjectsNum);
   
   uint32_t hitProgramIndex = 0;
   uint32_t subObjectsIndex = 0;
   
   std::shared_ptr<SCompiledShaderCode> rayTracingShaderCode = GetGlobalResource().m_ShaderCompilerDxc.Compile(shaderPath, L"", L"lib_6_3", nullptr, 0);
    SDxilLibrary dxilLib(rayTracingShaderCode, pEntryPoint.data(), pEntryPoint.size());
    stateSubObjects[subObjectsIndex] = dxilLib.m_stateSubobject;
    subObjectsIndex++;
    
    // create root signatures and export asscociation
    std::vector<const WCHAR*> emptyRootSigEntryPoints;
    for (uint32_t index = 0; index < rtShaders.size(); index++)
    {
        const SShader& rtShader = rtShaders[index];
        switch (rtShader.m_eShaderType)
        {
        case ERayShaderType::RAY_AHS:
            hitgroupDesc[hitProgramIndex].HitGroupExport = pHitGroupExports[hitProgramIndex].c_str();
            hitgroupDesc[hitProgramIndex].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
            hitgroupDesc[hitProgramIndex].AnyHitShaderImport = rtShader.m_entryPoint.data();
            hitgroupDesc[hitProgramIndex].ClosestHitShaderImport = nullptr;
            break;
        case ERayShaderType::RAY_CHS:
            hitgroupDesc[hitProgramIndex].HitGroupExport = pHitGroupExports[hitProgramIndex].c_str();
            hitgroupDesc[hitProgramIndex].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
            hitgroupDesc[hitProgramIndex].AnyHitShaderImport = nullptr;
            hitgroupDesc[hitProgramIndex].ClosestHitShaderImport = rtShader.m_entryPoint.data();
            hitProgramIndex++;
            break;
        }
    }
    
    for (uint32_t indexHit = 0; indexHit < hitProgramNum; indexHit++)
    {
        stateSubObjects[subObjectsIndex++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP ,&hitgroupDesc[indexHit] };
    }
    
    // shader config subobject and export associations
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
    shaderConfig.MaxPayloadSizeInBytes = MaxPayloadSizeInBytes;
    D3D12_STATE_SUBOBJECT configSubobject = {};
    configSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    configSubobject.pDesc = &shaderConfig;
    stateSubObjects[subObjectsIndex] = configSubobject;
    
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION sgExportAssociation = {};
    sgExportAssociation.NumExports = pEntryPoint.size();
    sgExportAssociation.pExports = pEntryPoint.data();
    sgExportAssociation.pSubobjectToAssociate = &stateSubObjects[subObjectsIndex++];
    stateSubObjects[subObjectsIndex++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION ,&sgExportAssociation };
    
    // pipeline config
    {
        SPipelineConfig pipelineConfig(8);
        stateSubObjects[subObjectsIndex] = pipelineConfig.m_subobject;
        subObjectsIndex++;
    }
    
    // global root signature
    SGlobalRootSignature globalRootSignature;
    {
        globalRootSignature.Init(pRaytracingDevice, rayTracingResources, &rayTracingPipelineState);
        stateSubObjects[subObjectsIndex] = globalRootSignature.m_subobject;
        subObjectsIndex++;
    }
    //  
    D3D12_STATE_OBJECT_DESC desc;
    desc.NumSubobjects = stateSubObjects.size();
    desc.pSubobjects = stateSubObjects.data();
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    
    rayTracingPipelineState.m_pGlobalRootSig = globalRootSignature.m_pRootSig;
    
    ThrowIfFailed(pRaytracingDevice->CreateStateObject(&desc, IID_PPV_ARGS(&rayTracingPipelineState.m_pRtPipelineState)));
    
    // create shader table
    uint32_t shaderTableSize = ShaderTableEntrySize * rtShaders.size() + sizeof(D3D12_GPU_DESCRIPTOR_HANDLE) + 8/*align*/;
    
    std::vector<char>shaderTableData;
    shaderTableData.resize(shaderTableSize);
    
    ID3D12StateObjectPropertiesPtr pRtsoProps;
    rayTracingPipelineState.m_pRtPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));
    
    std::vector<void*>rayGenShaderIdentifiers;
    std::vector<void*>rayMissShaderIdentifiers;
    std::vector<void*>rayHitShaderIdentifiers;
    
    uint32_t shaderTableHGindex = 0;
    for (uint32_t index = 0; index < rtShaders.size(); index++)
    {
        const SShader& rtShader = rtShaders[index];
        switch (rtShader.m_eShaderType)
        {
        case ERayShaderType::RAY_AHS:
        case ERayShaderType::RAY_CHS:
            rayHitShaderIdentifiers.push_back(pRtsoProps->GetShaderIdentifier(pHitGroupExports[shaderTableHGindex].c_str()));
            shaderTableHGindex++;
            break;
        case ERayShaderType::RAY_RGS:
            rayGenShaderIdentifiers.push_back(pRtsoProps->GetShaderIdentifier(rtShader.m_entryPoint.c_str()));
            break;
        case ERayShaderType::RAY_MIH:
            rayMissShaderIdentifiers.push_back(pRtsoProps->GetShaderIdentifier(rtShader.m_entryPoint.c_str()));
            break;
        };
        rayTracingPipelineState.m_nShaderNum[uint32_t(rtShader.m_eShaderType)]++;
    }
    uint32_t offsetShaderTableIndex = 0;
    for (uint32_t index = 0; index < rayGenShaderIdentifiers.size(); index++)
    {
        memcpy(shaderTableData.data() + (offsetShaderTableIndex + index) * ShaderTableEntrySize, rayGenShaderIdentifiers[index], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    }
    offsetShaderTableIndex += rayGenShaderIdentifiers.size();
    
    for (uint32_t index = 0; index < rayMissShaderIdentifiers.size(); index++)
    {
        memcpy(shaderTableData.data() + (offsetShaderTableIndex + index) * ShaderTableEntrySize, rayMissShaderIdentifiers[index], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    }
    offsetShaderTableIndex += rayMissShaderIdentifiers.size();
    
    for (uint32_t index = 0; index < rayHitShaderIdentifiers.size(); index++)
    {
        memcpy(shaderTableData.data() + (offsetShaderTableIndex + index) * ShaderTableEntrySize, rayHitShaderIdentifiers[index], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    }
    offsetShaderTableIndex += rayHitShaderIdentifiers.size();
    
    ShaderTableBuffer.Create(L"ShaderTableBuffer", 1, shaderTableSize, shaderTableData.data());
    //rayTracingPipelineState.m_pShaderTable = ShaderTableBuffer.GetResource();
}

D3D12_DISPATCH_RAYS_DESC CRestirRayTracer::CreateRayTracingDesc(uint32_t width, uint32_t height)
{
    D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
    raytraceDesc.Width = width;
    raytraceDesc.Height = height;
    raytraceDesc.Depth = 1;

    raytraceDesc.RayGenerationShaderRecord.StartAddress = ShaderTableBuffer->GetGPUVirtualAddress() + 0 * ShaderTableEntrySize;
    raytraceDesc.RayGenerationShaderRecord.SizeInBytes = ShaderTableEntrySize;

    uint32_t offset = 1 * ShaderTableEntrySize;;

    uint32_t rMissShaderNum = rayTracingPipelineState.m_nShaderNum[uint32_t(ERayShaderType::RAY_MIH)];
    if (rMissShaderNum > 0)
    {
        raytraceDesc.MissShaderTable.StartAddress = ShaderTableBuffer->GetGPUVirtualAddress() + offset;
        raytraceDesc.MissShaderTable.StrideInBytes = ShaderTableEntrySize;
        raytraceDesc.MissShaderTable.SizeInBytes = ShaderTableEntrySize * rMissShaderNum;

        offset += rMissShaderNum * ShaderTableEntrySize;
    }

    uint32_t rHitShaderNum = rayTracingPipelineState.m_nShaderNum[uint32_t(ERayShaderType::RAY_AHS)] + rayTracingPipelineState.m_nShaderNum[uint32_t(ERayShaderType::RAY_CHS)];
    if (rHitShaderNum > 0)
    {
        raytraceDesc.HitGroupTable.StartAddress = ShaderTableBuffer->GetGPUVirtualAddress() + offset;
        raytraceDesc.HitGroupTable.StrideInBytes = ShaderTableEntrySize;
        raytraceDesc.HitGroupTable.SizeInBytes = ShaderTableEntrySize * rHitShaderNum;
    }
    return raytraceDesc;
}

