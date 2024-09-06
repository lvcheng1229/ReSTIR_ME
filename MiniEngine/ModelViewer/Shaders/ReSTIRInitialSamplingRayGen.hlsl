Texture3D<float2> stbn_vec2_tex3d: register(t2);

#include "ReSTIRCommon.hlsl"

struct SMeshGpuData
{   
    float4x4 worldTM;
    uint ibStride;
    uint ibIndex;
    uint vbIndex;
    uint vbStride;
    uint uvOffset
    uint albedoIndex; // fully rough surface
};

#define RAY_TRACING_MASK_OPAQUE				0x01
#define RT_MATERIAL_SHADER_INDEX            0
#define RT_SHADOW_SHADER_INDEX              1

Texture2D<float4> gbuffer_a: register(t0);
Texture2D<float4> gbuffer_b: register(t1);

StructuredBuffer<SMeshGpuData> rt_scene_mesh_gpu_data : register(t3);
RaytracingAccelerationStructure rtScene : register(t0);

SamplerState defaultTextureSampler       	: register(s0);

RWTexture2D<float4> rw_reservoir_ray_direction : register(u0);
RWTexture2D<float3> rw_reservoir_ray_radiance : register(u1);
RWTexture2D<float> rw_reservoir_hit_distance : register(u2);
RWTexture2D<float3> rw_reservoir_hit_normal : register(u3);
RWTexture2D<float3> rw_reservoir_weights : register(u4);

RWTexture2D<float3> rw_downsampled_world_pos : register(u5);
RWTexture2D<float3> rw_downsampled_world_normal : register(u6);

ByteAddressBuffer bindless_byte_addr_buffer[] : register(t0, space1);
Texture2D<float4> bindless_albedo_texs[] : register(t0, space2);

cbuffer CBRestirSceneInfo : register(b0)
{
    RESTIR_SCENE_INFO_COMMON
};

struct SRayTracingIntersectionAttributes
{
    float x;
    float y;
};

struct SInitialSamplingRayPayLoad
{
    float3 world_normal;
    float3 world_position;
    float3 albedo;
    float hit_t;
};

// From Unreal MonteCarlo.ush
// PDF = 1 / (2 * PI)
float4 UniformSampleHemisphere( float2 E )
{
	float Phi = 2 * PI * E.x;
	float CosTheta = E.y;
	float SinTheta = sqrt( 1 - CosTheta * CosTheta );

	float3 H;
	H.x = SinTheta * cos( Phi );
	H.y = SinTheta * sin( Phi );
	H.z = CosTheta;

	float PDF = 1.0 / (2 * PI);

	return float4( H, PDF );
}

// From Unreal MonteCarlo.ush
float3x3 GetTangentBasisFrisvad(float3 TangentZ)
{
	float3 TangentX;
	float3 TangentY;

	if (TangentZ.z < -0.9999999f)
	{
		TangentX = float3(0, -1, 0);
		TangentY = float3(-1, 0, 0);
	}
	else
	{
		float A = 1.0f / (1.0f + TangentZ.z);
		float B = -TangentZ.x * TangentZ.y * A;
		TangentX = float3(1.0f - TangentZ.x * TangentZ.x * A, B, -TangentZ.x);
		TangentY = float3(B, 1.0f - TangentZ.y * TangentZ.y * A, -TangentZ.y);
	}

	return float3x3( TangentX, TangentY, TangentZ );
}



float Luminance(float3 color)
{
    return dot(color,float3(0.3,0.59,0.11));
}

// half resolution

