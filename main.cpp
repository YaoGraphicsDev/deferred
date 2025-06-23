#include "otcv.h"
#include "otcv_utils.h"

#include "input_handler.h"
#include "arcball.h"

#include "gltf_parser_bindless.h"
#include "render_global_types.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_otcv.h"

#include "camera.h"
#include "static_ubo.h"

#include "shadow_manager.h"
#include "postprocess_manager.h"

#include "noise.h"

#include <iostream>
#include <array>
#include <random>

const int window_width = 1920;
const int window_height = 960;
const int cascaded_shadowmap_size = 2048;
const int cascaded_shadowmap_layers = 3;
const int jitter_tile_size = 8;
const int jitter_strata_per_dim = 8;
const float jitter_radius = 0.05f;
const float cascade_blend_depth = 1.0f;


PerspectiveCamera cam(
    glm::vec3(20.0f, 20.0f, 20.0f),
    glm::vec3(0.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    0.1f,
    50.0f,
    glm::radians(60.0f),
    (float)window_width / (float)window_height);

class Application {
public:
    void run() {
        init_window();
        init_vulkan_context();
        init_imgui();
        if (!load_scene()) {
            std::cout << "scene load error" << std::endl;
        }
        init_lighting_pipeline();
        init_render_targets();
        init_frame_contexts();
        init_texture();
        connect_render_targets();
        init_shadow();
        init_postprocess();
        main_loop();
        cleanup_scene();
        cleanup_imgui();
        cleanup();
    }

    void left_press_callback(double x, double y) {
        _arcball.begin(cam.eye, cam.center, cam.up, glm::ivec2(x, y), glm::ivec2(window_width, window_height));
    }

    void left_release_callback(double x, double y) {
        _arcball.end();
    }

    void left_drag_callback(double x, double y) {
        _arcball.progress(glm::ivec2(x, y), cam.eye, cam.up);
    }

    void wheel_scroll_callback(double x, double y, double offset) {
        float scale = glm::pow(0.9f, offset);
        cam.eye = glm::mix(cam.center, cam.eye, scale);
    }

    bool mouse_input_filter() {
        return !ImGui::GetIO().WantCaptureMouse;
    }

    void init_window() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        _window = glfwCreateWindow(window_width, window_height, "Deferred shading", nullptr, nullptr);

        _input_handler = std::make_shared<InputHandler>(_window);

        _input_handler->set_mouse_input_filter(std::bind(&Application::mouse_input_filter, this));
        _input_handler->set_mouse_drag_handler(std::bind(&Application::left_drag_callback, this, std::placeholders::_1, std::placeholders::_2),
            InputHandler::MouseButton::Left);
        _input_handler->set_mouse_press_handler(std::bind(&Application::left_press_callback, this, std::placeholders::_1, std::placeholders::_2),
            InputHandler::MouseButton::Left);
        _input_handler->set_mouse_release_handler(std::bind(&Application::left_release_callback, this, std::placeholders::_1, std::placeholders::_2),
            InputHandler::MouseButton::Left);
        _input_handler->set_mouse_scroll_handler(std::bind(&Application::wheel_scroll_callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    std::shared_ptr<StaticUBO> init_frame_ubo(RenderPassType pass) {
        if (pass == RenderPassType::Geometry) {
            Std140AlignmentType FrameUBO;
            FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "projectView");
            return std::make_shared<StaticUBO>(FrameUBO);
        }

        if (pass == RenderPassType::Lighting) {
            Std140AlignmentType DirectionalLight;
            DirectionalLight.add(Std140AlignmentType::InlineType::Float, "intensity");
            DirectionalLight.add(Std140AlignmentType::InlineType::Vec3, "color");
            DirectionalLight.add(Std140AlignmentType::InlineType::Vec3, "direction");
            Std140AlignmentType Cascade;
            Cascade.add(Std140AlignmentType::InlineType::Float, "zBegin");
            Cascade.add(Std140AlignmentType::InlineType::Float, "zEnd");
            Cascade.add(Std140AlignmentType::InlineType::Mat4, "lightSpaceView");
            Cascade.add(Std140AlignmentType::InlineType::Mat4, "lightSpaceProject");
            Std140AlignmentType Shadow;
            Shadow.add(Std140AlignmentType::InlineType::Vec2, "nJitterTiles");
            Shadow.add(Std140AlignmentType::InlineType::Uint, "nJitterStrataPerDim");
            Shadow.add(Std140AlignmentType::InlineType::Float, "jitterRadius");
            Shadow.add(Std140AlignmentType::InlineType::Float, "cascadeBlendDepth");
            Shadow.add(Std140AlignmentType::InlineType::Uint, "nCascades");
            Shadow.add(Std140AlignmentType::InlineType::Uint, "cascadeResolution");
            Shadow.add(Cascade, "cascades", 6);
            
            Std140AlignmentType FrameUBO;
            FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "projectInv");
            FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "viewInv");
            FrameUBO.add(DirectionalLight, "light");
            FrameUBO.add(Shadow, "shadow");
            return std::make_shared<StaticUBO>(FrameUBO);
        }

        assert(false);
        return nullptr;
    }

    void init_vulkan_context() {
        _vulkan_context = otcv::create_context(_window);
        _instance = _vulkan_context.instance->vk_instance;
        _surface = _vulkan_context.surface->vk_surface;
        _physical_device = _vulkan_context.physical_device->vk_physical_device;
        _device = _vulkan_context.device->vk_device;
        _swapchain = _vulkan_context.swapchain;
        for (uint32_t i = 0; i < _swapchain->images.size(); ++i) {
            _swapchain->mock_image(i)->initialize_state(otcv::ResourceState::Present);
        }
    }
    void init_render_targets() {
        // cascaded shadowmap
        _cascaded_shadowmap = otcv::ImageBuilder()
            .size(cascaded_shadowmap_size, cascaded_shadowmap_size, 1)
            .format(VK_FORMAT_D24_UNORM_S8_UINT)
            .layers(cascaded_shadowmap_layers)
            .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .view_type(VK_IMAGE_VIEW_TYPE_2D_ARRAY)
            .aspect(VK_IMAGE_ASPECT_DEPTH_BIT)
            .build();
        _cascaded_shadowmap->initialize_state(otcv::ResourceState::DepthStencilAttachment);
        _cascaded_shadowsampler = otcv::SamplerBuilder()
            .compare(VK_COMPARE_OP_LESS)
            .build();

        // g-buffers
        _albedo_image = otcv::ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R8G8B8A8_SRGB)
            .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build();
        _albedo_image->initialize_state(otcv::ResourceState::ColorAttachment);
        _albedo_sampler = otcv::SamplerBuilder().build();

        _normals_image = otcv::ImageBuilder()
            .size(window_width, window_height, 1)
            //.format(VK_FORMAT_R8G8B8A8_UNORM)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT)
            .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build();
        _normals_image->initialize_state(otcv::ResourceState::ColorAttachment);
        _normals_sampler = otcv::SamplerBuilder().build();

        _metallic_roughness_image = otcv::ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R8G8B8A8_UNORM)
            .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build();
        _metallic_roughness_image->initialize_state(otcv::ResourceState::ColorAttachment);
        _metallic_roughness_sampler = otcv::SamplerBuilder().build();

        _depth_image = otcv::ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_D24_UNORM_S8_UINT)
            .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .aspect(VK_IMAGE_ASPECT_DEPTH_BIT)
            .build();
        _depth_image->initialize_state(otcv::ResourceState::DepthStencilAttachment);
        _depth_sampler = otcv::SamplerBuilder().build();

        // lit image
        _lit_image = otcv::ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT)
            .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build();
        _lit_image->initialize_state(otcv::ResourceState::ColorAttachment);

        // back buffer
        _back_buffer = otcv::ImageBuilder()
            .size(window_width, window_height, 1)
            .format(_swapchain->image_info.format)
            .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build();
        _back_buffer->initialize_state(otcv::ResourceState::ColorAttachment);
    }
    void init_postprocess() {
        _postprocess_manager.reset(new PostProcessManager("./spirv/post_process/", _lit_image, _back_buffer));
    }
    void init_shadow() {
        _shadow_manager.reset(new ShadowManager(
            "./spirv/shadows/",
            "./spirv/scene_culling/",
            _cascaded_shadowmap,
            _scene_graph,
            _scene_refs,
            _bindless_data,
            _swapchain->mock_images.size()));
    }
    void init_lighting_pipeline() {
        _lighting_shader_blob = std::move(otcv::load_shaders_from_dir("./spirv/lighting_pass"));
        
        otcv::GraphicsPipelineBuilder pipeline_builder;
        pipeline_builder.pipline_rendering()
            .add_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT)
            .end();
        pipeline_builder
            .shader_vertex(_lighting_shader_blob["screen_quad.vert"])
            .shader_fragment(_lighting_shader_blob["pbr.frag"]);
        {
            otcv::VertexBufferBuilder vbb;
            vbb.add_binding()
                .add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
                .add_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
            pipeline_builder.vertex_state(vbb);
        }
        pipeline_builder
            .add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
            .add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
        /// TODO: depth test to accomodate mock depth attachment
        // pipeline_builder.depth_test();
        _lighting_pipeline = pipeline_builder.build();
    }

    void init_frame_contexts() {
        _command_pool = otcv::CommandPool::create(false, true);
        _frame_desc_set_pool = std::make_shared<NaiveExpandableDescriptorPool>();

        _frame_ctxs.resize(_swapchain->mock_images.size());
        for (FrameContext& ctx : _frame_ctxs) {
            // per-frame ubo
            ctx.frame_ubos[RenderPassType::Geometry] = init_frame_ubo(RenderPassType::Geometry);
            ctx.frame_ubos[RenderPassType::Lighting] = init_frame_ubo(RenderPassType::Lighting);

            // per-frame descriptor sets 
            ctx.frame_desc_sets[RenderPassType::Geometry] = _frame_desc_set_pool->allocate(_bindless_data->frame_descriptor_set_layout());
            ctx.frame_desc_sets[RenderPassType::Geometry]->bind_buffer(0, ctx.frame_ubos[RenderPassType::Geometry]->_buf);
            ctx.frame_desc_sets[RenderPassType::Lighting] = _frame_desc_set_pool->allocate(_lighting_pipeline->desc_set_layouts[DescriptorSetRate::PerFrame]);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_buffer(0, ctx.frame_ubos[RenderPassType::Lighting]->_buf);

            // sync objects
            ctx.graphics_fence = otcv::Fence::create();
            ctx.blit_fence = otcv::Fence::create();
            ctx.image_available_semaphore = otcv::Semaphore::create();

            // command buffers
            for (uint16_t pass = 0; pass < (uint16_t)RenderPassType::All; ++pass) {
                ctx.graphics_command_buffers[(RenderPassType)pass] = _command_pool->allocate();
            }
            ctx.blit_command_buffer = _command_pool->allocate();
        }
        _screen_quad = otcv::screen_quad_ndc();
    }
    void init_texture() {
        _noise_texture = NoiseTexture::disk_noise_texture(jitter_tile_size, jitter_strata_per_dim);
        _noise_texture_sampler = otcv::SamplerBuilder()
            .filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST)
            .address_mode(VK_SAMPLER_ADDRESS_MODE_REPEAT)
            .build();
    }
    void connect_render_targets() {
        for (FrameContext& ctx : _frame_ctxs) {
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(1, &_depth_image, &_depth_sampler);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(2, &_albedo_image, &_albedo_sampler);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(3, &_normals_image, &_normals_sampler);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(4, &_metallic_roughness_image, &_metallic_roughness_sampler);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(5, &_cascaded_shadowmap, &_cascaded_shadowsampler);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(6, &_noise_texture, &_noise_texture_sampler);
        }
    }

    void init_imgui() {
        // imgui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.DisplaySize = ImVec2(window_width, window_height);
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(_window, true);

        ImGui_ImplOTCV_InitInfo info;
        info.queue = _vulkan_context.queue;
        info.target_format = _swapchain->image_info.format;
        ImGui_ImplOTCV_Init(&info);
    }
    bool load_scene() {
        bool ret = load_gltf(
            "C:/Users/Liyao/Sources/Sponza/glTF/Sponza.gltf",
            _scene_graph,
            _scene_refs,
            _material_res);

        _bindless_data.reset(new BindlessDataManager(
            _physical_device,
            "./spirv/geometry_pass_bindless/",
            "./spirv/mesh_preprocess",
            _scene_refs.size(),
            _material_res.materials.size(),
            _material_res.images.size(),
            _material_res.sampler_cfgs.size()));

        _bindless_data->set_materials(_material_res);
        _bindless_data->set_objects(_scene_graph, _scene_refs);
        
        
        _culling.reset(new SceneCulling(
            "./spirv/scene_culling/",
            _scene_graph,
            _scene_refs,
            _swapchain->mock_images.size()));
        _culling_in = _culling->create_object_buffer_context(_scene_graph, _scene_refs, _bindless_data);
        _culling_out = _culling->create_indirect_command_context((uint32_t)PipelineVariant::All, _bindless_data);

        return ret;
    }


    void g_pass_commands(otcv::CommandBuffer* cmd_buf, uint32_t frame_id) {
        FrameContext& f_ctx = _frame_ctxs[frame_id];

        _culling->commands(cmd_buf, _culling_in, _culling_out, frame_id);

        otcv::RenderingBegin pass_begin;
        pass_begin
            .area(window_width, window_height)
            .color_attachment()
            .image_view(_albedo_image->vk_view)
            .image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .clear_value(0.0f, 0.0f, 0.0f, 1.0f)
            .end()
            .color_attachment()
            .image_view(_normals_image->vk_view)
            .image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .clear_value(0.0f, 0.0f, 0.0f, 1.0f)
            .end()
            .color_attachment()
            .image_view(_metallic_roughness_image->vk_view)
            .image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .clear_value(0.0f, 0.0f, 0.0f, 1.0f)
            .end()
            .depth_stencil_attachment()
            .image_view(_depth_image->vk_view)
            .image_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .clear_value(1.0f, 0)
            .end();
        cmd_buf->cmd_begin_rendering(pass_begin);
        
        cmd_buf->cmd_set_viewport(window_width, window_height);
        cmd_buf->cmd_set_scissor(window_width, window_height);
        
        
        cmd_buf->cmd_bind_vertex_buffer(_bindless_data->_vb);
        cmd_buf->cmd_bind_index_buffer(_bindless_data->_ib, VK_INDEX_TYPE_UINT16);
        
        for (uint32_t pipeline_variant = 0; pipeline_variant < (uint32_t)PipelineVariant::All; ++pipeline_variant) {
            assert(_bindless_data->_pipeline_bins.find((PipelineVariant)pipeline_variant) != _bindless_data->_pipeline_bins.end());

            otcv::GraphicsPipeline* pipeline = _bindless_data->_pipeline_bins[(PipelineVariant)pipeline_variant];
            cmd_buf->cmd_bind_graphics_pipeline(pipeline);
        
            cmd_buf->cmd_bind_descriptor_set(pipeline, _frame_ctxs[frame_id].frame_desc_sets[RenderPassType::Geometry], DescriptorSetRate::PerFrame);
            cmd_buf->cmd_bind_descriptor_set(pipeline, _bindless_data->_bindless_object_desc_set, DescriptorSetRate::PerObject);
            cmd_buf->cmd_bind_descriptor_set(pipeline, _bindless_data->_bindless_material_desc_set, DescriptorSetRate::PerMaterial);
        
            uint32_t n_obj = _scene_refs.size();
            Std430AlignmentType::Range command_range = _culling_out.ssbo_commands->range_of(pipeline_variant * n_obj, SSBOAccess());
            Std430AlignmentType::Range count_range = _culling_out.ssbo_draw_count->range_of(pipeline_variant, SSBOAccess());
            cmd_buf->cmd_draw_indexed_indirect_count(
                _culling_out.ssbo_commands->_buf,
                command_range.offset,
                _culling_out.ssbo_draw_count->_buf, count_range.offset, n_obj, command_range.stride);
        }
        
        cmd_buf->cmd_end_rendering();

        cmd_buf->cmd_image_memory_barrier(_albedo_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::FragSample);
        cmd_buf->cmd_image_memory_barrier(_normals_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::FragSample);
        cmd_buf->cmd_image_memory_barrier(_metallic_roughness_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::FragSample);
        cmd_buf->cmd_image_memory_barrier(_depth_image, otcv::ResourceState::DepthStencilAttachment, otcv::ResourceState::FragSample);

        cmd_buf->cmd_buffer_memory_barrier(_culling_out.ssbo_commands->_buf, otcv::ResourceState::IndirectRead, otcv::ResourceState::ComputeSSBOWrite);
        cmd_buf->cmd_buffer_memory_barrier(_culling_out.ssbo_draw_count->_buf, otcv::ResourceState::IndirectRead, otcv::ResourceState::ComputeSSBOWrite);
    }

    void lighting_pass_commands(otcv::CommandBuffer* cmd_buf, uint32_t frame_id) {
        FrameContext& f_ctx = _frame_ctxs[frame_id];

        auto lighting = [&]() {
            assert(f_ctx.frame_desc_sets.find(RenderPassType::Lighting) != f_ctx.frame_desc_sets.end());
            // TODO: one lighting model might be shared across different materials.
            // Just use pbr model now

            cmd_buf->cmd_bind_graphics_pipeline(_lighting_pipeline);
            cmd_buf->cmd_bind_descriptor_set(_lighting_pipeline, f_ctx.frame_desc_sets[RenderPassType::Lighting], DescriptorSetRate::PerFrame);
            cmd_buf->cmd_bind_vertex_buffer(_screen_quad);
            vkCmdDraw(cmd_buf->vk_command_buffer, 3, 1, 0, 0);
        };

        otcv::RenderingBegin pass_begin;
        pass_begin
            .area(window_width, window_height)
            .color_attachment()
            .image_view(_lit_image->vk_view)
            .image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .clear_value(0.0f, 0.0f, 0.0f, 1.0f)
            .end();
        cmd_buf->cmd_begin_rendering(pass_begin);
        cmd_buf->cmd_set_viewport(window_width, window_height);
        cmd_buf->cmd_set_scissor(window_width, window_height);
        lighting();
        cmd_buf->cmd_end_rendering();

        cmd_buf->cmd_image_memory_barrier(_albedo_image, otcv::ResourceState::FragSample, otcv::ResourceState::ColorAttachment);
        cmd_buf->cmd_image_memory_barrier(_normals_image, otcv::ResourceState::FragSample, otcv::ResourceState::ColorAttachment);
        cmd_buf->cmd_image_memory_barrier(_metallic_roughness_image, otcv::ResourceState::FragSample, otcv::ResourceState::ColorAttachment);
        cmd_buf->cmd_image_memory_barrier(_depth_image, otcv::ResourceState::FragSample, otcv::ResourceState::DepthStencilAttachment);

        // for shadowmaps
        cmd_buf->cmd_image_memory_barrier(_cascaded_shadowmap, otcv::ResourceState::FragSample, otcv::ResourceState::DepthStencilAttachment);

        // get ready for postprocess
        cmd_buf->cmd_image_memory_barrier(_lit_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::FragSample);
    }

    void blit_commands(otcv::CommandBuffer* cmd_buf, uint32_t frame_id, uint32_t image_id) {
        FrameContext& f_ctx = _frame_ctxs[frame_id];

        cmd_buf->cmd_image_memory_barrier(_swapchain->mock_image(image_id),
            otcv::ResourceState::Present, otcv::ResourceState::TransferDst);

        otcv::ImageBlit region;
        region
            .src_upper_bound(window_width, window_height)
            .dst_upper_bound(window_width, window_height);

        // TODO: blit the final image to swapchain image.
        cmd_buf->cmd_image_blit(_back_buffer, _swapchain->mock_image(image_id), region);

        // insert a memory barrier at the end. Transition whatever images that graphics commands might draw on,
        // so that graphics commands for the next frame will wait behind the barrier and will not start prematurely
        cmd_buf->cmd_image_memory_barrier(_back_buffer, otcv::ResourceState::TransferSrc, otcv::ResourceState::ColorAttachment);
        // for gui overlay to draw on. Also serves as synchronization point for later gui overlay to wait on

        cmd_buf->cmd_image_memory_barrier(_swapchain->mock_image(image_id),
            otcv::ResourceState::TransferDst, otcv::ResourceState::Present); // TODO: imgui in-flight support. Change the final state to ColorAttachment
    }

    void draw_frame() {
        FrameContext& f_ctx = _frame_ctxs[_current_frame];
        f_ctx.graphics_fence->wait_reset();
        f_ctx.blit_fence->wait_reset();

        uint32_t image_index;
        vkAcquireNextImageKHR(_device, _swapchain->vk_swapchain, UINT64_MAX, f_ctx.image_available_semaphore->vk_semaphore, VK_NULL_HANDLE, &image_index);

        update_frame_ubos(_current_frame);
        f_ctx.graphics_command_buffers[RenderPassType::Shadow]->reset();
        f_ctx.graphics_command_buffers[RenderPassType::Shadow]->record(std::bind(&ShadowManager::commands, _shadow_manager.get(), std::placeholders::_1, _current_frame));
        f_ctx.graphics_command_buffers[RenderPassType::Geometry]->reset();
        f_ctx.graphics_command_buffers[RenderPassType::Geometry]->record(std::bind(&Application::g_pass_commands, this, std::placeholders::_1, _current_frame));
        f_ctx.graphics_command_buffers[RenderPassType::Lighting]->reset();
        f_ctx.graphics_command_buffers[RenderPassType::Lighting]->record(std::bind(&Application::lighting_pass_commands, this, std::placeholders::_1, _current_frame));
        f_ctx.graphics_command_buffers[RenderPassType::PostProcess]->reset();
        f_ctx.graphics_command_buffers[RenderPassType::PostProcess]->record(std::bind(&PostProcessManager::commands, _postprocess_manager.get(), std::placeholders::_1));
        {
            otcv::QueueSubmit graphics_submit;
            graphics_submit
                .batch()
                    .add_command_buffer(f_ctx.graphics_command_buffers[RenderPassType::Shadow])
                    .add_command_buffer(f_ctx.graphics_command_buffers[RenderPassType::Geometry])
                    .add_command_buffer(f_ctx.graphics_command_buffers[RenderPassType::Lighting])
                    .add_command_buffer(f_ctx.graphics_command_buffers[RenderPassType::PostProcess])
                .end();
            graphics_submit.signal(f_ctx.graphics_fence);
            _vulkan_context.queue->submit(graphics_submit);
        }


        f_ctx.blit_command_buffer->reset();
        f_ctx.blit_command_buffer->record(std::bind(&Application::blit_commands, this, std::placeholders::_1, _current_frame, image_index));
        {
            otcv::QueueSubmit blit_submit;
            blit_submit
                .batch()
                    .add_command_buffer(f_ctx.blit_command_buffer)
                    .add_wait(f_ctx.image_available_semaphore, VK_PIPELINE_STAGE_TRANSFER_BIT)
                .end()
                .signal(f_ctx.blit_fence);
            _vulkan_context.queue->submit(blit_submit);
        }

        // ImGui_ImplOTCV_SynchronizationInfo sync_info;
        // sync_info.target_post_render_state = otcv::ResourceState::Present;
        // sync_info.signal_fence = { f_ctx.composition_fence };
        // TODO: imgui in-flight support
        // ImGui_ImplOTCV_RenderDrawData(_swapchain->mock_image(image_index), &sync_info);

        otcv::QueuePresent present;
        present.image_index(image_index);
        _vulkan_context.queue->present(present);

        _current_frame = (_current_frame + 1) % _frame_ctxs.size();
    }



    void update_frame_ubos(uint32_t frame_id) {
        // g-pass
        {
            glm::mat4 proj = cam.update_proj();
            glm::mat4 view = cam.update_view();
            _culling->update(proj, view, frame_id);

            glm::mat4 proj_view = proj * view;
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Geometry]->set(StaticUBOAccess()["projectView"], &proj_view);
        }

        glm::vec3 light_direction(2.0f, -7.0f, 1.0f);
        // shadow pass
        std::vector<CSM::CascadeContext> cascade_ctxs;
        {
            // TODO: fixed light direction for the moment
            cascade_ctxs = std::move(_shadow_manager->update(light_direction, cam, frame_id, cascade_blend_depth));
        }

        // lighting pass
        {
            glm::mat4 proj = cam.update_proj();
            glm::mat4 view = cam.update_view();
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["projectInv"], &glm::inverse(proj));
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["viewInv"], &glm::inverse(view));

            glm::vec3 color(1.0f);
            float intensity = 2.0f;
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["light"]["intensity"], &intensity);
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["light"]["color"], &color);
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["light"]["direction"], &light_direction);

            // these are not correct
            glm::vec2 n_jitter_tiles((float)window_width / jitter_tile_size, (float)window_height / jitter_tile_size);
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["nJitterTiles"], &n_jitter_tiles);
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["nJitterStrataPerDim"], &jitter_strata_per_dim);
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["jitterRadius"], &jitter_radius);
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["cascadeBlendDepth"], &cascade_blend_depth);
            uint32_t n_cascades = cascade_ctxs.size();
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["nCascades"], &n_cascades);
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["cascadeResolution"], &cascaded_shadowmap_size);
            for (uint32_t i = 0; i < cascade_ctxs.size(); ++i) {
                _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["cascades"][i]["zBegin"], &cascade_ctxs[i].z_begin);
                _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["cascades"][i]["zEnd"], &cascade_ctxs[i].z_end);
                _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["cascades"][i]["lightSpaceView"], &cascade_ctxs[i].light_view);
                _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["shadow"]["cascades"][i]["lightSpaceProject"], &cascade_ctxs[i].light_proj);
            }
        }

    }

    void cleanup_scene() {
        _scene_graph.clear();
    }

    void cleanup_imgui() {

    }
    void main_loop() {
        while (!glfwWindowShouldClose(_window)) {
            glfwPollEvents();
            // immediate_gui();
            draw_frame();
        }

        vkDeviceWaitIdle(_device);
    }
    void cleanup() {

        otcv::destroy_context();
        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    // Boilerplate stuff
    GLFWwindow* _window = nullptr;
    otcv::Context _vulkan_context;
    VkInstance _instance = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physical_device = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    otcv::Swapchain* _swapchain;

    otcv::CommandPool* _command_pool;

    // cascaded shadow maps
    otcv::Image* _cascaded_shadowmap;
    otcv::Sampler* _cascaded_shadowsampler;

    // G-buffers
    otcv::Image* _albedo_image;
    otcv::Sampler* _albedo_sampler;
    otcv::Image* _normals_image;
    otcv::Sampler* _normals_sampler;
    otcv::Image* _metallic_roughness_image;
    otcv::Sampler* _metallic_roughness_sampler;
    otcv::Image* _depth_image;
    otcv::Sampler* _depth_sampler;

    // lit image
    otcv::Image* _lit_image;
    otcv::GraphicsPipeline* _lighting_pipeline;
    otcv::ShaderBlob _lighting_shader_blob;

    // final image
    otcv::Image* _back_buffer;

    // noise texture for shadowmaps
    otcv::Image* _noise_texture;
    otcv::Sampler* _noise_texture_sampler;

    // UBOs
    // per frame
    std::shared_ptr<NaiveExpandableDescriptorPool> _frame_desc_set_pool;

    struct FrameContext {
        // per-frame ubo
        std::map<RenderPassType, std::shared_ptr<StaticUBO>> frame_ubos;

        // per-frame descriptor sets
        std::map<RenderPassType, otcv::DescriptorSet*> frame_desc_sets;

        // synchronization
        otcv::Fence* graphics_fence;
        otcv::Fence* blit_fence;
        otcv::Semaphore* image_available_semaphore;

        // command buffers
        std::map<RenderPassType, otcv::CommandBuffer*> graphics_command_buffers;
        otcv::CommandBuffer* blit_command_buffer;
    };
    std::vector<FrameContext> _frame_ctxs;
    otcv::VertexBuffer* _screen_quad;
    size_t _current_frame = 0;

    std::shared_ptr<InputHandler> _input_handler;
    Arcball _arcball;

    SceneGraph _scene_graph;
    SceneGraphFlatRefs _scene_refs;
    MaterialResources _material_res;
    std::shared_ptr<BindlessDataManager> _bindless_data;

    std::shared_ptr<SceneCulling> _culling;
    SceneCulling::ObjectBufferContext _culling_in;
    SceneCulling::IndirectCommandContext _culling_out;

    std::shared_ptr<PostProcessManager> _postprocess_manager;
    std::shared_ptr<ShadowManager> _shadow_manager;
};

int main(int argc, char** argv)
{    
    Application app;
    app.run();
    app.cleanup();

    return 0;
}