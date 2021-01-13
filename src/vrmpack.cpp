#include "vrmpack.hpp"

#include <ctype.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>

#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#define CGLTF_VRM_v0_0_IMPLEMENTATION
#include "cgltf/cgltf_write.h"

#include "meshoptimizer/src/meshoptimizer.h"

using namespace VRM;

std::string getVersion()
{
	char result[32];
	sprintf(result, "%d.%d", VRMPACK_VERSION / 1000, (VRMPACK_VERSION % 1000) / 10);
	return result;
}

static Settings defaults()
{
	Settings settings = {};
	settings.simplify_threshold = 1.f;
	settings.simplify_aggressive = false;
	settings.target_error = 1e-2f;
	settings.target_error_aggressive = 1e-1f;
	return settings;
}

static cgltf_skin* get_skin(const cgltf_data* data, const cgltf_mesh* mesh)
{
	for (cgltf_size i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node* node = &data->nodes[i];
		if (node->skin != NULL && node->mesh != NULL && node->mesh->name != NULL && strcmp(node->mesh->name, mesh->name) == 0)
		{
			return node->skin;
		}
	}
	return NULL;
}
static void processMesh(Mesh* mesh, Settings& settings)
{
	const size_t target_index_count = size_t(double(mesh->indices.size() / 3) * settings.simplify_threshold) * 3;

	std::vector<uint32_t> indices(mesh->indices.size());
	indices.resize(meshopt_simplify(&indices[0], &mesh->indices[0], mesh->indices.size(), &mesh->positions[0], mesh->vertex_count, mesh->vertex_positions_stride, target_index_count, settings.target_error));
	mesh->indices.swap(indices);

	// if the precise simplifier got "stuck", we'll try to simplify using the sloppy simplifier; this is only used when aggressive simplification is enabled as it breaks attribute discontinuities
	if (settings.simplify_aggressive && mesh->indices.size() > target_index_count)
	{
		indices.resize(meshopt_simplifySloppy(&indices[0], &mesh->indices[0], mesh->indices.size(), &mesh->positions[0], mesh->vertex_count, mesh->vertex_positions_stride, target_index_count, settings.target_error_aggressive));
		mesh->indices.swap(indices);
	}
}

static void parseIndices(Mesh* mesh, cgltf_primitive* primitive)
{
	mesh->indices_accessor = primitive->indices;
	mesh->indices.resize(primitive->indices->count);
	for (cgltf_size i = 0; i < primitive->indices->count; ++i)
	{
		mesh->indices[i] = (uint32_t)cgltf_accessor_read_index(primitive->indices, i);
	}
}

