#define RAY_TRACING_MASK_OPAQUE				0x01
#define RT_MATERIAL_SHADER_INDEX            0
#define RT_SHADOW_SHADER_INDEX              1

#define PI 3.1415926535

SamplerState gSamPointWarp : register(s0, space1000);
SamplerState gSamLinearWarp : register(s4, space1000);
SamplerState gSamLinearClamp : register(s5, space1000);

#define SUN_INTENSITY 10.0

// transform: indentity matrix
struct RayTraceMeshInfo
{
    uint  m_indexOffsetBytes;
    uint  m_uvAttributeOffsetBytes;
    uint  m_normalAttributeOffsetBytes;
    uint  m_tangentAttributeOffsetBytes;
    uint  m_bitangentAttributeOffsetBytes;
    uint  m_positionAttributeOffsetBytes;
    uint  m_attributeStrideBytes;
    uint  m_materialInstanceId;
};

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

float Pow2(float x)
{
    return x * x;
}

float3x3 GetTangentBasis( float3 TangentZ )
{
	const float Sign = TangentZ.z >= 0 ? 1 : -1;
	const float a = -rcp( Sign + TangentZ.z );
	const float b = TangentZ.x * TangentZ.y * a;
	
	float3 TangentX = { 1 + Sign * a * Pow2( TangentZ.x ), Sign * b, -Sign * TangentZ.x };
	float3 TangentY = { b,  Sign + a * Pow2( TangentZ.y ), -TangentZ.y };

	return float3x3( TangentX, TangentY, TangentZ );
}

float Luminance(float3 color)
{
    return dot(color,float3(0.3,0.59,0.11));
}

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

float2 GetBlueNoiseVector2(Texture2D<float2> stbn_vec2_tex, uint2 screen_position,uint frame_index)
{
    uint3 texture_coord_3d = uint3(screen_position.xy, frame_index) % int3(128,128,64);
    return stbn_vec2_tex.Load(uint3(texture_coord_3d.x, (texture_coord_3d.z * 128) + texture_coord_3d.y,0)).xy;
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


RaytracingAccelerationStructure rtScene : register(t0);
Texture2D<float4> gbuffer_a: register(t1);
Texture2D<float4> gbuffer_b: register(t2);
StructuredBuffer<RayTraceMeshInfo> rt_scene_mesh_gpu_data : register(t3);
ByteAddressBuffer scene_idx_buffer : register(t4);
ByteAddressBuffer scene_vtx_buffer : register(t5);
Texture2D<float2> stbn_vec2_tex3d: register(t6);

RWTexture2D<float4> rw_reservoir_ray_direction : register(u0);
RWTexture2D<float4> rw_reservoir_ray_radiance : register(u1);
RWTexture2D<float> rw_reservoir_hit_distance : register(u2);
RWTexture2D<float4> rw_reservoir_hit_normal : register(u3);
RWTexture2D<float4> rw_reservoir_weights : register(u4);

RWTexture2D<float4> rw_downsampled_world_pos : register(u5);
RWTexture2D<float4> rw_downsampled_world_normal : register(u6);


cbuffer CBRayTracingInformation : register(b0)
{
	float4 light_direction;
	float4 g_restir_texturesize;
	float4 g_full_screen_texsize;
    uint g_current_frame_index;
}

Texture2D<float4> bindless_texs[] : register(t0, space2);


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
        return;
    }

    rw_downsampled_world_pos[reservoir_coord] = float4(world_position.xyz,0.0);
    rw_downsampled_world_normal[reservoir_coord] = float4(world_normal,0.0);

    float2 E = GetBlueNoiseVector2(stbn_vec2_tex3d, reservoir_coord, g_current_frame_index);
    float4 hemi_sphere_sample = UniformSampleHemisphere(E);
    float3x3 tangent_basis = GetTangentBasis(world_normal);

    //TODO: fix me !!!!!!!!!!!!!!!!!!!!!!!!!!
    float3 local_ray_direction = hemi_sphere_sample.xyz;
    float3 world_ray_direction = normalize(mul(local_ray_direction, tangent_basis));

    RayDesc ray;
    ray.Origin = world_position + world_normal * 0.00005f;
    ray.Direction = world_ray_direction;

    //TODO: fix me !!!!!!!!!!!!!!!!!!!!!!!!!!
    ray.TMin = 0.0;
    ray.TMax = 1e10;

    SInitialSamplingRayPayLoad payLoad = (SInitialSamplingRayPayLoad)0;
    TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_MATERIAL_SHADER_INDEX, 1,0, ray, payLoad);

    float3 radiance = float3(0,0,0);

    // trace a visibility ray from the world pos along the direction
    //if(payLoad.hit_t <= 0.0)
    //{
    //    // sky light
    //}

    if(payLoad.hit_t > 0.0)
    {
        RayDesc shadow_ray;
        shadow_ray.Origin = payLoad.world_position + payLoad.world_normal * 0.00005f;
        shadow_ray.TMin = 0.0;
        shadow_ray.Direction = light_direction.xyz;
        shadow_ray.TMax = payLoad.hit_t;

        SInitialSamplingRayPayLoad shadow_payLoad = (SInitialSamplingRayPayLoad)0;
        TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_SHADOW_SHADER_INDEX, 1,0, shadow_ray, shadow_payLoad);

        if(shadow_payLoad.hit_t <= 0.0)
        {
            float NoL = saturate(dot(light_direction.xyz, payLoad.world_normal));
            radiance = NoL * SUN_INTENSITY * payLoad.albedo * (1.0 / PI);
        }

        // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        //radiance = payLoad.albedo;
    }
    else
    {
        radiance = float3(0.2,0.2,0.2);
        payLoad.world_normal = -ray.Direction;
    }
        

    SSample initial_sample;
    initial_sample.ray_direction = ray.Direction;
    initial_sample.radiance = radiance;
    initial_sample.hit_distance = payLoad.hit_t;
    initial_sample.hit_normal = payLoad.world_normal;
    initial_sample.pdf = hemi_sphere_sample.w;

    float target_pdf = Luminance(radiance);
    float w = target_pdf / initial_sample.pdf;

    SReservoir reservoir;
    InitializeReservoir(reservoir);
    UpdateReservoir(reservoir, initial_sample, w, 0);
    reservoir.m_sample.pdf = target_pdf;
    reservoir.comb_weight = reservoir.weight_sum / (max(reservoir.M * reservoir.m_sample.pdf, 0.00001f));

    StoreReservoir(reservoir_coord, reservoir, rw_reservoir_ray_direction, rw_reservoir_ray_radiance, rw_reservoir_hit_distance, rw_reservoir_hit_normal, rw_reservoir_weights);
}

