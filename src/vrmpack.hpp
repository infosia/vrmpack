#ifndef VRMPACK_HPP_INCLUDED__
#define VRMPACK_HPP_INCLUDED__

#include <string>
#include <vector>

#define CGLTF_VRM_v0_0
#include "cgltf/cgltf.h"

/* Version macro; major * 1000 + minor * 10 + patch */
#define VRMPACK_VERSION 100 /* 0.1 */

namespace VRM {

struct Mesh
{
	std::string name;

	cgltf_mesh *mesh;
	cgltf_primitive *primitive;
	cgltf_material* material;
	cgltf_skin* skin;

	std::vector<uint32_t> indices;

	size_t vertex_count;
	size_t vertex_positions_stride;

	std::vector<cgltf_float> positions;
	std::vector<cgltf_float> normals;
	std::vector<cgltf_float> texcoord;
	std::vector<cgltf_float> weights;
	std::vector<cgltf_uint>  joints;

};

struct Settings
{
	float simplify_threshold;
	bool simplify_aggressive;
	float simplify_debug;

	float target_error;
	float target_error_aggressive;

	int verbose;
};


} // namespace VRM

#endif /* #ifdef VRMPACK_HPP_INCLUDED__ */
