/*
 * Simple utility program for extracting files referenced by given IPL file.
 */
#include <fstream>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "renderware.h"
#include <map>
#include <GL/glew.h>


#if defined(BAKE_WITH_LZHAM)
	// LZHAM_DEFINE_ZLIB_API causes lzham.h to remap the standard zlib.h functions/macro definitions to lzham's.
	// This is totally optional - you can also directly use the lzham_* functions and macros instead.
	#define LZHAM_DEFINE_ZLIB_API
	#include <lzham_static_lib.h>
#endif


uint32_t MAX_ARRAY_TEXTURE_LAYERS = 2048;


enum ItemDefinitionFlags {
	IDFLAG_WET = 1 << 0,                  //!< Wet effect (object appear darker)
	IDFLAG_DONT_FADE = 1 << 1,            //!< Do not fade the object when it is being loaded into or out of view
	IDFLAG_VISIBLE_THROUGH = 1 << 2,      //!< Allow transparencies of other objects to be visible through this object
	IDFLAG_ALPHA_TRANSPARENCY_2 = 1 << 3, //!< Alpha transparency 2
	// skipped 1 << 4
	IDFLAG_INTERIOR = 1 << 5,             //!< Indicates and object to be used inside an interior
	IDFLAG_NO_SHADOW_MESH = 1 << 6,       //!< Disable shadow mesh, allow transparencies of other objects, shadow, and lights to be visible through this object
	IDFLAG_DONT_CULL = 1 << 7,            //!< Object surface will not be culled
	IDFLAG_NO_DRAW_DISTANCE = 1 << 8,     //!< Disables draw distance (only used for LOD objects with an LOD value greater than 299)
	IDFLAG_BREAKABLE = 1 << 9,            //!< Object is breakable (like glass - additional parameters defined inside the 'objects.dat' file)
	IDFLAG_BREAKABLE_2 = 1 << 10          //!< Similar to above, but object first cracks on a strong collision, then it breaks
};


enum Interior {
	VIS_MAIN_MAP,       //!< The main world (outside)
	VIS_HOTEL,          //!< Ocean View Hotel (plays Hotel.mp3)
	VIS_MANSION,        //!< Diaz's mansion
	VIS_BANK,           //!< El Banco Corrupto Grande
	VIS_MALL,           //!< North Point Mall (plays MallAmb.mp3)
	VIS_STRIP_CLUB,     //!< Pole Position Club (plays Strip.mp3)
	VIS_LAWYERS,        //!< Ken Rosenberg's office
	VIS_COFFEE_SHOP,    //!< Cafe Robina
	VIS_CONCERT_HALL,   //!< Love Fist contert hall
	VIS_STUDIO,         //!< Love Fist recording studio
	VIS_RIFLE_RANGE,    //!< Shooting Range
	VIS_BIKER_BAR,      //!< Apartament 3C, Greasy Choppers
	VIS_POLICE_STATION, //!< VCPD HQ, Auntie Poulet's
	VIS_EVERYWHERE,     //!< <special>
	VIS_DIRT,           //!< Dirt Ring (plays DirtRing.mp3)
	VIS_BLOOD,          //!< Bloodring (plays DirtRing.mp3)
	VIS_OVALRING,       //!< Hotring (plays DirtRing.mp3)
	VIS_MALIBU_CLUB,    //!< The Malibu Club (plays Malibu.mp3)
	VIS_PRINT_WORKS     //!< Print Works
};


enum FilterMode {
	FILTER_NONE = 0,
	FILTER_NEAREST = 1,
	FILTER_LINEAR = 2,
	FILTER_MIP_NEAREST = 3,
	FILTER_MIP_LINEAR = 4,
	FILTER_LINEAR_MIP_NEAREST = 5,
	FILTER_LINEAR_MIP_LINEAR = 6
};

enum AddressingMode {
	WRAP_NONE = 0,
	WRAP_WRAP = 1,
	WRAP_MIRROR = 2,
	WRAP_CLAMP = 3
};


const size_t NUM_TEX_GROUPS = 11;

static const
uint16_t TEXKEY_FORMAT_LUT[NUM_TEX_GROUPS] = {
	 0, //RASTER_DEFAULT = 0x0000,  // not supported
	5,  //RASTER_1555 = 0x0100,     // alpha!
	2,  //RASTER_565 = 0x0200,
	4,  //RASTER_4444 = 0x0300,     // alpha!
	1,  //RASTER_LUM8 = 0x0400,
	 6, //RASTER_8888 = 0x0500,     // alpha!
	3,  //RASTER_888 = 0x0600,
	 0, //RASTER_16 = 0x0700,       // not supported
	 0, //RASTER_24 = 0x0800,       // not supported
	 0, //RASTER_32 = 0x0900,       // not supported
	2,  //RASTER_555 = 0x0a00,
};

static const
uint16_t TEXPOW_LUT[16] = {
	 0, // -- might be below or above range
	0,  // 16
	1,  // 32
	 0, // 48 - not power of 2
	2,  // 64
	 0, // 80 - not power of 2
	 0, // 96 - not power of 2
	 0, // 112 - not power of 2
	3,  // 128
	 0, // 144 - not power of 2
	 0, // 160 - not power of 2
	 0, // 176 - not power of 2
	 0, // 192 - not power of 2
	 0, // 208 - not power of 2
	 0, // 224 - not power of 2
	4  // 256
};


