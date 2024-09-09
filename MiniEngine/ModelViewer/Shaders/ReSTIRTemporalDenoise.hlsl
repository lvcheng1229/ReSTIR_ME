#define GROUP_SIZE 16
#include "ReSTIRCommon.hlsl"

Texture2D<float3> input_diffuse_indirect : register(t0);
Texture2D<float3> input_specular_indirect : register(t1);

Texture2D<float4> gbuffer_world_pos : register(t5);
Texture2D<float4> gbuffer_world_normal : register(t5); // xyz, world normal, w roughness

RWTexture2D<float3> output_diffuse_indirect : register(u0);
RWTexture2D<float3> output_specular_indirect : register(u1);


[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void ReSTIRSpatialDenoiseCS(uint2 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 current_pixel_pos = dispatch_thread_id;
    
    float3 filtered_diffuse_indirect = float3(0,0,0);
    float3 filtered_specular_indirect = float3(0,0,0);

    if(all(current_pixel_pos < g_full_screen_texsize))
    {
        float3 world_position = gbuffer_world_pos[current_pixel_pos].xyz;
        if(any(world_position != float3(0,0,0)))
        {
            filtered_diffuse_indirect = ToneMappingLighting(input_diffuse_indirect[current_pixel_pos]);
            filtered_specular_indirect = ToneMappingLighting(input_specular_indirect[current_pixel_pos]);

            float3 world_normal = gbuffer_world_normal[current_pixel_pos].xyz;
            float4 current_sample_scene_plane = float4(world_normal, dot(world_position, world_normal));

            const float biliteral_filter_radius = 2.0f * 2.0f / 1000.0f;
            float kernel_radius = biliteral_filter_radius * g_full_screen_texsize.x;
            float guassian_normalize = 2.0 / (kernel_radius * kernel_radius);
            uint2 random_seed = Rand3DPCG32(int3(current_pixel_pos, g_current_frame_index % 8)).xy;

            float3 sample_camera_distance = distance(g_camera_worldpos, world_position);

            float total_weight = 0.0f;
            const uint biliteral_sample_num = 16;
            for(int sample_index = 0; sample_index < biliteral_sample_num; sample_index++)
            {
                float2 offset = (Hammersley16(sample_index, biliteral_sample_num, random_seed) - 0.5f) * 2.0f * kernel_radius;
                int2 neighbor_position = current_pixel_pos + offset;
                if(all(neighbor_position < g_full_screen_texsize) && all(neighbor_position >= int2(0,0)))
                {
                    float3 neighbor_world_position = gbuffer_world_pos[neighbor_position].xyz;
                    if(any(neighbor_world_position != float3(0,0,0)))
                    {
                        float plane_distance = abs(dot(float4(neighbor_world_position, -1.0), current_sample_scene_plane));
                        float relative_difference = plane_distance / sample_camera_distance;
                        
                        float depth_weight = exp2(-10000.0f * (relative_difference * relative_difference));
                        
                        float spatial_weight = exp2(-guassian_normalize* dot(offset, offset));

                        float3 neighbor_normal = gbuffer_world_normal[neighbor_position].xyz;
                        float angle_between_normal = acosFast(saturate(dot(world_normal, neighbor_normal)));
                        float normal_weight = 1.0 - saturate(angle_between_normal * angle_between_normal);

                        float sample_weight = spatial_weight * depth_weight * normal_weight;
                        filtered_diffuse_indirect += ToneMappingLighting(input_diffuse_indirect[neighbor_position]) * sample_weight;
                        filtered_specular_indirect += ToneMappingLighting(input_specular_indirect[neighbor_position]) * sample_weight;
                        total_weight += sample_weight;
                    }
                }
            }

            filtered_diffuse_indirect = filtered_diffuse_indirect / total_weight;
            filtered_specular_indirect = filtered_specular_indirect / total_weight;

            output_diffuse_indirect[current_pixel_pos] = InverseToneMappingLight(filtered_diffuse_indirect);
        output_specular_indirect[current_pixel_pos] = InverseToneMappingLight(filtered_specular_indirect);
        }
    }

    output_diffuse_indirect[current_pixel_pos] = filtered_diffuse_indirect;
    output_specular_indirect[current_pixel_pos] = filtered_specular_indirect;
}