#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "shader_descriptor_layouts.cpp"
#include "shader_blinn_phong_lighting.cpp"

//
// NOTE: Descriptor Sets
//

TILED_DEFERRED_DESCRIPTOR_LAYOUT(0)
SCENE_DESCRIPTOR_LAYOUT(1)
MATERIAL_DESCRIPTOR_LAYOUT(2)

//
// NOTE: Grid Frustum Shader
//

#if GRID_FRUSTUM

layout(local_size_x = TILE_DIM_IN_PIXELS, local_size_y = TILE_DIM_IN_PIXELS, local_size_z = 1) in;

void main()
{
    uvec2 GridPos = uvec2(gl_GlobalInvocationID.xy);
    if (GridPos.x < GridSize.x && GridPos.y < GridSize.y)
    {
        // NOTE: Compute four corner points of tile
        vec3 CameraPos = vec3(0);
        vec4 BotLeft = vec4((GridPos + vec2(0, 0)) * vec2(TILE_DIM_IN_PIXELS), 0, 1);
        vec4 BotRight = vec4((GridPos + vec2(1, 0)) * vec2(TILE_DIM_IN_PIXELS), 0, 1);
        vec4 TopLeft = vec4((GridPos + vec2(0, 1)) * vec2(TILE_DIM_IN_PIXELS), 0, 1);
        vec4 TopRight = vec4((GridPos + vec2(1, 1)) * vec2(TILE_DIM_IN_PIXELS), 0, 1);
     
        // NOTE: Transform corner points to far plane in view space (we assume a counter clock wise winding order)
        BotLeft = ScreenToView(InverseProjection, ScreenSize, BotLeft);
        BotRight = ScreenToView(InverseProjection, ScreenSize, BotRight);
        TopLeft = ScreenToView(InverseProjection, ScreenSize, TopLeft);
        TopRight = ScreenToView(InverseProjection, ScreenSize, TopRight);
   
        // NOTE: Build the frustum planes and store
        frustum Frustum;
        Frustum.Planes[0] = PlaneCreate(CameraPos, BotLeft.xyz, TopLeft.xyz);
        Frustum.Planes[1] = PlaneCreate(CameraPos, TopRight.xyz, BotRight.xyz);
        Frustum.Planes[2] = PlaneCreate(CameraPos, TopLeft.xyz, TopRight.xyz);
        Frustum.Planes[3] = PlaneCreate(CameraPos, BotRight.xyz, BotLeft.xyz);
        
        // NOTE: Write out to buffer
        uint WriteIndex = GridPos.y * GridSize.x + GridPos.x;
        GridFrustums[WriteIndex] = Frustum;
    }
}

#endif

//
// NOTE: Light Culling Shader
//

#if LIGHT_CULLING

shared frustum SharedFrustum;
shared uint SharedMinDepth;
shared uint SharedMaxDepth;

// NOTE: Opaque
shared uint SharedGlobalLightId_O;
shared uint SharedCurrLightId_O;
shared uint SharedLightIds_O[1024];

// NOTE: Transparent
shared uint SharedGlobalLightId_T;
shared uint SharedCurrLightId_T;
shared uint SharedLightIds_T[1024];

void LightAppendOpaque(uint LightId)
{
    uint WriteArrayId = atomicAdd(SharedCurrLightId_O, 1);
    if (WriteArrayId < 1024)
    {
        SharedLightIds_O[WriteArrayId] = LightId;
    }
}

void LightAppendTransparent(uint LightId)
{
    uint WriteArrayId = atomicAdd(SharedCurrLightId_T, 1);
    if (WriteArrayId < 1024)
    {
        SharedLightIds_T[WriteArrayId] = LightId;
    }
}

layout(local_size_x = TILE_DIM_IN_PIXELS, local_size_y = TILE_DIM_IN_PIXELS, local_size_z = 1) in;

