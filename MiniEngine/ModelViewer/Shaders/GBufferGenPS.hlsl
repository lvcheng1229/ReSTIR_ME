struct VSOutput
{
	float4 position : SV_Position;
	float3 worldPos : WorldPos;
	float2 uv : TexCoord0;
	float3 viewDir : TexCoord1;
	float3 shadowCoord : TexCoord2;
	float3 normal : Normal;
	float3 tangent : Tangent;
	float3 bitangent : Bitangent;
};

struct SOutGBuffer
{
	float4 gbufferA : SV_Target0;
	float4 gbufferB : SV_Target1;
};

Texture2D<float3> texNormal			: register(t3);
SamplerState defaultTextureSampler       	: register(s0);

SOutGBuffer main(VSOutput vsOutput)
{
    SOutGBuffer gbufferData = (SOutGBuffer)0;
    //float3 texNormalValue = texNormal.Sample(defaultTextureSampler, vsOutput.uv);
    float3 texNormalValue = float3(0,0,1);
    float3x3 tbn = float3x3(normalize(vsOutput.tangent), normalize(vsOutput.bitangent), normalize(vsOutput.normal));
    float3 worldNormal = normalize(mul(texNormalValue, tbn));
    float roughness = 1.0;
	
	gbufferData.gbufferA = float4(worldNormal, 1.0);
	gbufferData.gbufferB = float4(vsOutput.worldPos,roughness);
    return gbufferData;
}

