#define GROUP_SIZE 16

#include "ReSTIRCommon.hlsl"

Texture2D<float4> reservoir_ray_direction : register(t0);
Texture2D<float4> reservoir_ray_radiance : register(t1);
Texture2D<float>  reservoir_hit_distance : register(t2);
Texture2D<float4> reservoir_hit_normal : register(t3);
Texture2D<float4> reservoir_weights : register(t4);

Texture2D<float4> gbuffer_world_normal : register(t5); // xyz, world normal, w roughness
Texture2D<float4> gbuffer_world_pos : register(t6); //gbuffer b

Texture3D<float> stbn_scalar_tex: register(t7);
Texture2D<float4> downsampled_world_pos : register(t8);

RWTexture2D<float4> output_diffuse_indirect : register(u0);
RWTexture2D<float4> output_specular_indirect : register(u1);

cbuffer CBRestirSceneInfo : register(b0)
{
    RESTIR_SCENE_INFO_COMMON
};

// from unrealengine
float D_GGX( float a2, float NoH )
{
	float d = ( NoH * a2 - NoH ) * NoH + 1;	// 2 mad
	return a2 / ( 3.14159265358979 *d*d );					// 4 mul, 1 rcp
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void UpscaleAndIntegrateCS(uint2 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 current_pixel_pos = dispatch_thread_id;
    float3 world_position = gbuffer_world_pos[current_pixel_pos].xyz;
    if(all(current_pixel_pos < g_full_screen_texsize) && any(world_position != float3(0,0,0)))
    {
        float3 world_normal = gbuffer_world_normal[current_pixel_pos].xyz;
        float roughness = gbuffer_world_normal[current_pixel_pos].z;

        float4 scene_plane = float4(world_normal, dot(world_position, world_normal));
        float3 vec_to_camera = g_camera_worldpos - world_position;
        float3 view_direction = normalize(vec_to_camera);
        float distance_to_camera = length(vec_to_camera);

        const uint upscale_number_sample = 16;
        const float upscale_kernel_size = 3.0;
        const float kernel_scale = upscale_kernel_size * 2.0 * 4.0 / upscale_number_sample;

        float noise = GetBlueNoiseScalar(stbn_scalar_tex, current_pixel_pos, g_current_frame_index);

        float3 weighted_diffuse_lighting = 0;
        float3 weighted_specular_lighting = 0;
        float total_weight = 0;

        //float mean = 0.0;
        //float s = 0.0;

        for(uint sample_index; sample_index < upscale_number_sample; sample_index++)
        {
            const float angle = (sample_index + noise) * 2.3999632f;
            const float radius = pow(float(sample_index), 0.666f) * kernel_scale;

            const int2 reservoir_pixel_offset = int2(floor(float2(cos(angle), sin(angle)) * radius));
            uint2 reservior_sample_coord = clamp((current_pixel_pos / 2 + reservoir_pixel_offset) , uint2(0,0), int2(g_restir_texturesize) - int2(1,1));

            float3 reservoir_world_position = downsampled_world_pos[reservior_sample_coord].xyz;
            if(any(reservoir_world_position != float3(0,0,0)))
            {
                SReservoir reservoir_sample = LoadReservoir(reservior_sample_coord, reservoir_ray_direction, reservoir_ray_radiance, reservoir_hit_distance, reservoir_hit_normal, reservoir_weights);
                float plane_distance = abs(dot(float4(reservoir_world_position, -1.0), scene_plane));

                float relative_distance = plane_distance / distance_to_camera;
                float depth_weight = exp2(-10000.0 * (relative_distance * relative_distance));
                
                // diffuse lighting
                float3 sample_light = reservoir_sample.m_sample.radiance * reservoir_sample.M;
                weighted_diffuse_lighting += sample_light * depth_weight * max(dot(world_normal, reservoir_sample.m_sample.ray_direction),0.0f);

                // specular lighting
                float3 H = normalize(view_direction + reservoir_sample.m_sample.ray_direction);
                float NoH = saturate(dot(world_normal, H));
                float D = D_GGX(,NoH);
                const float Vis_Implicit = 0.25;
                weighted_specular_lighting += ToneMappingLighting(sample_light * D * Vis_Implicit) * depth_weight;

                // weight
                total_weight += depth_weight;
                
                // bilateral filter
                //float sample_luma = Luma(sample_light);
                //float old_mean = mean;
                //mean += (depth_weight / total_weight) * (sample_luma - old_mean);
                //s += depth_weight * (sample_luma - mean) * (sample_luma - old_mean);
            }
        }

         float3 diffuse_lighting = (total_weight > 0.0) ? weighted_diffuse_lighting / total_weight : 0.0;
         float3 specular_lighting = (total_weight > 0.0) ? weighted_specular_lighting / total_weight : 0.0;

         output_diffuse_indirect[current_pixel_pos] = float4(diffuse_lighting / 3.1415926535, 1.0);
         output_specular_indirect[current_pixel_pos] = float4(specular_lighting, 1.0);
    }
    else
    {
        output_diffuse_indirect[current_pixel_pos] = float4(0,0,0,1);
        output_specular_indirect[current_pixel_pos] = float4(0,0,0,1);
    }

   
}