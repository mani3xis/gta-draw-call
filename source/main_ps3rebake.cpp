/*
 * Endianness conversion utility for baked `blob` files.
 * This application has been created to convert PC blobs to a more PS3-friendly format.
 *
 * NOTE: For now PS3 does not support compressed images.
 */
#include <stdint.h>  // uint*_t
#include <stdlib.h>  // NULL, malloc, free
#include <stdio.h>  // fprintf, printf, fopen, fwrite, fread, fclose, fseek, ftell
#include <string.h>  // memcpy
#include <assert.h>  // assert

#define TRANSPOSE_MATRICES	// column-major  <---> row-major
#define CONVERT_RGB_TO_RGBA

#define SWAP_ENDIANNESS_8BYTES(x) ( ((x & 0x00000000000000FF) << 56) \
                                  | ((x & 0x000000000000FF00) << 40) \
                                  | ((x & 0x0000000000FF0000) << 24) \
                                  | ((x & 0x00000000FF000000) << 8)  \
                                  | ((x & 0x000000FF00000000) >> 8)  \
                                  | ((x & 0x0000FF0000000000) >> 24) \
                                  | ((x & 0x00FF000000000000) >> 40) \
                                  | ((x & 0xFF00000000000000) >> 56) )

#define SWAP_ENDIANNESS_4BYTES(x) ( ((x & 0x000000FF) << 24) \
                                  | ((x & 0x0000FF00) << 8)  \
                                  | ((x & 0x00FF0000) >> 8)  \
                                  | ((x & 0xFF000000) >> 24) )

#define SWAP_ENDIANNESS_2BYTES(x) ( ((x & 0x00FF) << 8) \
                                  | ((x & 0xFF00) >> 8) )


//! Those texture formats match GL formats
enum TextureFormat {
	GL_RGB = 0x1907,
	GL_RGBA = 0x1908,
	GL_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0,
	GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1,
	GL_COMPRESSED_RGBA_S3TC_DXT3_EXT = 0x83F2,
	GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3
};

struct DrawCall {
	uint32_t texture_array;
	uint32_t tex_index;
	uint32_t index_offset;
	uint32_t num_vertices;
	uint32_t base_vertex;
	uint32_t num_instances;
	uint32_t base_instance;
};

struct TextureBucketData {
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t layers;
	uint32_t size;
};


//! Get texture format name for logging purposes.
static const
char *get_format_name(uint32_t fmt)
{
	switch (fmt) {
		case GL_RGB: return "RGB";
		case GL_RGBA: return "RGBA";
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT: return "RGB_DXT1";
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: return "RGBA_DXT1";
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: return "RGBA_DXT3";
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return "RGBA_DXT5";
	}
	return "UNKNOWN";
}


