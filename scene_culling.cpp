#include "math_common.h"
#include "scene_culling.h"

SceneCulling::SceneCulling(
	const std::string& shader_path,
	const SceneGraph& scene,
	const SceneGraphFlatRefs& scene_refs,
	uint32_t _in_flight_frames) {

	_shader_blob = otcv::load_shaders_from_dir(shader_path);
	_pipeline = otcv::ComputePipeline::create(_shader_blob["frustum_cull.comp"]);
	_desc_pool.reset(new NaiveExpandableDescriptorPool);
	_n_obj = scene_refs.size();

	_frame_ctxs.resize(_in_flight_frames);
	for (FrameContext& ctx : _frame_ctxs) {
		Std140AlignmentType UBO;
		UBO.add(Std140AlignmentType::InlineType::Vec4, "frustum_faces", 6);
		ctx._ubo.reset(new StaticUBO(UBO));
		ctx._desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::PerFrame]);
		ctx._desc_set->bind_buffer(0, ctx._ubo->_buf);
	}
}

SceneCulling::~SceneCulling() {

}

SceneCulling::ObjectBufferContext SceneCulling::create_object_buffer_context(
	const SceneGraph& scene,
	const SceneGraphFlatRefs& scene_refs,
	std::shared_ptr<BindlessDataManager> bindless_data) {

	ObjectBufferContext obj_buf_ctx;

	obj_buf_ctx.ssbo_aabbs = bindless_data->_mesh_preprocessor->AABB_SSBO();

	Std430AlignmentType ObjectData;
	ObjectData.add(Std430AlignmentType::InlineType::Mat4, "model");
	ObjectData.add(Std430AlignmentType::InlineType::Uint, "indexCount");
	ObjectData.add(Std430AlignmentType::InlineType::Uint, "firstIndex");
	ObjectData.add(Std430AlignmentType::InlineType::Int, "vertexOffset");
	ObjectData.add(Std430AlignmentType::InlineType::Uint, "pipelineVariant");
	obj_buf_ctx.ssbo_objects.reset(new SSBO(ObjectData, _n_obj));

	std::vector<SSBO::WriteContext> ssbo_writes(_n_obj);
	for (uint32_t i = 0; i < _n_obj; ++i) {
		const SceneNode& scene_node = scene[scene_refs[i].node_id];
		std::shared_ptr<MeshData> mesh = scene_node.renderables[scene_refs[i].renderable_id].mesh;
		ssbo_writes[i].id = i;
		ssbo_writes[i].access_ctxs.push_back({ SSBOAccess()["model"], &scene_node.world_transform });
		BindlessDataManager::ObjectDataSegment& segment = bindless_data->_object_data_segment[i];
		ssbo_writes[i].access_ctxs.push_back({ SSBOAccess()["indexCount"], &segment.index_count });
		ssbo_writes[i].access_ctxs.push_back({ SSBOAccess()["firstIndex"], &segment.index_start });
		ssbo_writes[i].access_ctxs.push_back({ SSBOAccess()["vertexOffset"], &segment.vertex_start });
		ssbo_writes[i].access_ctxs.push_back({ SSBOAccess()["pipelineVariant"], &scene_refs[i].pipeline_variant });
	}
	obj_buf_ctx.ssbo_objects->write(ssbo_writes);

	obj_buf_ctx.desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::ComputeRead]);
	obj_buf_ctx.desc_set->bind_buffer(0, obj_buf_ctx.ssbo_objects->_buf);
	obj_buf_ctx.desc_set->bind_buffer(1, obj_buf_ctx.ssbo_aabbs->_buf);

	return obj_buf_ctx;
}

SceneCulling::IndirectCommandContext SceneCulling::create_indirect_command_context(
	uint32_t n_pipeline_variants,
	std::shared_ptr<BindlessDataManager> bindless_data) {

	IndirectCommandContext indirect_cmd_ctx;
	
	// draw command buffer does not need to be initialized
	Std430AlignmentType DrawCommand;
	DrawCommand.add(Std430AlignmentType::InlineType::Uint, "indexCount");
	DrawCommand.add(Std430AlignmentType::InlineType::Uint, "instanceCount");
	DrawCommand.add(Std430AlignmentType::InlineType::Uint, "firstIndex");
	DrawCommand.add(Std430AlignmentType::InlineType::Int, "vertexOffset");
	DrawCommand.add(Std430AlignmentType::InlineType::Uint, "firstInstance");
	indirect_cmd_ctx.ssbo_commands.reset(new SSBO(DrawCommand, _n_obj * n_pipeline_variants, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));

	Std430AlignmentType DrawCount;
	DrawCount.add(Std430AlignmentType::InlineType::Uint, "value");
	indirect_cmd_ctx.ssbo_draw_count.reset(new SSBO(DrawCount, n_pipeline_variants, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));

	// initialize draw count buffer with 0. Also need to zero out every frame
	std::vector<SSBO::WriteContext> draw_count_writes(n_pipeline_variants);
	uint32_t zero_count = 0;
	for (uint32_t i = 0; i < n_pipeline_variants; ++i) {
		draw_count_writes[i].id = i;
		draw_count_writes[i].access_ctxs.push_back({ SSBOAccess()["value"], &zero_count });
	}
	indirect_cmd_ctx.ssbo_draw_count->write(draw_count_writes);
	
	indirect_cmd_ctx.desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::ComputeWrite]);
	indirect_cmd_ctx.desc_set->bind_buffer(0, indirect_cmd_ctx.ssbo_commands->_buf);
	indirect_cmd_ctx.desc_set->bind_buffer(1, indirect_cmd_ctx.ssbo_draw_count->_buf);

	return indirect_cmd_ctx;
}

