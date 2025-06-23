#include "mesh_preprocessor.h"
#include "render_global_types.h"

MeshPreprocessor::MeshPreprocessor(const std::string& shader_path) {
	_mesh_presprocess_blob = otcv::load_shaders_from_dir(shader_path);
	_aabb_pipeline = otcv::ComputePipeline::create(_mesh_presprocess_blob["aabb.comp"]);

	_mesh_preprocess_desc_pool.reset(new NaiveExpandableDescriptorPool);
	_aabb_in_desc_set = _mesh_preprocess_desc_pool->allocate(_aabb_pipeline->desc_set_layouts[DescriptorSetRate::ComputeRead]);
	_aabb_out_desc_set = _mesh_preprocess_desc_pool->allocate(_aabb_pipeline->desc_set_layouts[DescriptorSetRate::ComputeWrite]);

	_cmd_buf = otcv::get_context().command_pool->allocate();
}

MeshPreprocessor::~MeshPreprocessor() {
	_aabb_pipeline->destroy();
}

void MeshPreprocessor::generate_aabb(
	otcv::Buffer* positions,
	std::vector<uint32_t> vertex_offsets,
	std::vector<uint32_t> vertex_counts,
	otcv::ResourceState position_source_state,
	otcv::ResourceState position_target_state,
	otcv::ResourceState aabb_buffer_target_state) {

	assert(vertex_offsets.size() == vertex_counts.size());
	uint32_t n_obj = vertex_offsets.size();

	_aabb_in_desc_set->bind_buffer(0, positions);

	Std430AlignmentType MeshInfo;
	MeshInfo.add(Std430AlignmentType::InlineType::Uint, "firstVertex");
	MeshInfo.add(Std430AlignmentType::InlineType::Uint, "vertexCount");
	_mesh_info_ssbo.reset(new SSBO(MeshInfo, n_obj));
	std::vector<SSBO::WriteContext> ssbo_writes(n_obj);
	for (uint32_t i = 0; i < n_obj; ++i) {
		ssbo_writes[i].id = i;
		ssbo_writes[i].access_ctxs.push_back({ SSBOAccess()["firstVertex"], &vertex_offsets[i] });
		ssbo_writes[i].access_ctxs.push_back({ SSBOAccess()["vertexCount"], &vertex_counts[i] });
	}
	_mesh_info_ssbo->write(ssbo_writes);
	_aabb_in_desc_set->bind_buffer(1, _mesh_info_ssbo->_buf);

	Std430AlignmentType AABB;
	AABB.add(Std430AlignmentType::InlineType::Vec3, "min");
	AABB.add(Std430AlignmentType::InlineType::Vec3, "max");
	_aabb_ssbo.reset(new SSBO(AABB, n_obj));
	_aabb_out_desc_set->bind_buffer(0, _aabb_ssbo->_buf);

	_cmd_buf->begin(true);
	_cmd_buf->cmd_bind_compute_pipeline(_aabb_pipeline);
	_cmd_buf->cmd_bind_descriptor_set(_aabb_pipeline, _aabb_in_desc_set, DescriptorSetRate::ComputeRead);
	_cmd_buf->cmd_bind_descriptor_set(_aabb_pipeline, _aabb_out_desc_set, DescriptorSetRate::ComputeWrite);
	if (position_source_state != otcv::ResourceState::ComputeSSBORead) {
		_cmd_buf->cmd_buffer_memory_barrier(
			positions,
			position_source_state,
			position_target_state);
	}

	for (uint32_t i = 0; i < n_obj; ++i) {
		_cmd_buf->cmd_push_constant(_aabb_pipeline, "meshId", &i);
		_cmd_buf->cmd_dispatch(otcv::calc_group_count(vertex_counts[i], _compute_group_size), 1, 1);
	}

	_cmd_buf->cmd_buffer_memory_barrier(
		positions,
		otcv::ResourceState::ComputeSSBORead,
		position_target_state);
	_cmd_buf->cmd_buffer_memory_barrier(
		_aabb_ssbo->_buf,
		otcv::ResourceState::ComputeSSBOWrite,
		aabb_buffer_target_state);
	_cmd_buf->end();

	otcv::QueueSubmit submit;
	submit.batch()
		.add_command_buffer(_cmd_buf)
		.end();
	otcv::get_context().queue->submit(submit);
}

