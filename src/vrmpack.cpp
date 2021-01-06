#include "vrmpack.hpp"

#include <ctype.h>

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

	std::vector<unsigned int> indices(mesh->indices.size());
	if (settings.simplify_aggressive)
	{
		indices.resize(meshopt_simplifySloppy(&indices[0], &mesh->indices[0], mesh->indices.size(), &mesh->positions[0], mesh->vertex_count, mesh->vertex_positions_stride, target_index_count, settings.target_error));
	}
	else
	{
		indices.resize(meshopt_simplify(&indices[0], &mesh->indices[0], mesh->indices.size(), &mesh->positions[0], mesh->vertex_count, mesh->vertex_positions_stride, target_index_count, settings.target_error));
	}
	mesh->indices.swap(indices);
}

static void parseIndices(Mesh* mesh, cgltf_primitive* primitive)
{
	mesh->indices.resize(primitive->indices->count);
	for (cgltf_size i = 0; i < primitive->indices->count; ++i)
	{
		mesh->indices[i] = (uint32_t)cgltf_accessor_read_index(primitive->indices, i);
	}
}

static void parseAccessors(Mesh* mesh)
{
	for (cgltf_size i = 0; i < mesh->primitive->attributes_count; ++i)
	{
		const cgltf_attribute* attr = &mesh->primitive->attributes[i];

		cgltf_accessor* acc_POSITION = NULL;
		cgltf_accessor* acc_NORMAL = NULL;
		cgltf_accessor* acc_TEXCOORD_0 = NULL;
		cgltf_accessor* acc_WEIGHTS_0 = NULL;
		cgltf_accessor* acc_JOINTS_0 = NULL;

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

		if (acc_POSITION != nullptr)
		{
			const cgltf_size unpack_count = acc_POSITION->count * 3;
			mesh->positions.resize(unpack_count);
			cgltf_accessor_unpack_floats(acc_POSITION, &mesh->positions[0], unpack_count);

			mesh->vertex_count = acc_POSITION->count;
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
}

static cgltf_data* parse(const char* input, std::vector<Mesh*> meshes)
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

static int vrmpack(const char* input, const char* output, Settings settings)
{
	(void)output, settings;

	std::vector<Mesh*> meshes;
	cgltf_data* data = parse(input, meshes);

	if (data == nullptr)
	{
		return cgltf_result_invalid_gltf;
	}

	for (size_t i = 0; i < meshes.size(); ++i)
	{
		processMesh(meshes[i], settings);
	}

	// output TODO

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
