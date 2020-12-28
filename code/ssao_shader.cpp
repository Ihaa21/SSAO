#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "shader_descriptor_layouts.cpp"

/*

  NOTE: Ambient Occlusion is a approximation of a real world phenomina. The regular rendering integral doesn't have a concept like this
        built into it. Instead, we make 2 assumptions that make ambient occlusion real and practical:

        1) Ambient Light is uniformly incident (the same from all directions)
        2) Ambient Light is independent of viewing angle

        ^ These are generally things we already do in analytic light rendering so they are fair assumptions to make. Together this gives
        us the following integral that gives occlusion:

        AmbientOcclusion = Integral(All Directions, dot(surface_normal, incoming_direction) * d_incoming_direction)
                         = (1/pi)*Integral(All Directions, dot(surface_normal, incoming_direction) * d_incoming_direction)

        ^ There is some variability here with how big we define the hemisphere. We say its all directions but if we don't clamp the
        hemisphere, all rays will generally hit something, except maybe the sky. Really, its a epsilon value that gets tuned since we only
        want this to account for high frequency details.
  
 */

TILED_DEFERRED_DESCRIPTOR_LAYOUT(0)

layout(set = 1, binding = 0) uniform ssao_input_buffer
{
    mat4 VPTransform;
    vec4 HemisphereSamples[64];
    vec4 RandomRotations[16];
} SsaoInputBuffer;

layout(location = 0) out float OutOcclusion;

//
// NOTE: Standard SSAO
//

#if STANDARD_SSAO

void main()
{
    // TODO: Input
    float Radius = 0.25;
    float Bias = 0.0000001;
    
    // NOTE: https://learnopengl.com/Advanced-Lighting/SSAO
    ivec2 PixelPos = ivec2(gl_FragCoord.xy);

    // NOTE: Get our random rotation vector
    vec3 RandomRotation;
    {
        ivec2 SamplePos = ivec2(mod(PixelPos, 4));
        RandomRotation = vec3(SsaoInputBuffer.RandomRotations[SamplePos.y * 4 + SamplePos.x].xy, 0);
    }
    
    vec3 SurfacePos = texelFetch(GBufferPositionTexture, PixelPos, 0).xyz;
    vec3 SurfaceNormal = texelFetch(GBufferNormalTexture, PixelPos, 0).xyz;
    // NOTE: Use our random rotation to generate a basis + at the same time apply the rotation
    vec3 SurfaceTangent = normalize(RandomRotation - SurfaceNormal * dot(RandomRotation, SurfaceNormal));
    vec3 SurfaceBiTangent = cross(SurfaceNormal, SurfaceTangent);
    mat3 TBN = mat3(SurfaceTangent, SurfaceBiTangent, SurfaceNormal);

    float Occlusion = 0.0f;
    for (uint SampleId = 0; SampleId < 64; ++SampleId)
    {
        vec3 Sample = Radius * TBN * SsaoInputBuffer.HemisphereSamples[SampleId].xyz + SurfacePos;
        vec4 ProjectedSample = SsaoInputBuffer.VPTransform * vec4(Sample, 1);
        ProjectedSample.xyz /= ProjectedSample.w;
        
        // NOTE: Convert to 0-1 range
        ProjectedSample.xy = 0.5 * ProjectedSample.xy + vec2(0.5);

        // NOTE: Compare to depth value
        float StoredDepth = texture(GBufferDepthTexture, ProjectedSample.xy).x;
        Occlusion += ProjectedSample.z >= (StoredDepth - Bias) ? 1.0f : 0.0f;
    }

    Occlusion /= 64.0f;
    
    OutOcclusion = Occlusion;
}

#endif

//
// NOTE: Horizon SSAO
//

#if HORIZON_SSAO

void main()
{
    
    OutColor = texture(ColorTexture, InUv);
}

#endif
