#include "otcv.h"
#include "otcv_utils.h"

#include "input_handler.h"
#include "arcball.h"

#include "gltf_parser.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_otcv.h"

#include "camera.h"
#include "static_ubo.h"

#include <iostream>
#include <array>

const int window_width = 1920;
const int window_height = 960;

/*    glm::vec3 eye,
    glm::vec3 center,
    glm::vec3 up,
    float near,
    float far,
    float fov,
    float aspect*/
PerspectiveCamera cam(
    glm::vec3(20.0f, 20.0f, 20.0f),
    glm::vec3(0.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    0.1f,
    100.0f,
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
        init_render_targets();
        init_frame_contexts();
        connect_render_targets();
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
            Std140AlignmentType Light;
            Light.add(Std140AlignmentType::InlineType::Float, "intensity");
            Light.add(Std140AlignmentType::InlineType::Vec3, "color");
            Light.add(Std140AlignmentType::InlineType::Vec3, "direction");
            Std140AlignmentType FrameUBO;
            FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "projectInv");
            FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "viewInv");
            FrameUBO.add(Light, "lights", 16);
            return std::make_shared<StaticUBO>(FrameUBO);
        }

        assert(false);
        return nullptr;
    }

    otcv::DescriptorSet* init_frame_desc_sets(RenderPassType pass) {
        otcv::DescriptorSetLayout* per_frame_set_layout = _material_manager->per_frame_desc_set_layout(pass);
        if (!per_frame_set_layout) {
            return nullptr;
        }
        
        return _frame_desc_set_pool->allocate(per_frame_set_layout);
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
            .format(VK_FORMAT_R8G8B8A8_UNORM)
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

        _lit_image = otcv::ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R8G8B8A8_UNORM) // TODO: should support HDR
            .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build();
        _lit_image->initialize_state(otcv::ResourceState::ColorAttachment);

        _mock_depth_image = otcv::ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_D24_UNORM_S8_UINT)
            .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            .aspect(VK_IMAGE_ASPECT_DEPTH_BIT)
            .build();
        _depth_image->initialize_state(otcv::ResourceState::DepthStencilAttachment);
    }
    void init_frame_contexts() {
        _command_pool = otcv::CommandPool::create(false, true);
        _frame_desc_set_pool = std::make_shared<TrivialExpandableDescriptorPool>();

        _frame_ctxs.resize(_swapchain->mock_images.size());
        for (FrameContext& ctx : _frame_ctxs) {
            // per-frame ubo
            ctx.frame_ubos[RenderPassType::Geometry] = init_frame_ubo(RenderPassType::Geometry);
            ctx.frame_ubos[RenderPassType::Lighting] = init_frame_ubo(RenderPassType::Lighting);

            // per-frame descriptor sets 
            ctx.frame_desc_sets[RenderPassType::Geometry] = init_frame_desc_sets(RenderPassType::Geometry);
            ctx.frame_desc_sets[RenderPassType::Geometry]->bind_buffer(0, ctx.frame_ubos[RenderPassType::Geometry]->_buf);
            ctx.frame_desc_sets[RenderPassType::Lighting] = init_frame_desc_sets(RenderPassType::Lighting);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_buffer(0, ctx.frame_ubos[RenderPassType::Lighting]->_buf);

            // sync objects
            ctx.graphics_fence = otcv::Fence::create();
            ctx.blit_fence = otcv::Fence::create();
            ctx.image_available_semaphore = otcv::Semaphore::create();

            // command buffers
            ctx.graphics_command_buffer = _command_pool->allocate();
            ctx.blit_command_buffer = _command_pool->allocate();
        }
        _screen_quad = otcv::screen_quad_ndc();
    }

    void connect_render_targets() {
        for (FrameContext& ctx : _frame_ctxs) {
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(2, &_albedo_image, &_albedo_sampler);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(3, &_normals_image, &_normals_sampler);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(4, &_metallic_roughness_image, &_metallic_roughness_sampler);
            ctx.frame_desc_sets[RenderPassType::Lighting]->bind_image_sampler(1, &_depth_image, &_depth_sampler);
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
        std::map<RenderPassType, std::string> pass_shader_map = 
        { {RenderPassType::Geometry, "./spirv/geometry_pass/"},
            {RenderPassType::Lighting, "./spirv/lighting_pass/"}};
        _material_manager = std::make_shared<MaterialManager>(_physical_device, pass_shader_map);
        _object_ubo_manager = std::make_shared<DynamicUBOManager>(_physical_device);

        bool ret = load_gltf("C:/Users/Liyao/Sources/Sponza/glTF/Sponza.gltf", _scene_graph,
            _material_manager, _object_ubo_manager);

        _g_pass_bind_order = std::move(sort_draw_bind_order(_scene_graph, _material_manager, _object_ubo_manager, RenderPassType::Geometry));

        return ret;
    }


    void graphics_commands(otcv::CommandBuffer* cmd_buf, uint32_t frame_id) {
        FrameContext& f_ctx = _frame_ctxs[frame_id];



        auto bind_dynamic_ubo = [&](
            otcv::GraphicsPipeline* pipeline,
            DynamicUBOManager& ubo_manager,
            DescriptorSetInfoHandle& handle,
            DescriptorSetRate set_rate) {

            DynamicUBOManager::DescriptorSetInfo desc_set_info =
                std::move(ubo_manager.get_descriptor_set_info(handle));
            otcv::DescriptorSet* desc_set = ubo_manager.desc_set_cache[desc_set_info.key];
            // bind dynamic UBO with offset
            std::vector<uint32_t> dynamic_offsets;
            for (auto& acc : desc_set_info.ubo_accesses) {
                dynamic_offsets.push_back(acc.offset);
            }
            cmd_buf->cmd_bind_descriptor_set(pipeline, desc_set, set_rate, dynamic_offsets);
        };

        auto draw_scene = [&](RenderPassType pass_type) {
            if (_g_pass_bind_order.empty()) {
                return;
            }
            assert(f_ctx.frame_desc_sets.find(pass_type) != f_ctx.frame_desc_sets.end());
            for (const PipelineBatch& pb : _g_pass_bind_order) {
                otcv::GraphicsPipeline* pipeline = _material_manager->pipeline_cache->get(pb.pipeline);
                // bind per frame descriptor set
                cmd_buf->cmd_bind_descriptor_set(pipeline, f_ctx.frame_desc_sets[pass_type], DescriptorSetRate::PerFrameUBO);

                // bind pipeline
                cmd_buf->cmd_bind_graphics_pipeline(pipeline);
                for (const MaterialBatch& mb : pb.material_batches) {
                    MaterialManager::PipelineState p_state =
                        std::move(_material_manager->get_pipeline_state(mb.material, pass_type));

                    bind_dynamic_ubo(
                        pipeline,
                        *_material_manager->ubo_manager,
                        p_state.material_ubo_set_handle,
                        DescriptorSetRate::PerMaterialUBO);

                    // textures
                    cmd_buf->cmd_bind_descriptor_set(pipeline, p_state.texture_desc_set, DescriptorSetRate::PerMaterialTexture);

                    for (const RenderableId& r : mb.renderables) {
                        // geometries
                        SceneNode& node = _scene_graph[r.scene_node_id];
                        Renderable& renderable = node.renderables[r.renderable_id];

                        bind_dynamic_ubo(
                            pipeline,
                            *_object_ubo_manager,
                            node.object_ubo_set_handle,
                            DescriptorSetRate::PerObjectUBO);

                        cmd_buf->cmd_bind_vertex_buffer(renderable.mesh->vb);
                        cmd_buf->cmd_bind_index_buffer(renderable.mesh->ib, VK_INDEX_TYPE_UINT16);
                        vkCmdDrawIndexed(cmd_buf->vk_command_buffer, renderable.mesh->ib->builder._info.size / sizeof(uint16_t), 1, 0, 0, 0);
                    }
                }
            }
        };

        auto lighting = [&]() {
            assert(f_ctx.frame_desc_sets.find(RenderPassType::Lighting) != f_ctx.frame_desc_sets.end());
            // TODO: one lighting model might be shared across different materials.
            // Just use pbr model now
            
            // Temp: Grab the first material that uses pbr model
            otcv::GraphicsPipeline* pbr_pipeline = nullptr;
            for (auto& node : _scene_graph) {
                for (auto& r : node.renderables) {
                    MaterialManager::Material mat = std::move(_material_manager->get_material(r.material->material_handle));
                    if (mat.lighting.model == LightingModel::Model::PBR) {
                        PipelineHandle p_hdl = mat.pass_action[RenderPassType::Lighting].pipeline_handle;
                        pbr_pipeline = _material_manager->pipeline_cache->get(p_hdl);
                        break;
                    }
                }
                
            }
            assert(pbr_pipeline);

            cmd_buf->cmd_bind_graphics_pipeline(pbr_pipeline);
            cmd_buf->cmd_bind_descriptor_set(pbr_pipeline, f_ctx.frame_desc_sets[RenderPassType::Lighting], DescriptorSetRate::PerFrameUBO);
            cmd_buf->cmd_bind_vertex_buffer(_screen_quad);
            vkCmdDraw(cmd_buf->vk_command_buffer, 3, 1, 0, 0);
        };

        // Geometry pass
        {
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
            // TODO: cover this with otcv::CommandBuffer functions
            VkViewport viewport{ 0.0f, 0.0f, window_width, window_height, 0.0f, 1.0f };
            vkCmdSetViewport(cmd_buf->vk_command_buffer, 0, 1, &viewport);
            VkRect2D scissor{ {0, 0}, {window_width, window_height} };
            vkCmdSetScissor(cmd_buf->vk_command_buffer, 0, 1, &scissor);
            draw_scene(RenderPassType::Geometry);
            cmd_buf->cmd_end_rendering();

            cmd_buf->cmd_image_memory_barrier(_albedo_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::FragSample);
            cmd_buf->cmd_image_memory_barrier(_normals_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::FragSample);
            cmd_buf->cmd_image_memory_barrier(_metallic_roughness_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::FragSample);
            cmd_buf->cmd_image_memory_barrier(_depth_image, otcv::ResourceState::DepthStencilAttachment, otcv::ResourceState::FragSample);
        }

        // lighting pass 
        {
            // TODO: cover this with otcv::CommandBuffer functions
            VkViewport viewport{ 0.0f, 0.0f, window_width, window_height, 0.0f, 1.0f };
            vkCmdSetViewport(cmd_buf->vk_command_buffer, 0, 1, &viewport);
            VkRect2D scissor{ {0, 0}, {window_width, window_height} };
            vkCmdSetScissor(cmd_buf->vk_command_buffer, 0, 1, &scissor);

            otcv::RenderingBegin pass_begin;
            pass_begin
                .area(window_width, window_height)
                .color_attachment()
                .image_view(_lit_image->vk_view)
                .image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
                .clear_value(0.0f, 0.0f, 0.0f, 1.0f)
                .end()
                .depth_stencil_attachment()
                .image_view(_mock_depth_image->vk_view)
                .image_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
                .load_store(VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE)
                .clear_value(1.0f, 0)
                .end();
              cmd_buf->cmd_begin_rendering(pass_begin);
            lighting();
            cmd_buf->cmd_end_rendering();

            cmd_buf->cmd_image_memory_barrier(_albedo_image, otcv::ResourceState::FragSample, otcv::ResourceState::ColorAttachment);
            cmd_buf->cmd_image_memory_barrier(_normals_image, otcv::ResourceState::FragSample, otcv::ResourceState::ColorAttachment);
            cmd_buf->cmd_image_memory_barrier(_metallic_roughness_image, otcv::ResourceState::FragSample, otcv::ResourceState::ColorAttachment);
            cmd_buf->cmd_image_memory_barrier(_depth_image, otcv::ResourceState::FragSample, otcv::ResourceState::DepthStencilAttachment);
        }


        // insert a memory barrier at the end, so that blit commands will not start prematurely
        cmd_buf->cmd_image_memory_barrier(_lit_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::TransferSrc);
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
        cmd_buf->cmd_image_blit(_lit_image, _swapchain->mock_image(image_id), region);

        // insert a memory barrier at the end. Transition whatever images that graphics commands might draw on,
        // so that graphics commands for the next frame will wait behind the barrier and will not start prematurely
        cmd_buf->cmd_image_memory_barrier(_lit_image, otcv::ResourceState::TransferSrc, otcv::ResourceState::ColorAttachment);
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
        f_ctx.graphics_command_buffer->reset();
        f_ctx.graphics_command_buffer->record(std::bind(&Application::graphics_commands, this, std::placeholders::_1, _current_frame));
        {
            otcv::QueueSubmit graphics_submit;
            graphics_submit
                .batch()
                    .add_command_buffer(f_ctx.graphics_command_buffer)
                .end()
                .signal(f_ctx.graphics_fence);
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
        // update camera for g-pass
        {
            glm::mat4 proj = cam.update_proj();
            glm::mat4 view = cam.update_view();
            glm::mat4 proj_view = proj * view;
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Geometry]->set(StaticUBOAccess()["projectView"], &proj_view);
        }

        // TODO: update lighting pass ubo
        {
            glm::mat4 proj = cam.update_proj();
            glm::mat4 view = cam.update_view();
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["projectInv"], &glm::inverse(proj));
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["viewInv"], &glm::inverse(view));
            glm::vec3 color(1.2, 3.4, 5.6);
            _frame_ctxs[frame_id].frame_ubos[RenderPassType::Lighting]->set(StaticUBOAccess()["lights"][15]["color"], &color);
        }
    }

    void cleanup_scene() {
        _scene_graph.clear();
        _material_manager = nullptr;
        _object_ubo_manager = nullptr;
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

    // Boiler plate stuff
    GLFWwindow* _window = nullptr;
    otcv::Context _vulkan_context;
    VkInstance _instance = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physical_device = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    otcv::Swapchain* _swapchain;

    otcv::CommandPool* _command_pool;

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
    // mock depth image
    otcv::Image* _mock_depth_image;

    // UBOs
    // per object
    std::shared_ptr<DynamicUBOManager> _object_ubo_manager;
    // per frame
    // std::shared_ptr<SingleTypeExpandableDescriptorPool> _frame_ubo_desc_pool;
    std::shared_ptr<TrivialExpandableDescriptorPool> _frame_desc_set_pool;

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
        otcv::CommandBuffer* graphics_command_buffer;
        otcv::CommandBuffer* blit_command_buffer;
    };
    std::vector<FrameContext> _frame_ctxs;
    otcv::VertexBuffer* _screen_quad;
    size_t _current_frame = 0;

    std::shared_ptr<InputHandler> _input_handler;
    Arcball _arcball;

    std::shared_ptr<MaterialManager> _material_manager;
    SceneGraph _scene_graph;
    BindOrder _g_pass_bind_order;
};

int main(int argc, char** argv)
{
    Application app;
    app.run();
    app.cleanup();

    return 0;
}