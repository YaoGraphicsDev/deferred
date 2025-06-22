#include "shadow_manager.h"


ShadowManager::ShadowManager(
	const std::string& shadow_shader_path,
	const std::string& culling_shader_path,
	otcv::Image* shadowmap,
	const SceneGraph& scene,
	const SceneGraphFlatRefs& scene_refs,
	std::shared_ptr<BindlessDataManager> bindless_data,
	uint32_t in_flight_frames) {

	_shadowmap = shadowmap;

	std::map<uint32_t, uint32_t> vs_indexing_limits = {
		{otcv::pack(DescriptorSetRate::PerObject, 0), scene_refs.size()}
	};
	std::map<std::string, otcv::ShaderLoadHint> file_hints = {
		{"cascaded_shadow.vert", {otcv::ShaderLoadHint::Hint::DescriptorIndexing, &vs_indexing_limits}}
	};
	_shader_blob = std::move(otcv::load_shaders_from_dir(shadow_shader_path, file_hints));

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

	_frame_ctxs.resize(in_flight_frames);
	uint32_t n_cascades = shadowmap->builder._image_info.arrayLayers;
	for (FrameContext& frame : _frame_ctxs) {
		frame.resize(n_cascades); // number of cascades
		for (CascadeContext& cascade : frame) {
			cascade.desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::PerFrame]);

			Std140AlignmentType FrameUBO;
			FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "projectView");
			cascade.ubo.reset(new StaticUBO(FrameUBO));
			cascade.desc_set->bind_buffer(0, cascade.ubo->_buf);
			// cascade.cascade_splits = { 0.0f, 0.0f };
		}
	}

	_bindless_data = bindless_data;

	_scene_cullings.resize(n_cascades);
	_culling_out.resize(n_cascades);
	for (uint32_t i = 0; i < n_cascades; ++i) {
		_scene_cullings[i].reset(new SceneCulling(culling_shader_path, scene, scene_refs, in_flight_frames));
		_culling_out[i] = _scene_cullings[i]->create_indirect_command_context((uint32_t)PipelineVariant::All, _bindless_data);
	}
	_culling_in = _scene_cullings[0]->create_object_buffer_context(scene, scene_refs, _bindless_data);
	_n_obj = scene_refs.size();
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
	for (uint32_t cascade = 0; cascade < frame_ctx.size(); ++cascade) {
		// traverse and update cascades
		glm::mat4 light_pv = cascade_ctxs[cascade].light_proj * cascade_ctxs[cascade].light_view;
		frame_ctx[cascade].ubo->set(acc, &(light_pv));

		// update culling
		_scene_cullings[cascade]->update(glm::inverse(cascade_ctxs[cascade].light_proj), glm::inverse(cascade_ctxs[cascade].light_view), frame_id);
	}
	return cascade_ctxs;
}

void ShadowManager::commands(otcv::CommandBuffer* cmd_buf, uint32_t frame_id) {
	// TODO: ensure an _shadowmap image is already transitioned to ResourceState::DepthStencilAttachment state

	uint32_t width = _shadowmap->builder._image_info.extent.width;
	uint32_t height = _shadowmap->builder._image_info.extent.height;
	for (uint32_t cascade = 0; cascade < _shadowmap->builder._image_info.arrayLayers; ++cascade) {
		// cull frustum 
		_scene_cullings[cascade]->commands(cmd_buf, _culling_in, _culling_out[cascade], frame_id);

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

		cmd_buf->cmd_bind_descriptor_set(_pipeline, _frame_ctxs[frame_id][cascade].desc_set, DescriptorSetRate::PerFrame);
		cmd_buf->cmd_bind_descriptor_set(_pipeline, _bindless_data->_bindless_object_desc_set, DescriptorSetRate::PerObject);
		cmd_buf->cmd_bind_vertex_buffer(_bindless_data->_vb);
		cmd_buf->cmd_bind_index_buffer(_bindless_data->_ib, VK_INDEX_TYPE_UINT16);

		cmd_buf->cmd_bind_graphics_pipeline(_pipeline);

		// TODO: issuing a draw call for each pipeline variant is not really necessary 
		// as shadow pipeline do not differentiate materials
		// this can be solve by writing another version of frustum_cull.comp shader that puts all indirect commands in one place
		for (uint32_t pipeline_variant = 0; pipeline_variant < (uint32_t)PipelineVariant::All; ++pipeline_variant) {
			Std430AlignmentType::Range command_range = _culling_out[cascade].ssbo_commands->range_of(pipeline_variant * _n_obj, SSBOAccess());
			Std430AlignmentType::Range count_range = _culling_out[cascade].ssbo_draw_count->range_of(pipeline_variant, SSBOAccess());
			cmd_buf->cmd_draw_indexed_indirect_count(
				_culling_out[cascade].ssbo_commands->_buf,
				command_range.offset,
				_culling_out[cascade].ssbo_draw_count->_buf,
				count_range.offset,
				_n_obj,
				command_range.stride);
		}

		cmd_buf->cmd_end_rendering();

		cmd_buf->cmd_buffer_memory_barrier(_culling_out[cascade].ssbo_commands->_buf, otcv::ResourceState::IndirectRead, otcv::ResourceState::ComputeSSBOWrite);
		cmd_buf->cmd_buffer_memory_barrier(_culling_out[cascade].ssbo_draw_count->_buf, otcv::ResourceState::IndirectRead, otcv::ResourceState::ComputeSSBOWrite);
	}

	cmd_buf->cmd_image_memory_barrier(_shadowmap, otcv::ResourceState::DepthStencilAttachment, otcv::ResourceState::FragSample);
}