uint3 Load3x16BitIndices(
    uint offsetBytes)
{
    const uint dwordAlignedOffset = offsetBytes & ~3;

    const uint2 four16BitIndices = scene_idx_buffer.Load2(dwordAlignedOffset);

    uint3 indices;

    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

[shader("closesthit")]
void InitialSamplingRayHit(inout SInitialSamplingRayPayLoad payload, in SRayTracingIntersectionAttributes attributes)
{
    if(HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE)
    {
        const float3 bary = float3(1.0 - attributes.x - attributes.y, attributes.x, attributes.y);

        RayTraceMeshInfo info = rt_scene_mesh_gpu_data[InstanceID()];
        const uint3 ii = Load3x16BitIndices(info.m_indexOffsetBytes + PrimitiveIndex() * 3 * 2);

        const float2 uv0 = asfloat(scene_vtx_buffer.Load2((info.m_uvAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes)));
        const float2 uv1 = asfloat(scene_vtx_buffer.Load2((info.m_uvAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes)));
        const float2 uv2 = asfloat(scene_vtx_buffer.Load2((info.m_uvAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes)));

        const float3 normal0 = asfloat(scene_vtx_buffer.Load3(info.m_normalAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
        const float3 normal1 = asfloat(scene_vtx_buffer.Load3(info.m_normalAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
        const float3 normal2 = asfloat(scene_vtx_buffer.Load3(info.m_normalAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
    
        //  const float3 tangent0 = asfloat(scene_vtx_buffer.Load3(info.m_tangentAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
        //  const float3 tangent1 = asfloat(scene_vtx_buffer.Load3(info.m_tangentAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
        //  const float3 tangent2 = asfloat(scene_vtx_buffer.Load3(info.m_tangentAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
        //  
        //  // Reintroduced the bitangent because we aren't storing the handedness of the tangent frame anywhere.  Assuming the space
        //  // is right-handed causes normal maps to invert for some surfaces.  The Sponza mesh has all three axes of the tangent frame.
        //  //float3 vsBitangent = normalize(cross(vsNormal, vsTangent)) * (isRightHanded ? 1.0 : -1.0);
        //  const float3 bitangent0 = asfloat(scene_vtx_buffer.Load3(info.m_bitangentAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
        //  const float3 bitangent1 = asfloat(scene_vtx_buffer.Load3(info.m_bitangentAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
        //  const float3 bitangent2 = asfloat(scene_vtx_buffer.Load3(info.m_bitangentAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));

        float2 uv = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;
        float3 vsNormal = normalize(normal0 * bary.x + normal1 * bary.y + normal2 * bary.z);
        //float3 vsTangent = normalize(tangent0 * bary.x + tangent1 * bary.y + tangent2 * bary.z);
        //float3 vsBitangent = normalize(bitangent0 * bary.x + bitangent1 * bary.y + bitangent2 * bary.z);

        Texture2D<float4> diffTex = bindless_texs[info.m_materialInstanceId * 2 + 0];
        //Texture2D<float4> normalTex = bindless_texs[info.m_materialInstanceId * 2 + 1];

        const float3 diffuseColor = diffTex.SampleLevel(gSamLinearWarp, uv, 0).rgb;
        //const float3 tex_normal = normalTex.SampleLevel(gSamLinearWarp, uv, 0).rgb;
        //float3x3 tbn = float3x3(vsTangent, vsBitangent, vsNormal);
        //float3 normal = normalize(mul(tex_normal, tbn));

        float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

        payload.world_normal = vsNormal;
        payload.world_position = worldPosition;
        payload.albedo = diffuseColor;
        payload.hit_t = RayTCurrent();
    }
}

[shader("closesthit")]
void ShadowClosestHitMain(inout SInitialSamplingRayPayLoad payload, in SRayTracingIntersectionAttributes attributes)
{
    payload.hit_t = RayTCurrent();;
}

[shader("miss")]
void RayMiassMain(inout SInitialSamplingRayPayLoad payload)
{
    payload.hit_t = -1.0;
}