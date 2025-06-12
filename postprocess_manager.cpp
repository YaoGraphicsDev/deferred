#include "postprocess_manager.h"
#include "render_global_types.h"

PostProcessManager::PostProcessManager(const std::string& shader_path, otcv::Image* in_image, otcv::Image* out_image) {
    _in_image = in_image;
    _out_image = out_image;
    _in_sampler = otcv::SamplerBuilder().build();
	_shader_blob = std::move(otcv::load_shaders_from_dir(shader_path));
	_screen_quad = otcv::screen_quad_ndc();

	otcv::GraphicsPipelineBuilder pipeline_builder;
    pipeline_builder.pipline_rendering()
        .add_color_attachment_format(_out_image->builder._image_info.format)
        .end()
        .shader_vertex(_shader_blob["screen_quad.vert"])
        .shader_fragment(_shader_blob["tone_mapping.frag"])
		.vertex_state(_screen_quad->builder)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
    _pipeline = pipeline_builder.build();

    _desc_pool.reset(new NaiveExpandableDescriptorPool());

    _desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::PerFrame]);
    _desc_set->bind_image_sampler(0, &_in_image, &_in_sampler);
}

PostProcessManager::~PostProcessManager() {

}

void PostProcessManager::commands(otcv::CommandBuffer* cmd_buf) {
	uint32_t width = _in_image->builder._image_info.extent.width;
	uint32_t height = _in_image->builder._image_info.extent.height;
	cmd_buf->cmd_set_viewport(width, height);
	cmd_buf->cmd_set_scissor(width, height);

    otcv::RenderingBegin pass_begin;
    pass_begin
        .area(width, height)
        .color_attachment()
        .image_view(_out_image->vk_view)
        .image_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        .load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
        .clear_value(0.0f, 0.0f, 0.0f, 1.0f)
        .end();
    cmd_buf->cmd_begin_rendering(pass_begin);

    cmd_buf->cmd_bind_graphics_pipeline(_pipeline);
    cmd_buf->cmd_bind_vertex_buffer(_screen_quad);
    cmd_buf->cmd_bind_descriptor_set(_pipeline, _desc_set);
    vkCmdDraw(cmd_buf->vk_command_buffer, 3, 1, 0, 0);

    cmd_buf->cmd_end_rendering();

	cmd_buf->cmd_image_memory_barrier(_in_image, otcv::ResourceState::FragSample, otcv::ResourceState::ColorAttachment);
    cmd_buf->cmd_image_memory_barrier(_out_image, otcv::ResourceState::ColorAttachment, otcv::ResourceState::TransferSrc);
}

