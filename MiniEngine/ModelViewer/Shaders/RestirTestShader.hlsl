#define RAY_TRACING_MASK_OPAQUE				0x01
#define RT_MATERIAL_SHADER_INDEX            0
#define RT_SHADOW_SHADER_INDEX              1


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

RaytracingAccelerationStructure rtScene : register(t0);
Texture2D<float4> gbuffer_a: register(t1);
Texture2D<float4> gbuffer_b: register(t2);

RWTexture2D<float4> debug_rt: register(u0);

cbuffer CBRayTracingTestInformation : register(b0)
{
	float4 light_direction;
	float4 g_restir_texturesize;
	float4 g_full_screen_texsize;
}

[shader("raygeneration")]
void InitialSamplingRayGen()
{
	uint2 reservoir_coord = DispatchRaysIndex().xy;

    float3 world_normal = (float3)0;
    float3 world_position = (float3)0;

    bool is_valid_sample = true;
    if(reservoir_coord.x < g_restir_texturesize.x && reservoir_coord.y < g_restir_texturesize.y)
    {
        float2 reservior_uv = float2(reservoir_coord.xy) / float2(g_restir_texturesize.xy);
        uint2 gbuffer_pos = (reservior_uv) * g_full_screen_texsize.xy;

        if((gbuffer_pos.x < g_full_screen_texsize.x) && (gbuffer_pos.y < g_full_screen_texsize.y))
        {
            world_position = gbuffer_b[gbuffer_pos.xy].xyz;
		    world_normal = gbuffer_a[gbuffer_pos.xy].xyz;
            world_normal = normalize(world_normal);

            if(all(world_position == float3(0,0,0)))
            {
                is_valid_sample = false;
            }
        }
        else
        {
            is_valid_sample = false;
        }
	}

    if(is_valid_sample == false)
    {
        //todo: clear
        debug_rt[reservoir_coord] = float4(0,0,0,0);
        return;
    }

    RayDesc ray;
    ray.Origin = world_position;
    ray.Direction = light_direction.xyz;
    ray.TMin = 0.05;
    ray.TMax = 1e10;

	SInitialSamplingRayPayLoad payLoad = (SInitialSamplingRayPayLoad)0;
    TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, ~0/*RAY_TRACING_MASK_OPAQUE*/, 0, 1,0, ray, payLoad);

    float4 outputresult = (float4)0;
    if(payLoad.hit_t > 0.0)
    {
        outputresult = float4(0.0,0.0,abs(payLoad.hit_t),0.0);
    }
    else if(payLoad.hit_t == -1.0)
    {
        outputresult = float4(1.0,0.0,0.0,0.0);
    }
    else
    {
        outputresult = float4(0.0, abs(payLoad.hit_t), 0.0,0.0);
    }

    debug_rt[reservoir_coord] = outputresult;
}

[shader("closesthit")]
void InitialSamplingRayHit(inout SInitialSamplingRayPayLoad payload, in SRayTracingIntersectionAttributes attributes)
{
	payload.hit_t = RayTCurrent();
    if(payload.hit_t == 0.0)
    {
        payload.hit_t = 10.0f;
    }
}

[shader("miss")]
void RayMiassMain(inout SInitialSamplingRayPayLoad payload)
{
    payload.hit_t = -1.0;
}