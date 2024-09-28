
#define GROUP_SIZE 16



#include "ReSTIRCommon.hlsl"

// ping-pong pass, texture resources are from the temporal resampling
Texture2D<float4> reservoir_ray_direction : register(t0);
Texture2D<float4> reservoir_ray_radiance : register(t1); //xyz only
Texture2D<float>  reservoir_hit_distance : register(t2);
Texture2D<float4> reservoir_hit_normal : register(t3);//xyz only
Texture2D<float4> reservoir_weights : register(t4);//xyz only

Texture2D<float4> downsampled_world_pos : register(t5); //xyz only
Texture2D<float4> downsampled_world_normal : register(t6); //xyz only
Texture2D<float> stbn_scalar_tex: register(t7);

RWTexture2D<float4> rw_reservoir_ray_direction : register(u0);
RWTexture2D<float4> rw_reservoir_ray_radiance : register(u1);//xyz only
RWTexture2D<float>  rw_reservoir_hit_distance : register(u2);
RWTexture2D<float4> rw_reservoir_hit_normal : register(u3);//xyz only
RWTexture2D<float4> rw_reservoir_weights : register(u4);//xyz only

cbuffer CBRestirSceneInfo : register(b0)
{
    RESTIR_SCENE_INFO_COMMON
};

// root constant
cbuffer CBRestirSceneInfo : register(b1)
{
    uint g_spatial_resampling_pass_index;
};

float CalculateJacobian(float3 world_position, float3 neighbor_world_position, SReservoir reservoir_sample)
{
    float3 neighbor_hit_position = neighbor_world_position + reservoir_sample.m_sample.hit_distance * reservoir_sample.m_sample.ray_direction;

    float3 neighbor_receiver_to_sample = neighbor_world_position - neighbor_hit_position;
    float original_distance = length(neighbor_receiver_to_sample);
    float original_cos_angle = saturate(dot(reservoir_sample.m_sample.hit_normal,neighbor_receiver_to_sample / original_distance));

    float3 new_receiver_to_sample = world_position - neighbor_hit_position;
    float new_distance = length(new_receiver_to_sample);
    float new_cos_angle = saturate(dot(reservoir_sample.m_sample.hit_normal,new_receiver_to_sample / new_distance));

    float jacobian = (new_cos_angle * original_distance * original_distance) / (original_cos_angle * new_distance * new_distance);

	if (isinf(jacobian) || isnan(jacobian)) { jacobian = 0; }
	if (abs(dot(reservoir_sample.m_sample.hit_normal, 1.0f)) < .01f) { jacobian = 1.0f; }
	if (jacobian > 10.0f || jacobian < 1 / 10.0f) { jacobian = 0; }
	return jacobian;
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void SpatialResamplingCS(uint2 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 reservoir_coord = dispatch_thread_id.xy;
    float3 world_position = downsampled_world_pos[reservoir_coord].xyz;
    if(all(reservoir_coord < g_restir_texturesize) && any(world_position != float3(0,0,0)))
    {
        SReservoir current_reservoir = LoadReservoir(reservoir_coord, reservoir_ray_direction, reservoir_ray_radiance, reservoir_hit_distance, reservoir_hit_normal, reservoir_weights);

        float3 world_normal = downsampled_world_normal[reservoir_coord].xyz;
        float noise = GetBlueNoiseScalar(stbn_scalar_tex, reservoir_coord, g_current_frame_index);

        const int num_spatial_samples = 4;
        const float spatial_resampling_kernel_radius = 0.05;

        float spatial_sampling_scale = spatial_resampling_kernel_radius * g_restir_texturesize.x;
        for(uint sample_index = 0; sample_index < num_spatial_samples; sample_index++)
        {
            const float angle = (sample_index + 0.3 * g_spatial_resampling_pass_index) * 2.3999632f;
            const float radius = pow(float(sample_index + 1), 0.66666) * spatial_sampling_scale / float(num_spatial_samples);
            const float2 reservoir_offset = float2(cos(angle),sin(angle)) * radius;
            const int2 reservoir_offset_int = floor(reservoir_offset + float2(0.5, 0.5));

            int2 neighbor_reservoir_coord = clamp(reservoir_offset_int + reservoir_coord, int2(0,0), int2(g_restir_texturesize.xy) - int2(1,1));

            float3 neighbor_world_position = downsampled_world_pos[neighbor_reservoir_coord].xyz;
            if(any(neighbor_world_position != float3(0,0,0)))
            {
                SReservoir neighbor_reservoir = LoadReservoir(neighbor_reservoir_coord, reservoir_ray_direction, reservoir_ray_radiance, reservoir_hit_distance, reservoir_hit_normal, reservoir_weights);

                float3 neighbor_world_normal = downsampled_world_normal[neighbor_reservoir_coord].xyz;

                bool is_history_nearby = (distance(neighbor_world_position, world_position) < 10.0f) && (abs(dot(neighbor_world_normal,world_normal) < 0.25));
                if(is_history_nearby)
                {
                    float jacobian = CalculateJacobian(world_position, neighbor_world_position, neighbor_reservoir);
                    float swap_noise = GetBlueNoiseScalar(stbn_scalar_tex, reservoir_coord, g_current_frame_index * num_spatial_samples + sample_index);
                    MergeReservoirSample(current_reservoir, neighbor_reservoir, neighbor_reservoir.m_sample.pdf * jacobian, swap_noise);
                }
            }
        }

        current_reservoir.comb_weight = current_reservoir.weight_sum / max(current_reservoir.M * current_reservoir.m_sample.pdf, 0.0001f);
        StoreReservoir(reservoir_coord, current_reservoir, rw_reservoir_ray_direction, rw_reservoir_ray_radiance, rw_reservoir_hit_distance, rw_reservoir_hit_normal, rw_reservoir_weights);
    }
    else
    {
        rw_reservoir_ray_direction[reservoir_coord] = float4(0,0,0,0);
    }
}