cbuffer GlobalConstants : register(b0)
{
    float4x4 ViewProjMatrix;
    float4x4 SunShadowMatrix;
    float3 ViewerPos;
    float3 SunDirection;
    float3 SunIntensity;
}

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord0 : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPos : WorldPos;
    float2 texCoord : TexCoord0;
    float3 viewDir : TexCoord1;
    float3 shadowCoord : TexCoord2;
    float3 normal : Normal;
    float3 tangent : Tangent;
    float3 bitangent : Bitangent;
};

VSOutput main(VSInput vsInput, uint vertexID : SV_VertexID)
{
    VSOutput vsOutput;

    float4 position = float4(vsInput.position, 1.0);
    float3 normal = vsInput.normal * 2 - 1;

    vsOutput.worldPos = vsInput.position;
    vsOutput.position = mul(ViewProjMatrix, float4(vsOutput.worldPos, 1.0));
    vsOutput.shadowCoord = mul(SunShadowMatrix, float4(vsOutput.worldPos, 1.0)).xyz;
    vsOutput.normal = vsInput.normal;
    vsOutput.texCoord = vsInput.texcoord0;
    vsOutput.tangent = vsInput.tangent;
    vsOutput.bitangent = vsInput.bitangent;

    return vsOutput;
}