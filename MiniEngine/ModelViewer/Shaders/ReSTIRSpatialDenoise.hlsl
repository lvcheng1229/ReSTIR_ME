#define GROUP_SIZE 16
#include "ReSTIRCommon.hlsl"

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void ReSTIRTemporalDenoiseCS(uint2 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 current_pixel_pos = dispatch_thread_id;
    if(all(current_pixel_pos < g_full_screen_texsize))
    {
        float3 world_position = downsampled_world_pos[current_pixel_pos];

        float4 pre_view_pos = mul(PreViewProjMatrix,float4(world_position, 1.0));
        float2 pre_view_screen_pos = (float2(pre_view_pos.xy / pre_view_pos.w) * 0.5 + float2(0.5,0.5));
        pre_view_screen_pos.y = (1.0 - pre_view_screen_pos.y);
        uint2  pre_view_texture_pos = pre_view_screen_pos * g_restir_texturesize;

        
    }
}