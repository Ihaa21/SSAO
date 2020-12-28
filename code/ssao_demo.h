#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

/*

  NOTE: The goal for this demo is to have a scene where we can switch between various rendering methods:

    - Forward
    - Deferred
    - Tiled Forward
    - Tiled Deferred
    - Textureless Deferred

    For the forward cases, I want to also implement MSAA to see how to make it work. We also want to support transparent geometry for all
    passes. Vegetation might be a good thing to add for that. We might also want to look at adding screen space effects to all these
    techniques, its a bit weirder with forward ones since we don't have a GBuffer but might be worth taking a shot at it (look at how DOOM
    does it).

    References:

    - https://www.3dgep.com/forward-plus/#G-Buffer_Pass
    
    
 */

struct directional_light
{
    v3 Color;
    u32 Pad0;
    v3 Dir;
    u32 Pad1;
    v3 AmbientColor;
    u32 Pad2;
};

struct point_light
{
    v3 Color;
    u32 Pad0;
    v3 Pos;
    f32 MaxDistance;
};

struct scene_globals
{
    v3 CameraPos;
    u32 NumPointLights;
};

struct instance_entry
{
    u32 MeshId;
    m4 WTransform;
    m4 WVPTransform;
};

struct gpu_instance_entry
{
    m4 WTransform;
    m4 WVPTransform;
};

struct render_mesh
{
    vk_image Color;
    vk_image Normal;
    VkDescriptorSet MaterialDescriptor;
    
    VkBuffer VertexBuffer;
    VkBuffer IndexBuffer;
    u32 NumIndices;
};

struct render_scene;
struct renderer_create_info
{
    u32 Width;
    u32 Height;
    VkFormat ColorFormat;

    VkDescriptorSetLayout MaterialDescLayout;
    VkDescriptorSetLayout SceneDescLayout;
    render_scene* Scene;
};

#include "tiled_deferred.h"

struct render_scene
{
    // NOTE: General Render Data
    camera Camera;
    VkDescriptorSetLayout MaterialDescLayout;
    VkDescriptorSetLayout SceneDescLayout;
    VkBuffer SceneBuffer;
    VkDescriptorSet SceneDescriptor;

    // NOTE: Scene Lights
    u32 MaxNumPointLights;
    u32 NumPointLights;
    point_light* PointLights;
    VkBuffer PointLightBuffer;
    VkBuffer PointLightTransforms;
    
    directional_light DirectionalLight;
    VkBuffer DirectionalLightBuffer;

    // NOTE: Scene Meshes
    u32 MaxNumRenderMeshes;
    u32 NumRenderMeshes;
    render_mesh* RenderMeshes;
    
    // NOTE: Opaque Instances
    u32 MaxNumOpaqueInstances;
    u32 NumOpaqueInstances;
    instance_entry* OpaqueInstances;
    VkBuffer OpaqueInstanceBuffer;
};

struct demo_state
{
    linear_arena Arena;
    linear_arena TempArena;

    // NOTE: Samplers
    VkSampler PointSampler;
    VkSampler LinearSampler;
    VkSampler AnisoSampler;

    // NOTE: Render Target Entries
    VkFormat SwapChainFormat;
    render_target_entry SwapChainEntry;
    render_target CopyToSwapTarget;
    VkDescriptorSetLayout CopyToSwapDescLayout;
    VkDescriptorSet CopyToSwapDesc;
    render_fullscreen_pass CopyToSwapPass;

    render_scene Scene;

    // NOTE: Saved model ids
    u32 Quad;
    u32 Cube;
    u32 Sphere;

    tiled_deferred_state TiledDeferredState;
};

global demo_state* DemoState;