void SceneCulling::update(const glm::mat4& proj, const glm::mat4& view, uint32_t frame_id) {
	// update frame ubo
	// TODO: update frustum planes

	FrustumUtils::Frustum f = FrustumUtils::view_frustum_vertices(glm::inverse(proj), glm::inverse(view));
	glm::vec4 left = FrustumUtils::plane(f[0], f[4], f[5]);
	glm::vec4 right = FrustumUtils::plane(f[2], f[6], f[7]);
	glm::vec4 top = FrustumUtils::plane(f[3], f[7], f[4]);
	glm::vec4 bottom = FrustumUtils::plane(f[1], f[5], f[6]);
	glm::vec4 far = FrustumUtils::plane(f[5], f[4], f[7]);
	glm::vec4 near = FrustumUtils::plane(f[0], f[1], f[2]);

	_frame_ctxs[frame_id]._ubo->set(StaticUBOAccess()["frustum_faces"][0], &left);
	_frame_ctxs[frame_id]._ubo->set(StaticUBOAccess()["frustum_faces"][1], &right);
	_frame_ctxs[frame_id]._ubo->set(StaticUBOAccess()["frustum_faces"][2], &top);
	_frame_ctxs[frame_id]._ubo->set(StaticUBOAccess()["frustum_faces"][3], &bottom);
	_frame_ctxs[frame_id]._ubo->set(StaticUBOAccess()["frustum_faces"][4], &far);
	_frame_ctxs[frame_id]._ubo->set(StaticUBOAccess()["frustum_faces"][5], &near);
}

void SceneCulling::commands(
	otcv::CommandBuffer* cmd_buf,
	ObjectBufferContext in_context,
	IndirectCommandContext out_context,
	uint32_t frame_id) {

	cmd_buf->cmd_buffer_memory_barrier(out_context.ssbo_draw_count->_buf, otcv::ResourceState::ComputeSSBOWrite, otcv::ResourceState::TransferDst);
	cmd_buf->cmd_fill_buffer(out_context.ssbo_draw_count->_buf, 0);
	cmd_buf->cmd_buffer_memory_barrier(out_context.ssbo_draw_count->_buf, otcv::ResourceState::TransferDst, otcv::ResourceState::ComputeSSBOWrite);
	
	cmd_buf->cmd_buffer_memory_barrier(out_context.ssbo_commands->_buf, otcv::ResourceState::ComputeSSBOWrite, otcv::ResourceState::TransferDst);
	cmd_buf->cmd_fill_buffer(out_context.ssbo_commands->_buf, 0);
	cmd_buf->cmd_buffer_memory_barrier(out_context.ssbo_commands->_buf, otcv::ResourceState::TransferDst, otcv::ResourceState::ComputeSSBOWrite);

	cmd_buf->cmd_bind_compute_pipeline(_pipeline);
	cmd_buf->cmd_bind_descriptor_set(_pipeline, _frame_ctxs[frame_id]._desc_set, DescriptorSetRate::PerFrame);
	cmd_buf->cmd_bind_descriptor_set(_pipeline, in_context.desc_set, DescriptorSetRate::ComputeRead);
	cmd_buf->cmd_bind_descriptor_set(_pipeline, out_context.desc_set, DescriptorSetRate::ComputeWrite);
	cmd_buf->cmd_dispatch(otcv::calc_group_count(_n_obj, _compute_group_size), 1, 1);

	cmd_buf->cmd_buffer_memory_barrier(out_context.ssbo_commands->_buf, otcv::ResourceState::ComputeSSBOWrite, otcv::ResourceState::IndirectRead);
	cmd_buf->cmd_buffer_memory_barrier(out_context.ssbo_draw_count->_buf, otcv::ResourceState::ComputeSSBOWrite, otcv::ResourceState::IndirectRead);
}
