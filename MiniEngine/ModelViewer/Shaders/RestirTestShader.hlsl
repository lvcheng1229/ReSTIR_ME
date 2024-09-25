#define RAY_TRACING_MASK_OPAQUE				0x01
#define RT_MATERIAL_SHADER_INDEX            0
#define RT_SHADOW_SHADER_INDEX              1

cbuffer CBRayTracingTestInformation : register(b0)
{
	float4 light_direction;
	float4 g_restir_texturesize;
	float4 g_full_screen_texsize;
}

struct SInitialSamplingRayPayLoad
{
    float3 world_normal;
    float3 world_position;
    float3 albedo;
    float hit_t;
};

struct SRayTracingIntersectionAttributes
{
    float x;
    float y;
};

Texture2D<float4> gbuffer_a: register(t0);
Texture2D<float4> gbuffer_b: register(t1);
RaytracingAccelerationStructure rtScene : register(t2);

RWTexture2D<float4> debug_rt: register(u0);

[shader("raygeneration")]
void InitialSamplingRayGen()
{
	uint2 reservoir_coord = DispatchRaysIndex().xy;
    if(reservoir_coord.x < g_restir_texturesize.x && reservoir_coord.y < g_restir_texturesize.y)
    {
        float2 reservpor_uv = float2(reservoir_coord.xy + float2(0.5,0.5)) / float2(g_restir_texturesize.xy);
        uint2 gbuffer_pos = (reservpor_uv) * g_full_screen_texsize.xy;

        float3 world_position = gbuffer_b.Load(int3(gbuffer_pos.xy,0)).xyz;
		float3 world_normal = gbuffer_a.Load(int3(gbuffer_pos.xy,0)).xyz;

		RayDesc ray;
        ray.Origin = world_position + world_normal * 0.005;
        ray.Direction = light_direction.xyz;
        ray.TMin = 0.05;
        ray.TMax = 1e10;

		SInitialSamplingRayPayLoad payLoad = (SInitialSamplingRayPayLoad)0;
        TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_MATERIAL_SHADER_INDEX, 1,0, ray, payLoad);
        if(payLoad.hit_t > 0.0)
        {
            debug_rt[reservoir_coord] = float4(0.0,0.0,1.0,0.0);
        }
        else
        {
            debug_rt[reservoir_coord] = float4(0.0,0.0,0.0,0.0);
        }
	}

	
}

[shader("closesthit")]
void InitialSamplingRayHit(inout SInitialSamplingRayPayLoad payload, in SRayTracingIntersectionAttributes attributes)
{
	payload.hit_t = RayTCurrent();;
}