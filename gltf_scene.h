#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>

#include "gltf_scene_config.h"

#ifdef GLTF_SCENE_USING_OTCV
#include "otcv.h"
#include "global_handles.h"
#include "render_global_types.h"
#endif

struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uv0;
    std::vector<glm::vec2> uv1;
    std::vector<glm::vec4> tangents;

    std::vector<uint16_t> indices;

#ifdef GLTF_SCENE_USING_OTCV
    otcv::VertexBuffer* vb;
    otcv::Buffer* ib;
#endif

    // For CPU-side ray tracing
    // AABB boundingBox;
};

struct ImageData {
    std::string uri;
    std::vector<uint8_t> pixel_data;
    int width;
    int height;
    int channels;
    int bit_depth;
};

struct TextureBinding {
    std::string name;
    std::shared_ptr<ImageData> image;

    struct SamplerConfig {
        int min_filter;
        int mag_filter;
        int wrap_s;
        int wrap_t;
    };
    SamplerConfig sampler_config;

    int texcoord_id = 0;
};

enum class AlphaMode {
    Opaque = 0,
    Mask,
    Blend
};

struct MaterialData {
    std::string name;

    glm::vec4 base_color_factor = glm::vec4(1.0f);
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    float normal_scale = 1.0f;
    float occlusion_strength = 0.0f;
    
    AlphaMode alpha_mode = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;

    std::shared_ptr<TextureBinding> base_color;
    std::shared_ptr<TextureBinding> metallic_roughness;
    std::shared_ptr<TextureBinding> normal;
    std::shared_ptr<TextureBinding> occlusion;
    std::shared_ptr<TextureBinding> emissive;

#ifdef GLTF_SCENE_USING_OTCV
    MaterialHandle material_handle;
#endif
};

struct Renderable {
    std::shared_ptr<MeshData> mesh;
    std::shared_ptr<MaterialData> material;
};


struct SceneNode {
    int parent = -1;
    std::string name;
    glm::mat4 local_transform = glm::mat4(1.0f);
    glm::mat4 world_transform = glm::mat4(1.0f);
    std::vector<Renderable> renderables;
#ifdef GLTF_SCENE_USING_OTCV
    DescriptorSetInfoHandle object_ubo_set_handle;
#endif
};

typedef std::vector<SceneNode> SceneGraph;

#ifdef GLTF_SCENE_USING_OTCV
struct RenderableId {
    size_t scene_node_id; // Id of a SceneNode in SceneGraph
    size_t renderable_id; // Id of a renderable in Scenenode::renderables
};

struct MaterialBatch {
    MaterialHandle material;
    std::vector<RenderableId> renderables;
};

struct PipelineBatch {
    PipelineHandle pipeline;
    std::vector<MaterialBatch> material_batches;
};

typedef std::vector<PipelineBatch> BindOrder;

// typedef std::map<MaterialHandle, std::vector<RenderableId>> MaterialBatch;
// typedef std::map<PipelineHandle, MaterialBatch> PipelineBatch;
// typedef std::map<RenderPassType, PipelineBatch> RenderOrderMap;

/*
RenderOrderMap is structured like this:

               |<-----------------------PipelineStateBatch_0-------------------------->| |<-----------------PipelineStateBatch_1----------------->|
                            |<--------renderables that share the same pipeline------->|
Pass_0   ---   [ pipeline_0 [Renderable_1] [Renderable_2] [Renderable_6] [Renderable_9]] [ pipeline_2 [Renderable_3] [Renderable_7] [Renderable_8]] [...] ...
Pass_1   ---   [...] [...] ...
Pass_2   ---   ...

A material might need multiple passes to get fully drawn. RenderOrderMap is indexed by render pass number.
How many passes and what they do depends on the renderer. 
Each PipelineStateBatch is an aggregation of renderables that share the same graphics pipeline.
A PipelineStateBatch is submitted together and pipeline switches only happen between batches,
keeping the number of switches to a minimum.
*/
#endif