//! Function for rebaking "texturebuckets.blob" files.
int rebake_textures(const char *out_filename, const char *in_filename)
{
	FILE *in_blob = fopen(in_filename, "rb");
	if (!in_blob)
		return -1; // Couldn't open input file for reading

	FILE *out_blob = fopen(out_filename, "wb");
	if (!out_blob) {
		fclose(in_blob);
		return -2; // Couldn't open output file for writing
	}

	// Read the file header
	uint32_t num_texture_splits = 0;
	uint32_t biggest_split_buffer = 0;
	fread(&num_texture_splits, sizeof(num_texture_splits), 1, in_blob);
	fread(&biggest_split_buffer, sizeof(biggest_split_buffer), 1, in_blob);
	printf("VERBOSE: num_texture_splits=%u\n", num_texture_splits);
	printf("VERBOSE: biggest_split_buffer=%u\n", biggest_split_buffer);
	const uint32_t num_texture_splits2 = SWAP_ENDIANNESS_4BYTES(num_texture_splits);
	uint32_t biggest_split_buffer2 = SWAP_ENDIANNESS_4BYTES(biggest_split_buffer);
	fwrite(&num_texture_splits2, sizeof(num_texture_splits2), 1, out_blob);
	fwrite(&biggest_split_buffer2, sizeof(biggest_split_buffer2), 1, out_blob);
	const size_t biggest_split_offset = ftell(out_blob) - sizeof(biggest_split_buffer2); // this is needed just in case when transcoded texture is bigger

	void *texture_storage_ptr = malloc(2ULL * (size_t)biggest_split_buffer);
	assert(NULL != texture_storage_ptr);

	// Read texture split header
	TextureBucketData tb = {};
	for (uint32_t i=0; i < num_texture_splits; i++) {
		fread(&tb.format, sizeof(uint32_t), 1, in_blob);
		fread(&tb.width, sizeof(uint32_t), 1, in_blob);
		fread(&tb.height, sizeof(uint32_t), 1, in_blob);
		fread(&tb.layers, sizeof(uint32_t), 1, in_blob);
		fread(&tb.size, sizeof(uint32_t), 1, in_blob);
		printf("VERBOSE: SPLIT[%u]: format=0x%x [%s], width=%u, height=%u, layers=%u, size=%u\n",
			i, tb.format, get_format_name(tb.format), tb.width, tb.height, tb.layers, tb.size);
		const size_t orig_size = (size_t)tb.size; // Keep a copy of size before endianness conversion since its used after conversion!

#ifdef CONVERT_RGB_TO_RGBA
		const bool transcode = (GL_RGB == tb.format);
		if (transcode) {
			tb.format = GL_RGBA;
			assert(4 * tb.width * tb.height == tb.size);
			tb.size = 4 * tb.width * tb.height;
			if (tb.size > biggest_split_buffer) {
				// patch the size!
				biggest_split_buffer2 = SWAP_ENDIANNESS_4BYTES(tb.size);
				const size_t cur_pos = ftell(out_blob);
				fseek(out_blob, SEEK_SET, biggest_split_offset);
				fwrite(&biggest_split_buffer2, sizeof(biggest_split_buffer2), 1, out_blob);
				fseek(out_blob, SEEK_SET, cur_pos);
			}
		}
#endif

		tb.format = SWAP_ENDIANNESS_4BYTES(tb.format);
		tb.width = SWAP_ENDIANNESS_4BYTES(tb.width);
		tb.height = SWAP_ENDIANNESS_4BYTES(tb.height);
		tb.layers = SWAP_ENDIANNESS_4BYTES(tb.layers);
		tb.size = SWAP_ENDIANNESS_4BYTES(tb.size);
		fwrite(&tb.format, sizeof(uint32_t), 1, out_blob);
		fwrite(&tb.width, sizeof(uint32_t), 1, out_blob);
		fwrite(&tb.height, sizeof(uint32_t), 1, out_blob);
		fwrite(&tb.layers, sizeof(uint32_t), 1, out_blob);
		fwrite(&tb.size, sizeof(uint32_t), 1, out_blob);

#ifdef CONVERT_RGB_TO_RGBA
		if (transcode) {
			const size_t num_pixels = tb.width * tb.height;
			for (size_t l=0; l < tb.layers; l++) {
				for (size_t j=0; j < num_pixels; j++) {
					uint8_t rgba[4];
					fread(rgba, sizeof(uint8_t), 3, in_blob);
					fwrite(&rgba[3], sizeof(uint8_t), 1, out_blob); // A
					fwrite(&rgba[2], sizeof(uint8_t), 1, out_blob); // B
					fwrite(&rgba[1], sizeof(uint8_t), 1, out_blob); // G
					fwrite(&rgba[0], sizeof(uint8_t), 1, out_blob); // R
				}
			}
		} else {
#endif
		// Read texture data to allocated memory
		const size_t bytes_read = fread(texture_storage_ptr, 1, (size_t)orig_size, in_blob);
		printf("INFO: Processed %u bytes of %u\n", bytes_read, orig_size);
		fwrite(texture_storage_ptr, 1, (size_t)orig_size, out_blob);

#ifdef CONVERT_RGB_TO_RGBA
		}
#endif
	}

	free(texture_storage_ptr);
	fclose(in_blob);
	fclose(out_blob);
	return 0;
}