struct DrawCall {
	GLuint texture_array; //!< OpenGL's name (handle) of the texture array to bind
	GLuint tex_index; //!< Integer index to the texture within texture array

	GLuint index_offset; //!< Byte offset within index buffer to the first index
	GLuint num_vertices; //!< How many vertices to draw
	GLuint base_vertex; //!< Value added to all fetched index buffer indices

	GLuint num_instances; //!< Number of instances to render (how many duplicates)
	GLuint base_instance; //!< Index to the first instance data within instance buffer
};
std::map<uint64_t, DrawCall> ordered_draw_calls;


struct Instance {
	int id;
	uint32_t num_instances;
	uint32_t base_instance;
};


//! Structure representing a file entry in the IMG archive (loaded from DIR file)
struct DirectoryEntry {
	// NOTE: IMG's sector size is equal to 2048 bytes
	uint32_t offset; //!< Offset (in sectors)
	uint32_t size; //!< Number of consecutive sectors
	unsigned char name[24]; //!< Name of the file (NUL-terminated)
};

//! Structure representing an entry from IDE file (INST and TOBJ sections)
struct ItemDefinitionEntry {
	int id; //!< Unique object ID (max 6500)
	std::string model_name; //!< Name of the .dff model file w/o extension
	std::string txd_name; //!< Name of the .txd texture dictionary file w/o extension
	int mesh_count; //!< Number of meshes (also equal to the number of draw distance fields)
	float draw_distance[3]; //!< Draw distance for all meshes (0 < x < 300)
	int flags; //!< Object flags
	int time_on, time_off;
};

//! Structure representing an entry from IPL file (OBJS section)
struct ItemPlacementEntry {
	glm::vec3 position;
	glm::vec3 scale;
	glm::quat rotation;
	glm::mat4 world_from_object;
	std::string model_name;
	int interior;
	int id; //!< Unique object ID (max 6500)
};


std::unordered_map<std::string, uint32_t> ide_lookup; //!< model name (without extension) -> ID (from IDE file)
std::map<int, ItemDefinitionEntry> item_definitions;
std::map<int, std::vector<ItemPlacementEntry> > item_placements; //!< This structure is better for collecting batches
std::unordered_set<std::string> dependent_dff, dependent_txd;

struct TextureBucket {
	std::vector<GLuint> tex_array;
	std::vector<rw::NativeTexture> natives;
};

//! Map holding created textures that where bucketized by their key.
std::map<uint16_t, TextureBucket> texture_buckets;

struct TextureRef {
	uint32_t index; // Index to the texture within the bucket (within natives[])
	uint16_t bucket_key; // Key to the `texture_buckets` map
};
std::unordered_map<std::string, TextureRef> named_textures;


std::vector<glm::vec3> baked_vert_pos; //!< Buffer with vertex positions of all models
std::vector<glm::u8vec4> baked_vert_rgba; //!< Buffer with vertex colors of all models
std::vector<glm::vec3> baked_vert_normals; //!< Buffer with vertex normals of all models
std::vector<glm::vec4> baked_vert_uv; //!< Buffer with vertex UVs of all models
std::vector<uint16_t> baked_indices; //!< Buffer with face indices of all models



struct MeshTableEntry {
	uint32_t id; //!< Unique ID (from IDE)                   // TODO: This might be uint16_t!
	//uint32_t num_indices; //!< Number of indices to render // TODO: This might be uint16_t!
	uint32_t num_splits; //!< Number of material splits      // TODO: This might be uint16_t or even uint8_t!
	uint32_t base_vertex; //!< Offset into index buffer
	uint32_t offset; //!< Byte offset into index buffer
};

//! Ordered container for item definitions that will be serialized to "meshtable".
std::map<uint32_t, MeshTableEntry> mesh_table;

struct MaterialSplit {
	std::string mat_name;
	uint32_t num_indices;
	uint32_t material_idx;
};

std::map<uint32_t, std::vector<MaterialSplit> > material_splits;



//! Check, if given string starts with given prefix.
static inline
bool starts_with(const std::string &haystack, const std::string &needle) {
	return 0 == haystack.compare(0, needle.size(), needle);
}


size_t fwrite_compressed(const void *data, size_t element_size, size_t element_count, FILE *file)
{
#if defined(BAKE_WITH_LZHAM)
	// Compress the data using LZHAM
	const size_t uncompressed_bytes = element_size * element_count;
	uLong compressed_bytes = compressBound(uncompressed_bytes);
	uint8_t *buffer = (uint8_t *)malloc(compressed_bytes);
	int status = compress(buffer, &compressed_bytes, (const uint8_t *)data, uncompressed_bytes);
	if (Z_OK != status)
		assert(false && "Compression failed!");

	fwrite(&compressed_bytes, sizeof(uLong), 1, file);
	size_t bytes_written = fwrite(buffer, sizeof(uint8_t), compressed_bytes, file);

	free(buffer);
	return bytes_written;
#else
	// Write uncompressed data
	return element_size * fwrite(data, element_size, element_count, file);
#endif
}


