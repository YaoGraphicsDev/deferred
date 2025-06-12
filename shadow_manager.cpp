#include "shadow_manager.h"


ShadowManager::ShadowManager(
	const std::string& shader_path,
	otcv::Image* shadowmap,
	std::shared_ptr<DynamicUBOManager> object_ubo_manager,
	uint32_t _in_flight_frames) {
	_shadowmap = shadowmap;

	otcv::ShaderLoadHint hint;
	std::set<uint16_t> dynamic_sets = { DescriptorSetRate::PerObjectUBO };
	hint.vertex_hint = otcv::ShaderLoadHint::Hint::DynamicUBO;
	hint.vertex_custom = &dynamic_sets;
	_shader_blob = std::move(otcv::load_shaders_from_dir(shader_path, hint));

	otcv::GraphicsPipelineBuilder pipeline_builder;
	pipeline_builder.pipline_rendering()
		.depth_stencil_attachment_format(shadowmap->builder._image_info.format)
		.end()
		.shader_vertex(_shader_blob["cascaded_shadow.vert"])
		.cull_back_face(VK_FRONT_FACE_CLOCKWISE)
		.depth_test();
	{
		otcv::VertexBufferBuilder vbb;
		vbb.add_binding().add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
		pipeline_builder.vertex_state(vbb); // bind position attribute only
	}
	pipeline_builder
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
	_pipeline = pipeline_builder.build();

	_desc_pool.reset(new NaiveExpandableDescriptorPool());

	_frame_ctxs.resize(_in_flight_frames);
	for (FrameContext& frame : _frame_ctxs) {
		frame.resize(shadowmap->builder._image_info.arrayLayers);
		for (CascadeContext& cascade : frame) {
			cascade.desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::PerFrame]);

			Std140AlignmentType FrameUBO;
			FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "projectView");
			cascade.ubo.reset(new StaticUBO(FrameUBO));
			cascade.desc_set->bind_buffer(0, cascade.ubo->_buf);
			// cascade.cascade_splits = { 0.0f, 0.0f };
		}
	}

	// per object UBO is managed by dynamic ubo manager
	_object_ubo_manager = object_ubo_manager;
}

ShadowManager::~ShadowManager() {

}

std::vector<CSM::CascadeContext> ShadowManager::update(glm::vec3 light_dir, PerspectiveCamera& camera, uint32_t frame_id, float blend_overlap) {
	std::vector<CSM::CascadeContext> cascade_ctxs = CSM::csm_ortho_projections(
		camera,
		light_dir,
		_shadowmap->builder._image_info.arrayLayers,
		_shadowmap->builder._image_info.extent.width,
		blend_overlap);
	assert(cascade_ctxs.size() == _shadowmap->builder._image_info.arrayLayers);

	StaticUBOAccess acc;
	acc["projectView"];
	FrameContext& frame_ctx = _frame_ctxs[frame_id];
	for (uint32_t i = 0; i < frame_ctx.size(); ++i) {
		glm::mat4 light_pv = cascade_ctxs[i].light_proj * cascade_ctxs[i].light_view;
		frame_ctx[i].ubo->set(acc, &(light_pv));
	}
	return cascade_ctxs;
}

void ShadowManager::commands(otcv::CommandBuffer* cmd_buf, SceneGraph& scene, uint32_t frame_id) {
	// TODO: ensure an _shadowmap image is already transitioned to ResourceState::DepthStencilAttachment state

	uint32_t width = _shadowmap->builder._image_info.extent.width;
	uint32_t height = _shadowmap->builder._image_info.extent.height;
	for (uint32_t cascade = 0; cascade < _shadowmap->builder._image_info.arrayLayers; ++cascade) {
		otcv::RenderingBegin pass_begin;
		pass_begin
			.area(width, height)
			.depth_stencil_attachment()
			.image_view(_shadowmap->view_of_layers(cascade, 1))
			.image_layout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
			.clear_value(1.0f, 0)
			.end();
		cmd_buf->cmd_begin_rendering(pass_begin);

		cmd_buf->cmd_set_viewport(width, height);
		cmd_buf->cmd_set_scissor(width, height);

		// TODO: perhaps cram it in with materials? Like what I'm going to do with lighting models?
		cmd_buf->cmd_bind_graphics_pipeline(_pipeline);

		cmd_buf->cmd_bind_descriptor_set(_pipeline, _frame_ctxs[frame_id][cascade].desc_set, DescriptorSetRate::PerFrame);

		for (SceneNode& node : scene) {
			DescriptorSetInfoHandle set_handle = node.object_ubo_set_handle;
			for (Renderable& r : node.renderables) {
				DynamicUBOManager::DescriptorSetInfo desc_set_info =
					std::move(_object_ubo_manager->get_descriptor_set_info(set_handle));
				otcv::DescriptorSet* desc_set = _object_ubo_manager->desc_set_cache[desc_set_info.key];
				// bind dynamic UBO with offset
				std::vector<uint32_t> dynamic_offsets;
				for (auto& acc : desc_set_info.ubo_accesses) {
					dynamic_offsets.push_back(acc.offset);
				}
				cmd_buf->cmd_bind_descriptor_set(_pipeline, desc_set, DescriptorSetRate::PerObjectUBO, dynamic_offsets);

				cmd_buf->cmd_bind_vertex_buffer(r.mesh->vb);
				cmd_buf->cmd_bind_index_buffer(r.mesh->ib, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(cmd_buf->vk_command_buffer, r.mesh->ib->builder._info.size / sizeof(uint16_t), 1, 0, 0, 0);
			}
		}

		cmd_buf->cmd_end_rendering();
	}

	cmd_buf->cmd_image_memory_barrier(_shadowmap, otcv::ResourceState::DepthStencilAttachment, otcv::ResourceState::FragSample);
}