[shader("raygeneration")]
void InitialSamplingRayGen()
{
    uint2 reservoir_coord = DispatchRaysIndex().xy;
    if(reservoir_coord.x < g_restir_texturesize.x && reservoir_coord.y < g_restir_texturesize.y)
    {
        float3 world_position = gbuffer_b.Load(int3(reservoir_coord.xy,0)).xyz;
        float world_normal = gbuffer_a.Load(int3(reservoir_coord.xy,0)).xyz;

        // https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-2/
        float2 E = GetBlueNoiseVector2(reservoir_coord, g_current_frame_index):
        float4 hemi_sphere_sample = UniformSampleHemisphere(E);
        float3x3 tangent_basis = GetTangentBasisFrisvad(world_normal);

        float3 local_ray_direction = hemi_sphere_sample.xyz;
        float3 world_ray_direction = mul(local_ray_direction, tangent_basis):

        FRayDesc ray;
        ray.Origin = world_position + world_normal * 0.005;
        ray.Direction = world_ray_direction;
        ray.TMin = 0.05;
        ray.TMax = 1e10;

        SInitialSamplingRayPayLoad payLoad = (SInitialSamplingRayPayLoad)0;
        TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_MATERIAL_SHADER_INDEX, 1,0, ray, payLoad);

        float3 radiance = float3(0,0,0);
        // trace a visibility ray from the world pos along the direction
        if(payLoad.hit_t == 0.0)
        {
            // sky light
        }
        else
        {
            RayDesc shadow_ray;
            shadow_ray.Origin = payLoad.world_position;
            shadow_ray.TMin = 0.0f;
            shadow_ray.Direction = g_sun_direction;
            shadow_ray.TMax = payLoad.hit_t;
            shadow_ray.Origin += abs(payLoad.world_position) * 0.001f * payLoad.world_position.world_normal; // todo : betther bias calculation

            SInitialSamplingRayPayLoad shadow_payLoad = (SInitialSamplingRayPayLoad)0;
            TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_SHADOW_SHADER_INDEX, 1,0, shadow_ray, shadow_payLoad);

            if(payLoad.hit_t != 0.0)
            {
                float NoL = saturate(dot(g_sun_direction, payLoad.world_normal));
                radiance = NoL * g_sun_intensity * payLoad.albedo * (1.0 / PI);
            }
        }

        SSample initial_sample;
        initial_sample.ray_direction = ray.Direction;
        initial_sample.radiance = radiance;
        initial_sample.hit_distance = payLoad.hit_distance;
        initial_sample.hit_normal = payLoad.world_normal;
        initial_sample.pdf = hemi_sphere_sample.w;

        float target_pdf = Luminance(radiance);
        float w = target_pdf / initial_sample.pdf;

        SReservoir reservoir;
        InitializeReservoir(reservoir);
        UpdateReservoir(reservoir, initial_sample, ,0);
        reservoir.m_sample.pdf = target_pdf;
        reservoir.W = reservoir.weight_sum / (max(reservoir.M, reservoir.m_sample.pdf, 0.00001f));

        rw_reservoir_ray_direction[reservoir_coord] = float4(reservoir.m_sample.ray_direction, reservoir.m_sample.pdf);
        rw_reservoir_ray_radiance[reservoir_coord] = float3(reservoir.m_sample.radiance);
        rw_reservoir_hit_distance[reservoir_coord] = reservoir.m_sample.hit_distance;
        rw_reservoir_hit_normal[reservoir_coord] = reservoir.m_sample.hit_normal;
        rw_reservoir_weights[reservoir_coord] = float3(reservoir.weight_sum, reservoir.M, reservoir.comb_weight);

        rw_downsampled_world_pos[reservoir_coord] = world_position;
        rw_downsampled_world_normal[reservoir_coord] = world_normal;
    }
    else
    {
        rw_downsampled_world_pos[reservoir_coord] = float3(0,0,0);
        rw_downsampled_world_normal[reservoir_coord] = float3(0,0,0);
    }
}

[shader("closesthit")]
void InitialSamplingRayHit(inout SInitialSamplingRayPayLoad payload, in SRayTracingIntersectionAttributes attributes)
{
    if(HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE)
    {
        const float3 barycentrics = float3(1.0 - attributes.x - attributes.y, attributes.x, attributes.y);

        SMeshGpuData mesh_gpu_data = rt_scene_mesh_gpu_data[InstanceID()];
        const float4x4 world_trans = mesh_gpu_data.world_trans;
        const uint ibStride = mesh_gpu_data.ibStride;
        const uint ibIndex = mesh_gpu_data.ibIndex;
        const uint vbIndex = mesh_gpu_data.vbIndex;
        const uint vbStride = mesh_gpu_data.vbStride;
        const uint uvOffset = mesh_gpu_data.uvOffset;
        const uint albedoIndex = mesh_gpu_data.albedoIndex;

        uint primitiveIndex = PrimitiveIndex();
        uint baseIndex = primitiveIndex * 3;
        
        // vertex position
        float3 local_vertex0_position = asfloat(bindless_byte_addr_buffer[vbIndex].Load<float3>(indices.x * vbStride));
        float3 local_vertex1_position = asfloat(bindless_byte_addr_buffer[vbIndex].Load<float3>(indices.y * vbStride));
        float3 local_vertex2_position = asfloat(bindless_byte_addr_buffer[vbIndex].Load<float3>(indices.z * vbStride));

        float3 world_position0 =  mul(world_trans, float4(local_vertex0_position,1.0)).xyz;
        float3 world_position1 =  mul(world_trans, float4(local_vertex1_position,1.0)).xyz;
        float3 world_position2 =  mul(world_trans, float4(local_vertex2_position,1.0)).xyz;

        float3 world_position = world_position0 * barycentrics.x + world_position1 * barycentrics.y + world_position2 * barycentrics.z;
        
        // vertex uv
        float3 vertex0_uv = asfloat(bindless_byte_addr_buffer[vbIndex].Load<float2>(indices.x * vbStride + uvOffset));
        float3 vertex1_uv = asfloat(bindless_byte_addr_buffer[vbIndex].Load<float2>(indices.y * vbStride + uvOffset));
        float3 vertex2_uv = asfloat(bindless_byte_addr_buffer[vbIndex].Load<float2>(indices.z * vbStride + uvOffset));

        float3 vertex_uv = vertex0_uv * barycentrics.x + vertex1_uv * barycentrics.y + vertex2_uv * barycentrics.z;

        float3 PA = world_position1 - world_position0;
	    float3 PB = world_position2 - world_position0;
        float3 Unnormalized = cross(PB, PA);
	    float inv_world_area = rsqrt(dot(Unnormalized, Unnormalized));
        float3 face_normal = Unnormalized * inv_world_area;

        float3 albedo = texNormal.Sample(defaultTextureSampler, vertex_uv);

        payload.world_normal = face_normal;
        payload.world_position = world_position;
        payload.albedo = albedo;
        payload.hit_t = RayTCurrent();
    }
}

[shader("closesthit")]
void ShadowClosestHitMain(inout SInitialSamplingRayPayLoad payload, in SRayTracingIntersectionAttributes attributes)
{
    payload.hit_t = RayTCurrent();;
}