void upload_meshes()
{
	// Write baked buffers to "meshes.blob"
	FILE *blob = fopen("meshes.blob", "wb");
	uint32_t num_vertices = baked_vert_pos.size();
	uint32_t num_indices = baked_indices.size();
	fwrite(&num_vertices, sizeof(uint32_t), 1, blob);
	fwrite(&num_indices, sizeof(uint32_t), 1, blob);

	fwrite_compressed(baked_indices.data(), sizeof(uint16_t), num_indices, blob);
	fwrite_compressed(baked_vert_pos.data(), sizeof(glm::vec3), num_vertices, blob);
	fwrite_compressed(baked_vert_rgba.data(), sizeof(glm::u8vec4), num_vertices, blob);
	fwrite_compressed(baked_vert_uv.data(), sizeof(glm::vec4), num_vertices, blob);
	fclose(blob);
}


bool read_dff_mesh(uint32_t id, const std::string &filename)
{
	fprintf(stderr, "Loading DFF id=%u: '%s'\n", id, filename.c_str());
	std::ifstream in(filename, std::ios::binary);
	if (in.fail()) {
		//std::cerr << "cannot open " << argv[0] << endl;
		return false;
	}

	using namespace rw;
	HeaderInfo header;
	while (header.read(in) && CHUNK_NAOBJECT != header.type) {
		if (CHUNK_CLUMP == header.type) {
			in.seekg(-12, std::ios::cur);
			Clump *clump = new Clump;
			clump->read(in);

			if (0 == clump->geometryList.size())
				continue; // This shall not happen... Invalid data!

			const Geometry &geo = clump->geometryList.front(); {
			//for (const Geometry &geo : clump->geometryList) {
				if (0 == geo.vertexCount)
					continue;
				MeshTableEntry mesh = {};
				mesh.id = id;
				mesh.base_vertex = (uint32_t)baked_vert_pos.size();
				mesh.offset = sizeof(uint16_t) * baked_indices.size();
				mesh.num_splits = geo.splits.size();
				// mesh.num_indices depends on the number of material splits :/

				// Load vertex data
				for (rw::uint32 v = 0; v < geo.vertexCount; ++v) {
					glm::vec3 pos(geo.vertices[3 * v + 0], geo.vertices[3 * v + 1], geo.vertices[3 * v + 2]);
					glm::u8vec4 rgba((uint8_t)geo.vertexColors[4 * v + 0], (uint8_t)geo.vertexColors[4 * v + 1], (uint8_t)geo.vertexColors[4 * v + 2], (uint8_t)geo.vertexColors[4 * v + 3]);
					glm::vec4 uv(0.0f, 0.0f, 0.0f, 0.0f);
					glm::vec3 normal;

					if (0 < geo.texCoords[0].size()) {
						uv.x = geo.texCoords[0][2 * v + 0];
						uv.y = geo.texCoords[0][2 * v + 1];
					}
					if (0 < geo.texCoords[1].size()) {
						uv.z = geo.texCoords[1][2 * v + 0];
						uv.w = geo.texCoords[1][2 * v + 1];
					}

					baked_vert_pos.push_back(pos);
					baked_vert_rgba.push_back(rgba);
					baked_vert_uv.push_back(uv);
				}

				// Load optimized indices from 'Bin Mesh PLG' chunk
				material_splits[id].reserve(mesh.num_splits);
				for (size_t b = 0; b < geo.splits.size(); ++b) {
					const Split &split = geo.splits[b];
					MaterialSplit batch = {};

					batch.material_idx = split.matIndex;
					batch.mat_name = geo.materialList[split.matIndex].texture.name;
					for (uint32_t idx : split.indices)
						baked_indices.push_back(idx);
					batch.num_indices = split.indices.size();
					material_splits[id].push_back(batch);
				}

				mesh_table[id] = mesh;
			}
			delete clump;
		}
		else
			in.seekg(header.length);
	}
	in.close();
	return true;
}


bool read_txd(const std::string &filename)
{
	fprintf(stderr, "Loading TXD: '%s'\n", filename.c_str());
	using namespace rw;
	std::ifstream rw(filename, std::ios::binary);
	if (!rw.good())
		return false;
	TextureDictionary *txd = new TextureDictionary;
	txd->read(rw);
	rw.close();

	for (uint32 i = 0; i < txd->texList.size(); i++) {
		if (txd->texList[i].platform == PLATFORM_PS2)
			txd->texList[i].convertFromPS2(0x40);
		if (txd->texList[i].platform == PLATFORM_XBOX)
			txd->texList[i].convertFromXbox();
	}

	for (size_t i = 0; i < txd->texList.size(); ++i) {
		const NativeTexture &tex = txd->texList[i];
		if ("" == tex.name)
			continue; // Why are unnamed textures in TXD in the first place?

		// Create texture key: XAFF WWWW HHHH
		// NOTE: There are different combinations of those texture sizes: 16, 32, 64, 128, 256 (3 bits)
		uint16_t format_idx = (tex.rasterFormat >> 8) & 0xF;
		uint16_t tex_group_key = 0
			| TEXKEY_FORMAT_LUT[format_idx] << 8    // First 3 bits encode format, while the last bit is alpha bit
			| TEXPOW_LUT[tex.width[0] >> 4] << 4    // Texture width exponent (Vice City uses PoT textures)
			| TEXPOW_LUT[tex.height[0] >> 4] << 0;  // Texture height exponent (Vice City used PoT textures)

		if (named_textures.end() != named_textures.find(tex.name)) {
			// Do determine if this is the case of name conflict or an already loaded texture
			// we have to check the "registry" if it already contains this key.
			if (named_textures[tex.name].bucket_key == tex_group_key)
				continue; // OK - this texture is already loaded
			else {
				fprintf(stderr, "WARNING: Texture name conflict '%s'! Bucket keys: %x vs %x\n", tex.name.c_str(), named_textures[tex.name].bucket_key, tex_group_key);
				continue; // NAME CONFLICT! (might be hash conflict or texture with same names are present, but different formats!)
			}
		}

		TextureBucket &bucket = texture_buckets[tex_group_key];
		bucket.natives.push_back(tex);

		TextureRef &ref = named_textures[tex.name];
		ref.bucket_key = tex_group_key;
		ref.index = bucket.natives.size() - 1;
	}

	return true;
}


