#ifndef _RESTIR_COMMON_
#define _RESTIR_COMMON_
#define PI 3.14159265358979

#define RESTIR_SCENE_INFO_COMMON\
    float4x4 PreViewProjMatrix;\
    float2 g_restir_texturesize;\
    float2 g_full_screen_texsize;\
    float3 g_sun_direction;\
    uint g_current_frame_index;\
    float3 g_sun_intensity;\
    float restirpadding0;\
    float3 g_camera_worldpos;\
    float restirpadding1;\


#define DISOCCLUSION_VARIANCE 1.0

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
    reservoir.weight_sum += weight_new;
    reservoir.comb_weight += 1.0f;

    if (noise < weight_new / reservoir.weight_sum)
	{
		reservoir.m_sample = new_sample;
		is_sample_changed = true;
	}
    return is_sample_changed;
}

bool MergeReservoirSample(inout SReservoir reservoir_sample, SReservoir new_reservoir_sample, float other_sample_pdf, float noise)
{
    float M0 = reservoir_sample.M;
    bool is_changed = UpdateReservoir(reservoir_sample, new_reservoir_sample.m_sample, other_sample_pdf * new_reservoir_sample.M * new_reservoir_sample.comb_weight, noise);
    reservoir_sample.M = M0 + new_reservoir_sample.M;
    return is_changed;
}

float2 GetBlueNoiseVector2(Texture2D<float2> stbn_vec2_tex, uint2 screen_position,uint frame_index)
{
    uint3 texture_coord_3d = uint3(screen_position.xy, frame_index) % int3(128,128,64);
    return stbn_vec2_tex.Load(uint3(texture_coord_3d.x, (texture_coord_3d.z * 128) + texture_coord_3d.y,0)).xy;
}

float GetBlueNoiseScalar(Texture2D<float> stbn_scalar_tex,uint2 screen_position,uint frame_index)
{
    uint3 texture_coord_3d = uint3(screen_position.xy, frame_index) % int3(128,128,64);
    return stbn_scalar_tex.Load(uint3(texture_coord_3d.x, (texture_coord_3d.z * 128) + texture_coord_3d.y,0)).x;
}

//From UnrealEngine
float acosFast(float inX) 
{
    float x = abs(inX);
    float res = -0.156583f * x + (0.5 * PI);
    res *= sqrt(1.0f - x);
    return (inX >= 0) ? res : PI - res;
}

float Luma(float3 color)
{
    return dot(color, float3(0.2,0.7,0.1));
}

float3 ToneMappingLighting(float3 light)
{
    return light / (1 + Luma(light));
}

float3 InverseToneMappingLight(float3 light)
{
    return light / (1.0 - Luma(light));
}

uint3 Rand3DPCG32(int3 p)
{
	uint3 v = uint3(p);
	v = v * 1664525u + 1013904223u;

	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;

	v ^= v >> 16u;

	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;

	return v;
}

float2 Hammersley16( uint Index, uint NumSamples, uint2 Random )
{
	float E1 = frac( (float)Index / NumSamples + float( Random.x ) * (1.0 / 65536.0) );
	float E2 = float( ( reversebits(Index) >> 16 ) ^ Random.y ) * (1.0 / 65536.0);
	return float2( E1, E2 );
}

SReservoir LoadReservoir(
    uint2 reservoir_coord, 
    Texture2D<float4> reservoir_ray_direction,
    Texture2D<float4> reservoir_ray_radiance,
    Texture2D<float> reservoir_hit_distance,
    Texture2D<float4> reservoir_hit_normal,
    Texture2D<float4> reservoir_weights
    )
{
    SSample reservoir_sample;
    reservoir_sample.ray_direction = reservoir_ray_direction[reservoir_coord].xyz;
    reservoir_sample.radiance = reservoir_ray_radiance[reservoir_coord].xyz;
    reservoir_sample.hit_distance = reservoir_hit_distance[reservoir_coord];
    reservoir_sample.hit_normal = reservoir_hit_normal[reservoir_coord].xyz;
    reservoir_sample.pdf = reservoir_ray_direction[reservoir_coord].w;

    SReservoir reservoir;
    reservoir.m_sample = reservoir_sample;
    reservoir.weight_sum = reservoir_weights[reservoir_coord].x;
    reservoir.M = reservoir_weights[reservoir_coord].y;
    reservoir.comb_weight = reservoir_weights[reservoir_coord].z;
    return reservoir;
}

void StoreReservoir(
    uint2 reservoir_coord, 
    SReservoir reservoir, 
    RWTexture2D<float4> reservoir_ray_direction,
    RWTexture2D<float4> reservoir_ray_radiance,
    RWTexture2D<float> reservoir_hit_distance,
    RWTexture2D<float4> reservoir_hit_normal,
    RWTexture2D<float4> reservoir_weights)
{
    SSample reservoir_sample = reservoir.m_sample;
    reservoir_ray_direction[reservoir_coord] = float4(reservoir.m_sample.ray_direction, reservoir.m_sample.pdf);
    reservoir_ray_radiance[reservoir_coord] = float4(reservoir_sample.radiance,1.0);
    reservoir_hit_distance[reservoir_coord] = reservoir_sample.hit_distance;
    reservoir_hit_normal[reservoir_coord] = float4(reservoir_sample.hit_normal,1.0);

    reservoir_weights[reservoir_coord] = float4(reservoir.weight_sum, reservoir.M, reservoir.comb_weight,1.0);
}
#endif