//! Function for rebaking "meshes.blob" files.
int rebake_meshes(const char *out_filename, const char *in_filename)
{
	FILE *in_blob = fopen(in_filename, "rb");
	if (!in_blob)
		return -1; // Couldn't open input file for reading

	FILE *out_blob = fopen(out_filename, "wb");
	if (!out_blob) {
		fclose(in_blob);
		return -2; // Couldn't open output file for writing
	}

	// Read file header
	uint32_t num_vertices = 0;
	uint32_t num_indices = 0;
	fread(&num_vertices, sizeof(uint32_t), 1, in_blob);
	fread(&num_indices, sizeof(uint32_t), 1, in_blob);
	printf("VERBOSE: num_vertices=%u, num_indices=%u\n", num_vertices, num_indices);
	const uint32_t num_vertices2 = SWAP_ENDIANNESS_4BYTES(num_vertices);
	const uint32_t num_indices2 = SWAP_ENDIANNESS_4BYTES(num_indices);
	fwrite(&num_vertices2, sizeof(uint32_t), 1, out_blob);
	fwrite(&num_indices2, sizeof(uint32_t), 1, out_blob);

	// Load index buffer (sizeof(uint16_t) * num_indices) bytes
	for (size_t i = 0; i < num_indices; i++) {
		uint16_t index = 0xFFFF;
		fread(&index, sizeof(uint16_t), 1, in_blob);
		index = SWAP_ENDIANNESS_2BYTES(index);
		fwrite(&index, sizeof(uint16_t), 1, out_blob);
	}

	typedef union { float f; uint32_t i; } ucast;

	// Load vertex positions (3*sizeof(float) * num_vertices) bytes
	printf("INFO: Processing vertex positions...\n");
	for (size_t v = 0; v < num_vertices; v++) {
		ucast x, y, z;
		fread(&x.f, sizeof(float), 1, in_blob);
		fread(&y.f, sizeof(float), 1, in_blob);
		fread(&z.f, sizeof(float), 1, in_blob);
		x.i = SWAP_ENDIANNESS_4BYTES(x.i);
		y.i = SWAP_ENDIANNESS_4BYTES(y.i);
		z.i = SWAP_ENDIANNESS_4BYTES(z.i);
		fwrite(&x.f, sizeof(float), 1, out_blob);
		fwrite(&y.f, sizeof(float), 1, out_blob);
		fwrite(&z.f, sizeof(float), 1, out_blob);
	}

	// Load vertex colors (4*sizeof(uint8_t) * num_vertices) bytes
	printf("INFO: Processing vertex colors...\n");
	for (size_t v = 0; v < num_vertices; v++) {
		uint32_t rgba;
		fread(&rgba, sizeof(uint32_t), 1, in_blob);
		rgba = SWAP_ENDIANNESS_4BYTES(rgba);
		fwrite(&rgba, sizeof(uint32_t), 1, out_blob);
	}

	// Load vertex UV's (2*sizeof(float) * num_vertices) bytes
	printf("INFO: Processing vertex UVs...\n");
	for (size_t v = 0; v < num_vertices; v++) {
		ucast x, y, z, w;
		fread(&x.f, sizeof(float), 1, in_blob);
		fread(&y.f, sizeof(float), 1, in_blob);
		fread(&z.f, sizeof(float), 1, in_blob);
		fread(&w.f, sizeof(float), 1, in_blob);
		x.i = SWAP_ENDIANNESS_4BYTES(x.i);
		y.i = SWAP_ENDIANNESS_4BYTES(y.i);
		z.i = SWAP_ENDIANNESS_4BYTES(z.i);
		w.i = SWAP_ENDIANNESS_4BYTES(w.i);
		fwrite(&x.f, sizeof(float), 1, out_blob);
		fwrite(&y.f, sizeof(float), 1, out_blob);
		fwrite(&z.f, sizeof(float), 1, out_blob);
		fwrite(&w.f, sizeof(float), 1, out_blob);
	}

	fclose(in_blob);
	fclose(out_blob);
	printf("INFO: Finished processing meshes\n");
	return 0;
}