bool upload_textures()
{
	using namespace rw;

	// Write texture buckets (without keys - draw calls use just indices) to "texturebuckets.blob"
	FILE *blob = fopen("texturebuckets.blob", "wb");
	uint32_t texture_split_count = 0; // This count will be patched at the end, once we know the exact number
	uint32_t biggest_split_buffer = 0; // This size will be patched at the end, once we know the exact size
	fwrite(&texture_split_count, sizeof(uint32_t), 1, blob);
	fwrite(&biggest_split_buffer, sizeof(uint32_t), 1, blob);

	GLuint texarr_id = 0;
	for (auto &bucket_pair : texture_buckets) {
		TextureBucket &bucket = bucket_pair.second;
		if (bucket.natives.empty())
			continue; // Why there is an empty bucket in the first place?
		const NativeTexture &tex = bucket.natives.front(); // Get a "representative" texture
		size_t textures_left = bucket.natives.size();

		uint16_t slice = 0;
		while (0 < textures_left) {
			uint16_t SLICE_SIZE = std::min((uint16_t)textures_left, (uint16_t)MAX_ARRAY_TEXTURE_LAYERS);
			bucket.tex_array.push_back(++texarr_id);

			// Allocate and upload textures into array
			GLsizei width = tex.width[0];
			GLsizei height = tex.height[0];
			GLsizei layers = SLICE_SIZE;

			uint8_t *buffer = new uint8_t[layers * width * height * 4];
			size_t datasize = tex.dxtCompression ? tex.dataSizes[0] : (width * height * ((tex.hasAlpha || (tex.rasterFormat & (RASTER_PAL8 | RASTER_PAL4))) ? 4 : 3));
			for (int i = 0; i < layers; ++i) {
				const NativeTexture &tn = bucket.natives[slice * MAX_ARRAY_TEXTURE_LAYERS + i];
				assert(tex.width[0] == tn.width[0]);
				assert(tex.height[0] == tn.height[0]);
				assert(tex.dxtCompression == tn.dxtCompression);
				assert(tex.hasAlpha == tn.hasAlpha);
				assert((tex.rasterFormat & RASTER_MASK) == (tn.rasterFormat & RASTER_MASK));

				// Convert all palettizied textures back to normal encoding...
				if (tex.rasterFormat & RASTER_PAL8 || tex.rasterFormat & RASTER_PAL4) {
					for (uint32 j = 0; j < 1/*mipmapCount*/; ++j) {
						uint32 dataSize = tex.width[j] * tex.height[j] * 4;
						uint8 *newtexels = buffer + i * datasize;
						for (uint32 i = 0; i < tex.width[j] * tex.height[j]; i++) {
							// dont swap r and b
							newtexels[i * 4 + 0] = tex.palette[tex.texels[j][i] * 4 + 0];
							newtexels[i * 4 + 1] = tex.palette[tex.texels[j][i] * 4 + 1];
							newtexels[i * 4 + 2] = tex.palette[tex.texels[j][i] * 4 + 2];
							newtexels[i * 4 + 3] = tex.palette[tex.texels[j][i] * 4 + 3];
						}
						//delete[] texels[j];
						//texels[j] = newtexels;
						//tex.dataSizes[j] = dataSize;
					}
					//delete[] tex.palette;
					//tex.palette = 0;
					//tex.rasterFormat &= ~(RASTER_PAL4 | RASTER_PAL8);
					//tex.depth = 0x20;
				} else {
					memcpy(buffer + i * datasize, tn.texels[0], tn.dataSizes[0]);
				}
			}

			switch (tex.rasterFormat & RASTER_MASK) {
			case RASTER_1555: // Used by DXT1 w/ alpha
				assert(tex.dxtCompression == 1);
				{
					const GLenum format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
					const GLsizei size = layers * datasize;
					fwrite(&format, sizeof(GLenum), 1, blob);
					fwrite(&width, sizeof(GLsizei), 1, blob);
					fwrite(&height, sizeof(GLsizei), 1, blob);
					fwrite(&layers, sizeof(GLsizei), 1, blob);
					fwrite(&size, sizeof(GLsizei), 1, blob);
					fwrite_compressed(buffer, datasize, layers, blob);
					++texture_split_count;
					biggest_split_buffer = std::max(biggest_split_buffer, (uint32_t)size);
				}
				break;

			case RASTER_565: // Used for DXT1 w/o alpha
				assert(tex.dxtCompression == 1);
				{
					const GLenum format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
					const GLsizei size = layers * datasize;
					fwrite(&format, sizeof(GLenum), 1, blob);
					fwrite(&width, sizeof(GLsizei), 1, blob);
					fwrite(&height, sizeof(GLsizei), 1, blob);
					fwrite(&layers, sizeof(GLsizei), 1, blob);
					fwrite(&size, sizeof(GLsizei), 1, blob);
					fwrite_compressed(buffer, datasize, layers, blob);
					++texture_split_count;
					biggest_split_buffer = std::max(biggest_split_buffer, (uint32_t)size);
				}
				break;

			case RASTER_4444: // Used for DXT3 (has alpha)
				assert(tex.dxtCompression == 3);
				{
					const GLenum format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
					const GLsizei size = layers * datasize;
					fwrite(&format, sizeof(GLenum), 1, blob);
					fwrite(&width, sizeof(GLsizei), 1, blob);
					fwrite(&height, sizeof(GLsizei), 1, blob);
					fwrite(&layers, sizeof(GLsizei), 1, blob);
					fwrite(&size, sizeof(GLsizei), 1, blob);
					fwrite_compressed(buffer, datasize, layers, blob);
					++texture_split_count;
					biggest_split_buffer = std::max(biggest_split_buffer, (uint32_t)size);
				}
				break;

			case RASTER_888:
			case RASTER_8888:
				{
					const bool four_bytes = tex.hasAlpha || (tex.rasterFormat & (RASTER_PAL8 | RASTER_PAL4));
					const GLenum format = four_bytes ? GL_RGBA : GL_RGB;
					const GLsizei size = layers * datasize;
					fwrite(&format, sizeof(GLenum), 1, blob);
					fwrite(&width, sizeof(GLsizei), 1, blob);
					fwrite(&height, sizeof(GLsizei), 1, blob);
					fwrite(&layers, sizeof(GLsizei), 1, blob);
					fwrite(&size, sizeof(GLsizei), 1, blob);
					fwrite_compressed(buffer, datasize, layers, blob);
					++texture_split_count;
					biggest_split_buffer = std::max(biggest_split_buffer, (uint32_t)size);
				}
				break;

			default:
				assert(false && "Unsupported texture format!");
			}

			delete[] buffer;

			textures_left -= SLICE_SIZE;
		}
	}

	fseek(blob, 0, SEEK_SET);
	fwrite(&texture_split_count, sizeof(uint32_t), 1, blob); // patch number of texture splits
	fwrite(&biggest_split_buffer, sizeof(uint32_t), 1, blob); // patch the biggest split buffer size
	fclose(blob);
	return true;
}