static void parseAccessors(Mesh* mesh)
{
	cgltf_accessor* acc_POSITION = nullptr;
	cgltf_accessor* acc_NORMAL = nullptr;
	cgltf_accessor* acc_TEXCOORD_0 = nullptr;
	cgltf_accessor* acc_WEIGHTS_0  = nullptr;
	cgltf_accessor* acc_JOINTS_0   = nullptr;

	for (cgltf_size i = 0; i < mesh->primitive->attributes_count; ++i)
	{
		const cgltf_attribute* attr = &mesh->primitive->attributes[i];

		if (attr->type == cgltf_attribute_type_position)
		{
			acc_POSITION = attr->data;
		}
		else if (attr->type == cgltf_attribute_type_normal)
		{
			acc_NORMAL = attr->data;
		}
		else if (strcmp(attr->name, "TEXCOORD_0") == 0)
		{
			acc_TEXCOORD_0 = attr->data;
		}
		else if (strcmp(attr->name, "WEIGHTS_0") == 0)
		{
			acc_WEIGHTS_0 = attr->data;
		}
		else if (strcmp(attr->name, "JOINTS_0") == 0)
		{
			acc_JOINTS_0 = attr->data;
		}
	}

	if (acc_POSITION != nullptr)
	{
		const cgltf_size unpack_count = acc_POSITION->count * 3;
		mesh->positions.resize(unpack_count);
		cgltf_accessor_unpack_floats(acc_POSITION, &mesh->positions[0], unpack_count);

		mesh->vertex_count  = acc_POSITION->count;
		mesh->vertex_positions_stride = acc_POSITION->stride;
	}

	if (acc_NORMAL != nullptr)
	{
		const cgltf_size unpack_count = acc_NORMAL->count * 3;
		mesh->normals.resize(unpack_count);
		cgltf_accessor_unpack_floats(acc_NORMAL, &mesh->normals[0], unpack_count);
	}

	if (acc_TEXCOORD_0 != nullptr)
	{
		const cgltf_size unpack_count = acc_TEXCOORD_0->count * 2;
		mesh->texcoord.resize(unpack_count);
		cgltf_accessor_unpack_floats(acc_TEXCOORD_0, &mesh->texcoord[0], unpack_count);
	}

	if (acc_JOINTS_0 != nullptr)
	{
		const cgltf_size unpack_count = acc_JOINTS_0->count * 4;
		mesh->joints.resize(unpack_count);
		for (cgltf_size j = 0; j < acc_JOINTS_0->count; ++j)
		{
			cgltf_accessor_read_uint(acc_JOINTS_0, j, &mesh->joints[0] + (j * 4), 4);
		}
	}

	if (acc_WEIGHTS_0 != nullptr)
	{
		const cgltf_size unpack_count = acc_WEIGHTS_0->count * 4;
		mesh->weights.resize(unpack_count);
		cgltf_accessor_unpack_floats(acc_WEIGHTS_0, &mesh->weights[0], unpack_count);
	}
}

static cgltf_data* parse(const char* input, std::vector<Mesh*>& meshes)
{
	cgltf_options options = {};
	cgltf_data* data = nullptr;
	cgltf_result result = cgltf_parse_file(&options, input, &data);

	if (result != cgltf_result_success)
	{
		fprintf(stderr, "Failed to parse file %s\n", input);
		return nullptr;
	}

	result = cgltf_load_buffers(&options, data, input);

	if (result != cgltf_result_success)
	{
		cgltf_free(data);
		fprintf(stderr, "Failed to load buffers from %s\n", input);
		return nullptr;
	}

	for (cgltf_size i = 0; i < data->meshes_count; ++i)
	{
		cgltf_mesh* mesh = &data->meshes[i];
		for (cgltf_size j = 0; j < mesh->primitives_count; ++j)
		{
			cgltf_primitive* primitive = &mesh->primitives[j];

			Mesh* m = new Mesh();
			m->mesh = mesh;
			m->name = mesh->name;
			m->primitive = primitive;
			m->skin = get_skin(data, mesh);

			parseIndices(m, primitive);
			parseAccessors(m);

			meshes.push_back(m);
		}
	}

	return data;
}

static void printSceneStats(cgltf_data* data, std::vector<Mesh*>& meshes)
{
	cgltf_result validate_result = cgltf_validate(data);
	if (validate_result != cgltf_result_success) {
		printf("Warn: glTF validation error found: %d\n", validate_result);
	}

	cgltf_size in_meshes_count  = 0;
	cgltf_size in_indices_count = 0;
	cgltf_size in_vertices_count = 0;
	for (cgltf_size i = 0; i < data->meshes_count; ++i)
	{
		const cgltf_mesh* mesh = &data->meshes[i];
		in_meshes_count += mesh->primitives_count;
		for (cgltf_size j = 0; j < mesh->primitives_count; ++j) {
			in_indices_count += mesh->primitives[j].indices->count;
		}
	}

	cgltf_size out_meshes_count  = 0;
	cgltf_size out_indices_count = 0;
	cgltf_size out_vertices_count = 0;

	out_meshes_count = meshes.size();

	for (cgltf_size i = 0; i < meshes.size(); ++i) {
		Mesh* m = meshes[i];
		out_indices_count += m->indices.size();
		in_vertices_count += m->vertex_count;
	}

	printf("input: %zd nodes, %zd primitives %zd indices, %zd vertices\n", data->nodes_count, in_meshes_count, in_indices_count, in_vertices_count);
	printf("output: %zd nodes, %zd primitives %zd indices, %zd vertices\n", data->nodes_count, out_meshes_count, out_indices_count, out_vertices_count);
}

