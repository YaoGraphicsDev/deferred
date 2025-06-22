#pragma once

enum class RenderPassType {
	Shadow = 0,
	Geometry,
	Lighting,
	PostProcess,
	All
};

enum DescriptorSetRate {
	PerFrame = 0,
	PerObject = 1,
	PerMaterial = 2,
	ComputeRead = 1,
	ComputeWrite
};

// struct LightingModel {
// 	enum class Model {
// 		PBR,
// 		Lambert,
// 		Unlit
// 	};
// 	enum class Variant {
// 		Default,
// 		Emissive,
// 	};
// 	Model model = Model::Unlit;
// 	Variant variant = Variant::Default;
// };