static
size_t fsize(FILE *file)
{
	if (!file)
		return 0;
	size_t pos = ftell(file);
	fseek(file, 0, SEEK_END);
	size_t size = ftell(file);
	fseek(file, pos, SEEK_SET);
	return size;
}


bool add_item_definition_objs(ItemDefinitionEntry &item)
{
	// Check, if model name is valid
	if (item.model_name.length() == 0 || item.model_name == "")
		return false;

	// HACK: Skip LODs (distance greater than 900)
	if (item.draw_distance[0] >= 900)
		return false;
	if (starts_with(item.model_name, "lod") || starts_with(item.model_name, "LOD"))
		return false;

	// HACK: Don't load interiors
	//if (item.flags & IDFLAG_INTERIOR)
	//	return false;

	item.time_on = 0;
	item.time_off = 24;
	item_definitions[item.id] = item;
	ide_lookup[item.model_name] = item.id;
	dependent_dff.insert(item.model_name + ".dff");
	dependent_txd.insert(item.txd_name + ".txd");
	return true;
}


bool add_item_definition_tobj(ItemDefinitionEntry &item)
{
	// Check, if model name is valid
	if (item.model_name.length() == 0 || item.model_name == "")
		return false;

	// HACK: Skip LODs (distance greater than 900)
	if (item.draw_distance[0] >= 900)
		return false;
	if (starts_with(item.model_name, "lod") || starts_with(item.model_name, "LOD"))
		return false;

	// HACK: Don't load interiors
	//if (item.flags & IDFLAG_INTERIOR)
	//	return false;

	// Skip all meshes that are to visible at 12:00
	const int RENDER_HOUR = 12;
	bool visible = false;
	if (item.time_off > item.time_on)
		visible = (RENDER_HOUR >= item.time_on && RENDER_HOUR < item.time_off); // e.g. 5 - 20
	else
		visible = !(RENDER_HOUR >= item.time_off && RENDER_HOUR < item.time_on); // e.g. 20 - 5
	if (!visible)
		return false;

	item_definitions[item.id] = item;
	ide_lookup[item.model_name] = item.id;
	dependent_dff.insert(item.model_name + ".dff");
	dependent_txd.insert(item.txd_name + ".txd");
	return true;
}