static void write_bin(cgltf_data* data, std::string output) 
{
	std::ofstream fout;
	fout.open(output.c_str(), std::ios::out | std::ios::binary);

	for (cgltf_size i = 0; i < data->buffers_count; i++) {
		cgltf_buffer* buffer = &data->buffers[i];
		fout.write(reinterpret_cast<const char*>(&buffer->size), 4);
		fout.write(reinterpret_cast<const char*>(&GlbMagicBinChunk), 4);
		fout.write(reinterpret_cast<const char*>(buffer->data), buffer->size);
	}

	fout.close();
}

static void processBuffers(cgltf_data* data, std::vector<Mesh*> meshes)
{
	// update indices assuming indices never increase.
	std::set<cgltf_size> buffers_changed;
	for (const auto mesh : meshes) {
	    cgltf_accessor* accessor = mesh->indices_accessor;

	    accessor->count = mesh->indices.size();
	    accessor->buffer_view->size = accessor->count * sizeof(uint32_t);
	    memcpy((uint8_t*)accessor->buffer_view->buffer->data + accessor->buffer_view->offset, &mesh->indices[0], accessor->buffer_view->size);

		buffers_changed.insert(mesh->indices_accessor->buffer_view->buffer_index);
	}

	// re-create buffers
	for (const auto b : buffers_changed) {
        cgltf_buffer* buffer = &data->buffers[b];
        uint8_t* dst = (uint8_t*)malloc(buffer->size);
		cgltf_size dst_offset = 0;
        for (cgltf_size i = 0; i < data->buffer_views_count; ++i)
        {
            cgltf_buffer_view* buffer_view = &data->buffer_views[i];
            if (buffer_view->buffer_index == b) {
                memcpy(dst + dst_offset, (uint8_t*)buffer->data + buffer_view->offset, buffer_view->size);
				buffer_view->offset = dst_offset;
				// align each bufferView by 4 bytes
				dst_offset += (buffer_view->size + 3) & ~3;
            }
        }

		data->buffers[b].size = dst_offset;
        memcpy(buffer->data, dst, dst_offset);
		free(dst);
	}
}

static void write(std::string output, std::string in_json, std::string in_bin) 
{
	std::ifstream in_json_st (in_json,std::ios::binary);
	std::ifstream in_bin_st (in_bin,std::ios::binary);
	std::ofstream out_st (output,std::ios::trunc|std::ios::binary);

	in_json_st.seekg(0, std::ios::end);
	uint32_t json_size = (uint32_t)in_json_st.tellg();
	in_json_st.seekg(0, std::ios::beg);

	in_bin_st.seekg(0, std::ios::end);
	uint32_t bin_size = (uint32_t)in_bin_st.tellg();
	in_bin_st.seekg(0, std::ios::beg);

	uint32_t total_size = GlbHeaderSize + GlbChunkHeaderSize + json_size + bin_size;

	out_st.write(reinterpret_cast<const char*>(&GlbMagic),   4);
	out_st.write(reinterpret_cast<const char*>(&GlbVersion), 4);
	out_st.write(reinterpret_cast<const char*>(&total_size), 4);

	out_st.write(reinterpret_cast<const char*>(&json_size), 4);
	out_st.write(reinterpret_cast<const char*>(&GlbMagicJsonChunk), 4);

	out_st << in_json_st.rdbuf();
	out_st << in_bin_st.rdbuf();

	out_st.close();
}