void main()
{    
    uint NumThreadsPerGroup = TILE_DIM_IN_PIXELS * TILE_DIM_IN_PIXELS;

    // NOTE: Skip threads that go past the screen
    if (!(gl_GlobalInvocationID.x < ScreenSize.x && gl_GlobalInvocationID.y < ScreenSize.y))
    {
        return;
    }
    
    // NOTE: Setup shared variables
    if (gl_LocalInvocationIndex == 0)
    {
        SharedFrustum = GridFrustums[uint(gl_WorkGroupID.y) * GridSize.x + uint(gl_WorkGroupID.x)];
        SharedMinDepth = 0xFFFFFFFF;
        SharedMaxDepth = 0;
        SharedCurrLightId_O = 0;
        SharedCurrLightId_T = 0;
    }

    barrier();
    
    // NOTE: Calculate min/max depth in grid tile (since our depth values are between 0 and 1, we can reinterpret them as ints and
    // comparison will still work correctly)
    ivec2 ReadPixelId = ivec2(gl_GlobalInvocationID.xy);
    uint PixelDepth = floatBitsToInt(texelFetch(GBufferDepthTexture, ReadPixelId, 0).x);
    atomicMin(SharedMinDepth, PixelDepth);
    atomicMax(SharedMaxDepth, PixelDepth);

    barrier();

    // NOTE: Convert depth bounds to frustum planes in view space
    float MinDepth = uintBitsToFloat(SharedMinDepth);
    float MaxDepth = uintBitsToFloat(SharedMaxDepth);

    MinDepth = ClipToView(InverseProjection, vec4(0, 0, MinDepth, 1)).z;
    MaxDepth = ClipToView(InverseProjection, vec4(0, 0, MaxDepth, 1)).z;

    float NearClipDepth = ClipToView(InverseProjection, vec4(0, 0, 1, 1)).z;
    plane MinPlane = { vec3(0, 0, 1), MaxDepth };
    
    // NOTE: Cull lights against tiles frustum (each thread culls one light at a time)
    for (uint LightId = gl_LocalInvocationIndex; LightId < SceneBuffer.NumPointLights; LightId += NumThreadsPerGroup)
    {
        point_light Light = PointLights[LightId];
        if (SphereInsideFrustum(Light.Pos, Light.MaxDistance, SharedFrustum, NearClipDepth, MinDepth))
        {
            LightAppendTransparent(LightId);

            if (!SphereInsidePlane(Light.Pos, Light.MaxDistance, MinPlane))
            {
                LightAppendOpaque(LightId);
            }
        }
    }

    barrier();

    // NOTE: Get space and light index lists
    if (gl_LocalInvocationIndex == 0)
    {
        ivec2 WritePixelId = ivec2(gl_WorkGroupID.xy);

        // NOTE: Without the ifs, we get a lot of false positives, might be quicker to skip the atomic? Idk if this matters a lot
        if (SharedCurrLightId_O != 0)
        {
            SharedGlobalLightId_O = atomicAdd(LightIndexCounter_O, SharedCurrLightId_O);
            imageStore(LightGrid_O, WritePixelId, ivec4(SharedGlobalLightId_O, SharedCurrLightId_O, 0, 0));
        }
        if (SharedCurrLightId_T != 0)
        {
            SharedGlobalLightId_T = atomicAdd(LightIndexCounter_T, SharedCurrLightId_T);
            imageStore(LightGrid_T, WritePixelId, ivec4(SharedGlobalLightId_T, SharedCurrLightId_T, 0, 0));
        }
    }

    barrier();

    // NOTE: Write opaque
    for (uint LightId = gl_LocalInvocationIndex; LightId < SharedCurrLightId_O; LightId += NumThreadsPerGroup)
    {
        LightIndexList_O[SharedGlobalLightId_O + LightId] = SharedLightIds_O[LightId];
    }

    // NOTE: Write transparent
    for (uint LightId = gl_LocalInvocationIndex; LightId < SharedCurrLightId_T; LightId += NumThreadsPerGroup)
    {
        LightIndexList_T[SharedGlobalLightId_T + LightId] = SharedLightIds_T[LightId];
    }
}

#endif

//
// NOTE: GBuffer Vertex
//

#if GBUFFER_VERT

layout(location = 0) in vec3 InPos;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUv;

layout(location = 0) out vec3 OutWorldPos;
layout(location = 1) out vec3 OutWorldNormal;
layout(location = 2) out vec2 OutUv;

void main()
{
    instance_entry Entry = InstanceBuffer[gl_InstanceIndex];
    
    gl_Position = Entry.WVPTransform * vec4(InPos, 1);
    OutWorldPos = (Entry.WTransform * vec4(InPos, 1)).xyz;
    OutWorldNormal = (Entry.WTransform * vec4(InNormal, 0)).xyz;
    OutUv = InUv;
}

#endif

//
// NOTE: GBuffer Fragment
//

#if GBUFFER_FRAG

layout(location = 0) in vec3 InWorldPos;
layout(location = 1) in vec3 InWorldNormal;
layout(location = 2) in vec2 InUv;

layout(location = 0) out vec4 OutWorldPos;
layout(location = 1) out vec4 OutWorldNormal;
layout(location = 2) out vec4 OutColor;

void main()
{
    OutWorldPos = vec4(InWorldPos, 0);
    // TODO: Add normal mapping
    OutWorldNormal = vec4(normalize(InWorldNormal), 0);
    OutColor = texture(ColorTexture, InUv);
}

#endif

//
// NOTE: Directional Light Vert
//

#if TILED_DEFERRED_LIGHTING_VERT

layout(location = 0) in vec3 InPos;

void main()
{
    gl_Position = vec4(2.0*InPos, 1);
}

#endif

//
// NOTE: Tiled Deferred Lighting
//

#if TILED_DEFERRED_LIGHTING_FRAG

layout(location = 0) out vec4 OutColor;

void main()
{
    vec3 CameraPos = SceneBuffer.CameraPos;
    ivec2 PixelPos = ivec2(gl_FragCoord.xy);
    
    vec3 SurfacePos = texelFetch(GBufferPositionTexture, PixelPos, 0).xyz;
    vec3 SurfaceNormal = texelFetch(GBufferNormalTexture, PixelPos, 0).xyz;
    vec3 SurfaceColor = texelFetch(GBufferColorTexture, PixelPos, 0).rgb;
    float Ao = texelFetch(SsaoTexture, PixelPos, 0).x;
    vec3 View = normalize(CameraPos - SurfacePos);

    vec3 Color = vec3(0);

    // NOTE: Calculate lighting for point lights
    ivec2 GridPos = PixelPos / ivec2(TILE_DIM_IN_PIXELS);
    uvec2 LightIndexMetaData = imageLoad(LightGrid_O, GridPos).xy; // NOTE: Stores the pointer + # of elements
    for (int i = 0; i < LightIndexMetaData.y; ++i)
    {
        uint LightId = LightIndexList_O[LightIndexMetaData.x + i];
        point_light CurrLight = PointLights[LightId];
        vec3 LightDir = normalize(SurfacePos - CurrLight.Pos);
        Color += BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, 32, LightDir, PointLightAttenuate(SurfacePos, CurrLight));
    }

    // NOTE: Calculate lighting for directional lights
    {
        Color += BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, 32, DirectionalLight.Dir, DirectionalLight.Color);
        Color += Ao * DirectionalLight.AmbientLight * SurfaceColor;
    }

    OutColor = vec4(Color, 1);
    OutColor = vec4(vec3(Ao), 1);
}

#endif