//! Parse the OBJS section from given IDE file.
//! @returns Number of parsed entries
int parse_ide(const char *basename, const char *filename)
{
	const size_t BUF_SIZE = 256;
	char buf[BUF_SIZE] = {};
	int num_entries = 0;

	// Construct the filename and open the IDE file
	FILE *ide = fopen(filename, "r");

	// Locate the "objs" section
	while (NULL != fgets(buf, BUF_SIZE, ide))
		if (0 == strcmp("objs\n", buf))
			break;

	// Parse line by line (ignoring lines that begin with #) until "end"
	while (NULL != fgets(buf, BUF_SIZE, ide)) {
		if (0 == strcmp("end\n", buf))
			break;
		else if ('#' == buf[0])
			continue; // Naive way, but should work

		ItemDefinitionEntry item;
		char dff_name[24], txd_name[24];
		if (6 != sscanf(buf, "%i, %[^,], %[^,], %i, %f, %i\n", &item.id, dff_name, txd_name, &item.mesh_count, &item.draw_distance[0], &item.flags))  // Type 1
			if (7 != sscanf(buf, "%i, %[^,], %[^,], %i, %f, %f, %i\n", &item.id, dff_name, txd_name, &item.mesh_count, &item.draw_distance[0], &item.draw_distance[1], &item.flags))  // Type 2
				if (8 != sscanf(buf, "%i, %[^,], %[^,], %i, %f, %f, %f, %i\n", &item.id, dff_name, txd_name, &item.mesh_count, &item.draw_distance[0], &item.draw_distance[1], &item.draw_distance[2], &item.flags)) {
					fprintf(stderr, "ERROR: Cannot match OBJS as IDE type 1, 2 nor 3: '%s'. Skipping!\n", buf);
					continue;
				}

		// Add item definition
		item.model_name = dff_name;
		item.txd_name = txd_name;
		if (add_item_definition_objs(item))
			++num_entries;
	}

	// Locate the "tobj" section
	while (NULL != fgets(buf, BUF_SIZE, ide))
		if (0 == strcmp("tobj\n", buf))
			break;

	// Parse line by line (ignoring lines that begin with #) until "end"
	while (NULL != fgets(buf, BUF_SIZE, ide)) {
		if (0 == strcmp("end\n", buf))
			break;
		else if ('#' == buf[0])
			continue; // Naive way, but should work

		ItemDefinitionEntry item;
		char dff_name[24], txd_name[24];
		if (8 != sscanf(buf, "%i, %[^,], %[^,], %i, %f, %i, %i, %i\n", &item.id, dff_name, txd_name, &item.mesh_count, &item.draw_distance[0], &item.flags, &item.time_on, &item.time_off))	// Type 1
			if (9 != sscanf(buf, "%i, %[^,], %[^,], %i, %f, %f, %i, %i, %i\n", &item.id, dff_name, txd_name, &item.mesh_count, &item.draw_distance[0], &item.draw_distance[1], &item.flags, &item.time_on, &item.time_off))		// Type 2
				if (10 != sscanf(buf, "%i, %[^,], %[^,], %i, %f, %f, %f, %i, %i, %i\n", &item.id, dff_name, txd_name, &item.mesh_count, &item.draw_distance[0], &item.draw_distance[1], &item.draw_distance[2], &item.flags, &item.time_on, &item.time_off)) {
					fprintf(stderr, "ERROR: Cannot match TOBJ as IDE type 1, 2 nor 3: '%s'. Skipping!\n", buf);
					continue;
				}

		// Add item definition
		item.model_name = dff_name;
		item.txd_name = txd_name;
		if (add_item_definition_tobj(item))
			++num_entries;
	}

	// Cleanup
	fclose(ide);
	return num_entries;
}