static int vrmpack(const char* input, const char* output, Settings settings)
{
	std::vector<Mesh*> meshes;
	cgltf_data* data = parse(input, meshes);

	if (data == nullptr)
	{
		return cgltf_result_invalid_gltf;
	}

	std::stringstream inss_json;

	inss_json << output << ".in.json";
	cgltf_options write_options = {};
	cgltf_write_file(&write_options, inss_json.str().c_str(), data);

	for (size_t i = 0; i < meshes.size(); ++i)
	{
		processMesh(meshes[i], settings);
	}

	processBuffers(data, meshes);

	std::stringstream outss_json;
	std::stringstream outss_bin;

	outss_json << output << ".json";
	outss_bin  << output << ".bin";

	std::string out_json = outss_json.str();
	std::string out_bin = outss_bin.str();

	cgltf_write_file(&write_options, out_json.c_str(), data);

	write_bin(data, out_bin);
	write(output, out_json, out_bin);

	if (settings.verbose > 0) {
		printSceneStats(data, meshes);
	}

	// clean up
	for (size_t i = 0; i < meshes.size(); ++i)
	{
		delete meshes[i];
	}
	meshes.clear();

	cgltf_free(data);

	return 0;
}

int main(int argc, char** argv)
{
	Settings settings = defaults();

	const char* input = 0;
	const char* output = 0;
	bool help = false;

	std::vector<const char*> testinputs;

	for (int i = 1; i < argc; ++i)
	{
		const char* arg = argv[i];

		if (strcmp(arg, "-si") == 0 && i + 1 < argc && isdigit(argv[i + 1][0]))
		{
			settings.simplify_threshold = float(atof(argv[++i]));
		}
		else if (strcmp(arg, "-sa") == 0)
		{
			settings.simplify_aggressive = true;
		}
		else if (strcmp(arg, "-i") == 0 && i + 1 < argc && !input)
		{
			input = argv[++i];
		}
		else if (strcmp(arg, "-o") == 0 && i + 1 < argc && !output)
		{
			output = argv[++i];
		}
		else if (strcmp(arg, "-v") == 0)
		{
			settings.verbose = 1;
		}
		else if (strcmp(arg, "-vv") == 0)
		{
			settings.verbose = 2;
		}
		else if (strcmp(arg, "-h") == 0)
		{
			help = true;
		}
		else if (arg[0] == '-')
		{
			fprintf(stderr, "Unrecognized option %s\n", arg);
			return 1;
		}
	}

	// shortcut for vrmpack -v
	if (settings.verbose && argc == 2)
	{
		printf("vrmpack %s\n", getVersion().c_str());
		return 0;
	}

	if (!input || !output || help)
	{
		fprintf(stderr, "vrmpack %s\n", getVersion().c_str());
		fprintf(stderr, "Usage: vrmpack [options] -i input -o output\n");

		if (help)
		{
			fprintf(stderr, "\nBasics:\n");
			fprintf(stderr, "\t-i file: input file to process, .vrm\n");
			fprintf(stderr, "\t-o file: output file path, .vrm\n");
			fprintf(stderr, "\nSimplification:\n");
			fprintf(stderr, "\t-si R: simplify meshes to achieve the ratio R (default: 1; R should be between 0 and 1)\n");
			fprintf(stderr, "\t-sa: aggressively simplify to the target ratio disregarding quality\n");
			fprintf(stderr, "\nMiscellaneous:\n");
			fprintf(stderr, "\t-v: verbose output (print version when used without other options)\n");
			fprintf(stderr, "\t-h: display this help and exit\n");
		}
		else
		{
			fprintf(stderr, "\nBasics:\n");
			fprintf(stderr, "\t-i file: input file to process, .vrm\n");
			fprintf(stderr, "\t-o file: output file path, .vrm\n");
			fprintf(stderr, "\t-si R: simplify meshes to achieve the ratio R (default: 1; R should be between 0 and 1)\n");
			fprintf(stderr, "\nRun vrmpack -h to display a full list of options\n");
		}

		return 1;
	}

	return vrmpack(input, output, settings);
}
