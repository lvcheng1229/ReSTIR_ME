#ifndef _RESTIR_COMMON_
#define _RESTIR_COMMON_

#define RESTIR_SCENE_INFO_COMMON\
    float4x4 PreViewProjMatrix;\
    float2 g_restir_texturesize;\
    uint g_current_frame_index;\
    float3 g_sun_direction;\
    float3 g_sun_intensity;\


struct SSample
{
    float3 ray_direction;
    float3 radiance;
    float hit_distance;
    float3 hit_normal;
    float pdf;
};

struct SReservoir
{
    SSample m_sample;
    float weight_sum;
    float M; //ReSTIR:Paper(3.1): First, M candidate samples y = y1,..., yM are sampled from a source distribution p(y).
    float comb_weight;
};

void InitializeReservoir(inout SReservoir reservoir)
{
    reservoir.weight_sum = 0;
    reservoir.M = 0;
    reservoir.comb_weight = 0;
}

bool UpdateReservoir(inout SReservoir reservoir, SSample new_sample, float weight_new, float noise)
{
    bool is_sample_changed = false;
    weight_sum += weight_new;
    num_sample += 1.0f;

    if (noise < weight_new / weight_sum)
	{
		m_sample = new_sample;
		is_sample_changed = true;
	}
    return is_sample_changed;
}

void MergeReservoirSample(inout SReservoir reservoir_sample, SReservoir new_reservoir_sample, float other_sample_pdf, float noise)
{
    float M0 = reservoir_sample.M;
    bool is_changed = UpdateReservoir(reservoir_sample, new_reservoir_sample, other_sample_pdf * new_reservoir_sample.M * new_reservoir_sample.comb_weight, noise);
    reservoir_sample.M = M0 + new_reservoir_sample.M;
    return is_changed;
}

float2 GetBlueNoiseVector2(uint2 screen_position,uint frame_index)
{
    uint3 texture_coord_3d = uint3(screen_position.xy, frame_index) & uint3(128,128,64);
    return stbn_vec2_tex.Load(texture_coord_3d.xyz,0).xy;
}

float GetBlueNoiseScalar(uint2 screen_position,uint frame_index)
{
    uint3 texture_coord_3d = uint3(screen_position.xy, frame_index) & uint3(128,128,64);
    return stbn_scalar_tex.Load(texture_coord_3d.xyz,0).x;
}


SReservoir LoadReservoir(
    uint2 reservoir_coord, 
    Texture2D<float4> reservoir_ray_direction,
    Texture2D<float3> reservoir_ray_radiance,
    Texture2D<float> reservoir_hit_distance,
    Texture2D<float3> reservoir_hit_normal,
    Texture2D<float3> reservoir_weights
    )
{
    SSample reservoir_sample;
    reservoir_sample.ray_direction = reservoir_ray_direction[reservoir_coord].xyz;
    reservoir_sample.radiance = reservoir_ray_radiance[reservoir_coord];
    reservoir_sample.hit_distance = reservoir_hit_distance[reservoir_coord];
    reservoir_sample.hit_normal = reservoir_hit_normal[reservoir_coord];
    reservoir_sample.pdf = reservoir_ray_direction[reservoir_coord].w;

    SReservoir reservoir;
    reservoir.m_sample = reservoir_sample;
    reservoir.weight_sum = reservoir_weights[reservoir_coord].x;
    reservoir.M = reservoir_weights[reservoir_coord].y;
    reservoir.comb_weight = reservoir_weights[reservoir_coord].z;
}

SReservoir StoreReservoir(
    uint2 reservoir_coord, 
    SReservoir reservoir, 
    RWTexture2D<float4> reservoir_ray_direction,
    RWTexture2D<float3> reservoir_ray_radiance,
    RWTexture2D<float> reservoir_hit_distance,
    RWTexture2D<float3> reservoir_hit_normal,
    RWTexture2D<float3> reservoir_weights)
{
    reservoir_ray_direction[reservoir_coord] = float4(reservoir.m_sample.ray_direction, reservoir.m_sample.pdf);
    reservoir_ray_radiance[reservoir_coord] = float3(reservoir_sample.radiance);
    reservoir_hit_distance[reservoir_coord] = reservoir_sample.hit_distance;
    reservoir_hit_normal[reservoir_coord] = reservoir_sample.hit_normal;

    reservoir_weights[reservoir_coord] = float3(reservoir.weight_sum, reservoir.M, reservoir.comb_weight);
}
#endif