//! Parse the INST section from given IPL file.
//! @returns Number of parsed entries
int parse_ipl(const char *basename)
{
	const size_t BUF_SIZE = 256;
	char buf[BUF_SIZE] = {};
	int num_entries = 0;

	// Construct the filename and open the IPL file
	sprintf(buf, "data/maps/%s/%s.ipl", basename, basename);
	FILE *ipl = fopen(buf, "r");

	// Locate the "inst" section
	while (NULL != fgets(buf, BUF_SIZE, ipl))
		if (0 == strcmp("inst\n", buf))
			break;

	// Parse line by line (ignoring lines that begin with #) until "end"
	while (NULL != fgets(buf, BUF_SIZE, ipl)) {
		if (0 == strcmp("end\n", buf))
			break;
		else if ('#' == buf[0])
			continue; // Naive way, but should work

		ItemPlacementEntry item = {};
		char dff_name[24];
		if (13 != sscanf(buf, "%i, %[^,], %i, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f\n", &item.id, dff_name, &item.interior,
				&item.position.x, &item.position.y, &item.position.z,
				&item.scale.x, &item.scale.y, &item.scale.z,
				&item.rotation.x, &item.rotation.y, &item.rotation.z, &item.rotation.w)) {
			if (12 != sscanf(buf, "%i, %[^,], %f, %f, %f, %f, %f, %f, %f, %f, %f, %f\n", &item.id, dff_name,
					&item.position.x, &item.position.y, &item.position.z,
					&item.scale.x, &item.scale.y, &item.scale.z,
					&item.rotation.x, &item.rotation.y, &item.rotation.z, &item.rotation.w))
				item.interior = 0;
			else {
				fprintf(stderr, "ERROR: Cannot parse as IPL line: '%s'. Skipping!\n", buf);
				continue;
			}
		}

		// Add item placement
		item.model_name = dff_name;
		if (ide_lookup.end() == ide_lookup.find(item.model_name)) {
			fprintf(stderr, "WARNING: Item placement entry references UNLOADED DFF! IPL=%s DFF=%s\n", basename, item.model_name.c_str());
			continue;
		}

		// WE ARE INTERESTED ONLY IN INTERIOR 0 AND 13
		if (0 != item.interior && 13 != item.interior)
			continue;

		item.rotation = glm::normalize(item.rotation);
		glm::vec3 ea = glm::eulerAngles(item.rotation);
		glm::mat4 transformX = glm::rotate(glm::mat4(1.0f), ea.x, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::mat4 transformY = glm::rotate(glm::mat4(1.0f), ea.y, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 transformZ = glm::rotate(glm::mat4(1.0f), -ea.z, glm::vec3(0.0f, 0.0f, 1.0f));
		glm::mat4 rotation = transformX * transformY * transformZ;
		item.world_from_object = glm::translate(glm::mat4(1.0f), item.position) * rotation; // *glm::scale(glm::mat4(1.0f), item.scale);
		item_placements[item.id].push_back(item);
		ide_lookup[item.model_name] = item.id;
		//dependent_dff.insert(item.model_name + ".dff");
		++num_entries;
	}

	// Cleanup
	fclose(ipl);
	return num_entries;
}


int copy_file(const char *in_file, const char *out_file)
{
	char buf[2048];
	FILE *src_file = fopen(in_file, "rb");
	if (!src_file) return -1;
	const size_t size = fsize(src_file);
	FILE *dst_file = fopen(out_file, "wb");
	for (size_t bytes_left = size; 0 < bytes_left; ) {
		size_t num_bytes = std::min(2048U, bytes_left);
		fread(buf, 1, num_bytes, src_file);
		fwrite(buf, 1, num_bytes, dst_file);
		bytes_left -= num_bytes;
	}
	fclose(dst_file);
	fclose(src_file);
	return 1;
}


int extract_file(FILE *img, const std::vector<DirectoryEntry> &dir, const char *filename, const char *out_filename)
{
	fprintf(stderr, "Extracting '%s' to '%s'...\n", filename, out_filename);

	// HACK: "Generic.txd" is not located in IMG archive so we simply copy it...
	if (0 == stricmp(filename, "generic.txd"))
		return copy_file("models/generic.txd", out_filename);

	// Locate the file in directory
	DirectoryEntry entry = {};
	bool found = false;
	for (const DirectoryEntry &de : dir)
		if (found = (0 == strcmp((const char *)de.name, filename))) {
			entry = de;
			break;
		}

	if (!found) {
		fprintf(stderr, "ERROR: Failed to locate '%s' in IMG archive!\n", filename);
		return 0;
	}

	int offset = 2048 * entry.offset;
	int size = 2048 * entry.size;
	fseek(img, offset, SEEK_SET);

	char sector[2048];
	FILE *out = fopen(out_filename, "wb");
	if (NULL == out)
		return 0;
	for (uint32_t i = 0; i < entry.size; ++i) {
		fread(sector, 2048, 1, img);
		fwrite(sector, 2048, 1, out);
	}
	fclose(out);
	return size;
}


int extract_img()
{
	std::vector<DirectoryEntry> img_files;

	// Load IMG "table of contents" file
	FILE *dir = fopen("models/gta3.dir", "rb");
	size_t num_entries = fsize(dir) / 32;
	img_files.resize(num_entries);
	fread(img_files.data(), 32, num_entries, dir);
	fclose(dir);

	FILE *img = fopen("models/gta3.img", "rb");
	for (const std::string &filename : dependent_dff) {
		extract_file(img, img_files, filename.c_str(), ("_extracted/" + filename).c_str());
	}
	for (const std::string &filename : dependent_txd) {
		extract_file(img, img_files, filename.c_str(), ("_extracted/" + filename).c_str());
	}
	fclose(img);
	return 0;
}


int main(int argc, char *argv[])
{
	const char *SECTORS[] = {
		"oceandrv",
		"oceandn",
		"washints",
		"washintn",
		"nbeachbt",
		"nbeach",
		"nbeachw",
		"mall",

		"littleha",
		"downtown",
		"downtows",
		"docks",
		"airport",
		"airportN",
		"haiti",
		"haitin",

		"islandsf",
		"golf",
		"bridge",
		"starisl",
		"mansion",
		"cisland"
	};

	if (!parse_ide("generic", "data/maps/generic.ide"))
		return 1;

#if 0 // Extract only
	for (int i = 0; i < 22; ++i) {
		char buf[256] = {};
		const char *basename = SECTORS[i];
		sprintf(buf, "data/maps/%s/%s.ide", basename, basename);

		if (parse_ide(basename, buf))	// Parse IDE file
			if (parse_ipl(basename))	// Parse IPL file
				if (extract_img(std::string(basename)))		// and extract files from IMG archive
					return 0;
	}
#else
	for (int i = 0; i < 22; ++i) {
		char buf[256] = {};
		const char *basename = SECTORS[i];
		sprintf(buf, "data/maps/%s/%s.ide", basename, basename);

		if (!parse_ide(basename, buf))	// Parse IDE file
			return 1;
		if (!parse_ipl(basename))	// Parse IPL file
			return 2;
	}

	// Extract DFF and TXD files from IMG archive
	extract_img();

	// Load all referenced DFFs
	for (const std::string &filename : dependent_dff) {
		const std::string mesh_name = filename.substr(0, filename.length() - 4); // cut the ".dff"
		if (!read_dff_mesh(ide_lookup[mesh_name], "_extracted/" + filename)) {
			fprintf(stderr, "ERROR: Failed to load DFF: '%s'\n", filename.c_str());
			return 4;
		}
	}

	// Load referenced TXD
	for (const std::string &filename : dependent_txd) {
		if (!read_txd("_extracted/" + filename)) {
			fprintf(stderr, "ERROR: Failed to load TXD: '%s'\n", filename.c_str());
			return 5;
		}
	}
	fprintf(stderr, "INFO: LOADING COMPLETED!\n");
	upload_meshes();
	fprintf(stderr, "INFO: BUFFER UPLOAD COMPLETE!\n");
	upload_textures();
	fprintf(stderr, "INFO: TEXTURE UPLOAD COMPLETE!\n");

	// Batch draw calls
	std::vector<Instance> instances;
	{
		// Start with filling instance buffer
		std::vector<glm::mat4> xforms;
		for (const auto &pair : item_placements) {
			Instance instance = {};
			instance.id = pair.first;
			instance.num_instances = pair.second.size();
			instance.base_instance = xforms.size();
			instances.push_back(instance);

			for (const ItemPlacementEntry &ipl : pair.second)
				xforms.push_back(ipl.world_from_object);
		}

		// Write all instance matrices to "instances.blob"
		FILE *blob = fopen("instances.blob", "wb");
		uint32_t num_instances = xforms.size();
		fwrite(&num_instances, sizeof(uint32_t), 1, blob);
		fwrite_compressed(xforms.data(), sizeof(glm::mat4), xforms.size(), blob);
		fclose(blob);
	}
	// Then sort the draw calls to reduce state switches and enable instancing
	for (const Instance &instance : instances) {
		const ItemDefinitionEntry &def = item_definitions[instance.id]; // The Item Definition of object that will be drawn
		const std::vector<ItemPlacementEntry> &instances = item_placements[instance.id];
		const std::vector<MaterialSplit> &mesh_splits = material_splits[instance.id];
		const MeshTableEntry &mesh = mesh_table[instance.id];

		int offset = 0;
		for (size_t mat_split_idx = 0; mat_split_idx < mesh_splits.size(); ++mat_split_idx) {
			const MaterialSplit &batch = mesh_splits[mat_split_idx];

			// skip not present materials / textures
			if (named_textures.end() == named_textures.find(batch.mat_name))
				continue;
			const TextureRef &ref = named_textures[batch.mat_name];
			if (texture_buckets.end() == texture_buckets.find(ref.bucket_key))
				continue;
			const TextureBucket &bucket = texture_buckets[ref.bucket_key];

			uint16_t slice_index = ref.index / MAX_ARRAY_TEXTURE_LAYERS; // Index to the slice (array texture) containg the texture to render
			uint16_t texture_id = ref.index % MAX_ARRAY_TEXTURE_LAYERS; // The index of the texture to render within the slice (texture array) => gl_DrawId / u_TempTextureIdx

			// TTTT TTTTTTTT SSSSSSSS ---IIIII IIIIIIII -MMMMMMM
			uint64_t sort_key = 0
				| (((uint64_t)ref.bucket_key & 0xFFF) << 32)  // 12-bit bucket key
				| (((uint64_t)slice_index & 0xFF) << 24)  // 8-bit slice key (texture array within bucket)
				| (((uint64_t)def.id & 0xFFF) << 8)  // Item definition ID
				| ((uint64_t)mat_split_idx & 0xFF)  // Mesh/Material split index
				;

			DrawCall &dc = ordered_draw_calls[sort_key];
			dc.texture_array = bucket.tex_array[slice_index];
			dc.tex_index = texture_id;
			dc.num_instances = instance.num_instances;
			dc.base_instance = instance.base_instance;

			dc.num_vertices = batch.num_indices;
			dc.base_vertex = mesh.base_vertex;
			dc.index_offset = mesh.offset + offset;

			offset += batch.num_indices * sizeof(uint16_t);
		}
	}

	// Write all ordered draw calls to "drawables.blob"
	FILE *blob = fopen("drawables.blob", "wb");
	uint32_t num_draw_calls = ordered_draw_calls.size();
	fwrite(&num_draw_calls, sizeof(uint32_t), 1, blob);

	#if defined(BAKE_WITH_LZHAM)
		{
			uint8_t *buffer = (uint8_t*)calloc(num_draw_calls, sizeof(DrawCall));
			size_t offset = 0;

			// Draw call's keys are needed for detecting state changes during batching
			for (const auto &pair : ordered_draw_calls) {
				memcpy(buffer + offset, &pair.first, sizeof(uint64_t));
				offset += sizeof(uint64_t);
			}
			fwrite_compressed(buffer, sizeof(uint64_t), num_draw_calls, blob);

			offset = 0;
			for (const auto &pair : ordered_draw_calls) {
				// FIXME: How to serialize OpenGL texture name? This approach might work only when there are no array texture slices
				memcpy(buffer + offset, &pair.second, sizeof(DrawCall));
				offset += sizeof(DrawCall);
			}
			fwrite_compressed(buffer, sizeof(DrawCall), num_draw_calls, blob);

			free(buffer);
		}
	#else
		// Draw call's keys are needed for detecting state changes during batching
		for (const auto &pair : ordered_draw_calls)
			fwrite(&pair.first, sizeof(uint64_t), 1, blob);

		for (const auto &pair : ordered_draw_calls) {
			// FIXME: How to serialize OpenGL texture name? This approach might work only when there are no array texture slices
			fwrite(&pair.second, sizeof(DrawCall), 1, blob);
		}
	#endif
	fclose(blob);
#endif
	return 0;
}