//! Function for rebaking "instances.blob" files.
int rebake_instances(const char *out_filename, const char *in_filename)
{
	FILE *in_blob = fopen(in_filename, "rb");
	if (!in_blob)
		return -1; // Couldn't open input file for reading

	FILE *out_blob = fopen(out_filename, "wb");
	if (!out_blob) {
		fclose(in_blob);
		return -2; // Couldn't open output file for writing
	}

	// Read file header
	uint32_t num_instances = 0;
	fread(&num_instances, sizeof(num_instances), 1, in_blob);
	printf("VERBOSE: num_instances=%u\n", num_instances);
	uint32_t num_instances2 = SWAP_ENDIANNESS_4BYTES(num_instances);
	fwrite(&num_instances2, sizeof(num_instances2), 1, out_blob);

	typedef union { float f; uint32_t i; } ucast;
	ucast temp[16];
	for (uint32_t i=0; i < num_instances; i++) {
		for (uint32_t j=0; j < 16; j++) {
			fread(&temp[j].f, sizeof(float), 1, in_blob);
			temp[j].i = SWAP_ENDIANNESS_4BYTES(temp[j].i);
		}

#ifdef TRANSPOSE_MATRICES
		// transpose the model matrix...
		ucast temp2[16];
		for (uint32_t r=0; r < 4; r++) {
			for (uint32_t c=0; c < 4; c++)
				temp2[4*r + c].f = temp[4*c + r].f;
		}
		memcpy(temp, temp2, 16 * sizeof(ucast));
#endif

		for (uint32_t j=0; j < 16; j++)
			fwrite(&temp[j].f, sizeof(float), 1, out_blob);
	}

	fclose(in_blob);
	fclose(out_blob);
	printf("INFO: Finished processing instances\n");
	return 0;
}


//! Function for rebaking "drawables.blob" files.
int rebake_drawables(const char *out_filename, const char *in_filename)
{
	FILE *in_blob = fopen(in_filename, "rb");
	if (!in_blob)
		return -1; // Couldn't open input file for reading

	FILE *out_blob = fopen(out_filename, "wb");
	if (!out_blob) {
		fclose(in_blob);
		return -2; // Couldn't open output file for writing
	}

	// Read file header
	uint32_t num_draw_calls = 0;
	fread(&num_draw_calls, sizeof(num_draw_calls), 1, in_blob);
	printf("VERBOSE: num_draw_calls=%u\n", num_draw_calls);
	uint32_t num_draw_calls2 = SWAP_ENDIANNESS_4BYTES(num_draw_calls);
	fwrite(&num_draw_calls2, sizeof(num_draw_calls2), 1, out_blob);

	// Read sort keys
	for (uint32_t i = 0; i < num_draw_calls; i++) {
		uint64_t key = 0xFFFFFFFF;
		fread(&key, sizeof(uint64_t), 1, in_blob);
		key = SWAP_ENDIANNESS_8BYTES(key);
		fwrite(&key, sizeof(uint64_t), 1, out_blob);
	}

	for (uint32_t i = 0; i < num_draw_calls; i++) {
		DrawCall dc;
		fread(&dc, sizeof(DrawCall), 1, in_blob);

		// Swap endianness
		dc.texture_array = SWAP_ENDIANNESS_4BYTES(dc.texture_array);
		dc.tex_index = SWAP_ENDIANNESS_4BYTES(dc.tex_index);
		dc.index_offset = SWAP_ENDIANNESS_4BYTES(dc.index_offset);
		dc.num_vertices = SWAP_ENDIANNESS_4BYTES(dc.num_vertices);
		dc.base_vertex = SWAP_ENDIANNESS_4BYTES(dc.base_vertex);
		dc.num_instances = SWAP_ENDIANNESS_4BYTES(dc.num_instances);
		dc.base_instance = SWAP_ENDIANNESS_4BYTES(dc.base_instance);

		fwrite(&dc, sizeof(DrawCall), 1, out_blob);
	}

	fclose(in_blob);
	fclose(out_blob);
	printf("INFO: Finished processing %u drawables\n", num_draw_calls);
	return 0;
}


int main(int argc, char *argv[])
{
	int status = 0;

	status = rebake_textures("texturebuckets.ps3.blob", "texturebuckets.blob");
	if (0 != status) {
		fprintf(stderr, "ERROR: 'texturebuckets.blob' conversion failed with status=%i\r\n", status);
		return 1;
	}

	status = rebake_meshes("meshes.ps3.blob", "meshes.blob");
	if (0 != status) {
		fprintf(stderr, "ERROR: 'meshes.blob' conversion failed with status=%i\r\n", status);
		return 2;
	}

	status = rebake_instances("instances.ps3.blob", "instances.blob");
	if (0 != status) {
		fprintf(stderr, "ERROR: 'instances.blob' conversion failed with status=%i\r\n", status);
		return 3;
	}

	status = rebake_drawables("drawables.ps3.blob", "drawables.blob");
	if (0 != status) {
		fprintf(stderr, "ERROR: 'drawables.blob' conversion failed with status=%i\r\n", status);
		return 4;
	}
	return 0;
}
