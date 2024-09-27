#define GROUP_SIZE 16


#include "ReSTIRCommon.hlsl"

cbuffer CBRestirSceneInfo : register(b0)
{
    RESTIR_SCENE_INFO_COMMON
};

Texture2D<float4> reservoir_ray_direction : register(t0);
Texture2D<float4> reservoir_ray_radiance : register(t1);//xyz only
Texture2D<float>  reservoir_hit_distance : register(t2);
Texture2D<float4> reservoir_hit_normal : register(t3);//xyz only
Texture2D<float4> reservoir_weights : register(t4);//xyz only

Texture2D<float4> downsampled_world_pos : register(t5);//xyz only
Texture2D<float4> downsampled_world_normal : register(t6);//xyz only

Texture2D<float4> history_reservoir_ray_direction : register(t7);
Texture2D<float4> history_reservoir_ray_radiance : register(t8);//xyz only
Texture2D<float>  history_reservoir_hit_distance : register(t9);
Texture2D<float4> history_reservoir_hit_normal : register(t10);//xyz only
Texture2D<float4> history_reservoir_weights : register(t11);//xyz only

Texture2D<float4> history_downsampled_world_pos : register(t12);//xyz only
Texture2D<float4> history_downsampled_world_normal : register(t13);//xyz only
Texture3D<float> stbn_scalar_tex: register(t14);

RWTexture2D<float4> rw_reservoir_ray_direction : register(u0);
RWTexture2D<float4> rw_reservoir_ray_radiance : register(u1);//xyz only
RWTexture2D<float>  rw_reservoir_hit_distance : register(u2);
RWTexture2D<float4> rw_reservoir_hit_normal : register(u3);//xyz only
RWTexture2D<float4> rw_reservoir_weights : register(u4);//xyz only

//half resolution
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void TemporalResamplingCS(uint2 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 reservoir_coord = dispatch_thread_id.xy;
    float3 world_position = downsampled_world_pos[reservoir_coord].xyz;
    if(all(reservoir_coord < g_restir_texturesize) && any(world_position != float3(0,0,0)))
    {
        SReservoir current_reservoir = LoadReservoir(reservoir_coord, reservoir_ray_direction, reservoir_ray_radiance, reservoir_hit_distance, reservoir_hit_normal, reservoir_weights);

        float3 world_normal = downsampled_world_normal[reservoir_coord].xyz;

        float4 pre_view_pos = mul(PreViewProjMatrix,float4(world_position, 1.0));
        float2 pre_view_screen_pos = (float2(pre_view_pos.xy / pre_view_pos.w) * 0.5 + float2(0.5,0.5));
        pre_view_screen_pos.y = (1.0 - pre_view_screen_pos.y);
        uint2  pre_view_texture_pos = pre_view_screen_pos * g_restir_texturesize;

        const bool isHistoryOnScreen = all(pre_view_screen_pos < g_restir_texturesize);
        if(isHistoryOnScreen)
        {
            const uint num_temporal_samples = 9;
            
            float noise = GetBlueNoiseScalar(stbn_scalar_tex, reservoir_coord, g_current_frame_index);

            uint2 history_reservoir_sample = clamp(pre_view_texture_pos, g_restir_texturesize, g_restir_texturesize);

            float3 history_world_position = history_downsampled_world_pos[history_reservoir_sample].xyz;
            float3 history_world_normal = history_downsampled_world_normal[history_reservoir_sample].xyz;

            bool is_history_nearby = (distance(history_world_position, world_position) < 10.0f) && (abs(dot(history_world_normal,world_normal) < 0.25));

            if(any(history_world_position != float3(0,0,0)) && is_history_nearby)
            {
                SReservoir history_reservoir = LoadReservoir(reservoir_coord, history_reservoir_ray_direction, history_reservoir_ray_radiance, history_reservoir_hit_distance, history_reservoir_hit_normal, history_reservoir_weights);
                
                // limited to 20
                history_reservoir.M = min(history_reservoir.M, 20.0f);
                MergeReservoirSample(current_reservoir, history_reservoir, history_reservoir.m_sample.pdf, noise);
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