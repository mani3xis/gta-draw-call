/*
 * Simple utility program for rendering baked Vice City.
 */
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // glm::perspective
#include <glm/gtc/type_ptr.hpp>
#include <map>

#include <SDL.h>
#include <GL/glew.h>
#include "shaders.h"


extern void start_opengl_log(const char *filename);
extern void stop_opengl_log();
extern GLuint compile_glsl_source(GLenum type, char *source);
extern GLuint link_glsl(GLuint vertex_shader, GLuint fragment_shader);


#if defined(BAKED_WITH_LZHAM)
	// LZHAM_DEFINE_ZLIB_API causes lzham.h to remap the standard zlib.h functions/macro definitions to lzham's.
	// This is totally optional - you can also directly use the lzham_* functions and macros instead.
	#define LZHAM_DEFINE_ZLIB_API
	#include <lzham_static_lib.h>
#endif

//! Handy function for reading continuous data blocks from standard FILE streams.
//! I use this helper function for reading blocks that COULD be compressed.
size_t fread_compressed(void *data, size_t element_size, size_t element_count, FILE *file)
{
#if defined(BAKE_WITH_LZHAM)
	// Read compressed data
	uLong compressed_bytes = 0;
	fread(&compressed_bytes, sizeof(uLong), 1, file);
	uint8_t *buffer = (uint8_t *)malloc(compressed_bytes);
	size_t bytes_read = fread(buffer, sizeof(uint8_t), compressed_bytes, file);
	assert(bytes_read == compressed_bytes); // Make sure that we loaded every compressed byte

	// Decompress the data using LZHAM
	uLong uncompressed_bytes = element_size * element_count;
	int status = uncompress((uint8_t *)data, &uncompressed_bytes, (const uint8_t *)buffer, bytes_read);
	if (Z_OK != status)
		assert(false && "Decompression failed!");
	assert(uncompressed_bytes == element_size * element_count);

	free(buffer);
	return uncompressed_bytes;
#else
	// Read uncompressed data
	return element_size * fread(data, element_size, element_count, file);
#endif
}

#define GL_CHECK() do { \
	GLenum err; \
	while (GL_NO_ERROR != (err = glGetError())) { \
		printf("TRAP: OpenGL error %i at %s:%i: %s\n", err, __FILE__, __LINE__, glewGetErrorString(err)); \
		SDL_assert(false); \
	} } while (false);


enum VertexAttribute {
	ATTRIB_POSITION = 0,
	ATTRIB_NORMAL = 1,
	ATTRIB_COLOR = 2,
	ATTRIB_TEXCOORD = 3,

	// Instanced attributes
	ATTRIB_WORLD_MATRIX = 12
};


//! Contains all data required by a single instanced draw call.
//! This structure is directly read from "drawables.blob" image.
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

//! Represents single indirect draw call.
//! The layout, padding and alignment are specified by OpenGL specs.
struct DrawElementsIndirectCommand {
	GLuint count;
	GLuint instance_count;
	GLuint first_index;
	GLuint base_vertex;
	GLuint base_instance;
};

struct MultiDrawCall {
	GLuint indirect_buffer;
	uint32_t indirect_offset;
	uint32_t indirect_count;
	GLuint tex_array;
	GLuint texid_offset;
};
std::vector<MultiDrawCall> multicalls;



static const glm::vec3 LOOK_DIR(0.0f, -1.0f, 0.0f);
static GLint MAX_ARRAY_TEXTURE_LAYERS = 2048;
static GLuint baked_buffers[4];
static GLuint baked_vao;
static GLuint instance_buffer;
static GLuint indirect_buffer;
static GLuint texid_buffer;
static GLuint texhandle_buffer;
static SDL_Window *wnd;
static int draw_call_counter = 0;
static int window_width = 800, window_height = 600;
static glm::mat4 proj_mat;
static glm::mat4 view_proj;
static glm::vec3 cam_pos(256.0f, -1265.0f, 15.0f);
static float cam_yaw = 0.0f;
static float cam_pitch = 0.0f;
static GLint WORLD_MATRIX_UNIFORM;
static GLint VIEW_PROJ_MATRIX_UNIFORM;
static GLint TEXTURE_0_UNIFORM;
static GLint TEMP_TEX_IDX_UNIFORM;
std::vector<GLuint> textures;
static std::vector<GLuint64> tex_handles; // bindless texture handles
static bool has_multi_draw_indirect;
static bool has_bindless_textures;
static bool has_shader_draw_params;


int init_renderer()
{
	// Create rendering window
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG | SDL_GL_CONTEXT_DEBUG_FLAG);
	wnd = SDL_CreateWindow("mani3xis' Vice City Renderer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		window_width, window_height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

	// Create and intialize OpenGL context
	SDL_GLContext ctx = SDL_GL_CreateContext(wnd);
	glewExperimental = GL_TRUE; // HACK: This has to be `true`, otherwise NVIDIA crashes on glGenVertexArrays() in core profile
	glewInit();
	if (-1 == SDL_GL_SetSwapInterval(-1))
		SDL_GL_SetSwapInterval(1);

	// Print which supported OpenGL extensions are available, since their presence specifies which code path will be taken.
	// Ever heard about "extension hell"? It's OpenGL version of "DLL hell", sorta...
	// My AMD Radeon HD 6950 doesn't support the last 2 extensions, so they are not perfectly tested.
	has_multi_draw_indirect = SDL_GL_ExtensionSupported("GL_ARB_multi_draw_indirect");
	has_bindless_textures = SDL_GL_ExtensionSupported("GL_ARB_bindless_texture");
	has_shader_draw_params = SDL_GL_ExtensionSupported("GL_ARB_shader_draw_parameters");
	printf("GL_ARB_multi_draw_indirect: %s\n", has_multi_draw_indirect ? "yes" : "no");
	printf("GL_ARB_bindless_texture: %s\n", has_bindless_textures ? "yes" : "no");
	printf("GL_ARB_shader_draw_parameters: %s\n", has_shader_draw_params ? "yes" : "no");

	// glewInit() generates OpenGL errors, so we have to manually clean the error flags
	while (GL_NO_ERROR != glGetError()) {};

	start_opengl_log("opengl-log.csv");
	glClearColor(0.341f, 0.498f, 0.738f, 1.0f); // HACK: Clear to sky blue (which I sampled from a random photograph)
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_CHECK();

	return 0;
}


int load_content()
{
	FILE *blob = NULL;
	uint8_t *buffer = NULL;
	size_t bytes_read = 0;

	{ // Load texture array splits from "texturebuckets.blob"
		blob = fopen("texturebuckets.blob", "rb");
		uint32_t num_texture_splits = 0;
		uint32_t biggest_split_buffer = 0;
		fread(&num_texture_splits, sizeof(uint32_t), 1, blob);
		fread(&biggest_split_buffer, sizeof(uint32_t), 1, blob);

		textures.reserve(num_texture_splits);
		tex_handles.push_back(0); // The first is reserved!

		buffer = (uint8_t *)malloc(biggest_split_buffer);
		for (uint32_t i = 0; i < num_texture_splits; ++i) {
			GLuint texture = 0;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
			textures.push_back(texture);

			GLenum format = GL_INVALID_ENUM;
			GLsizei width = 0, height = 0, layers = 0, size = 0;
			fread(&format, sizeof(GLenum), 1, blob);
			fread(&width, sizeof(GLsizei), 1, blob);
			fread(&height, sizeof(GLsizei), 1, blob);
			fread(&layers, sizeof(GLsizei), 1, blob);
			fread(&size, sizeof(GLsizei), 1, blob); // size = layers * tex.dataSizes[0]
			fread_compressed(buffer, 1, size, blob);

			if (GL_RGBA == format || GL_RGB == format)
				// Handle not compressed textures
				glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, format, width, height, layers, 0, format, GL_UNSIGNED_BYTE, buffer);
			else
				// Handle (DXT) compressed textures
				glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, 0, format, width, height, layers, 0, size, buffer);

			//glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			GL_CHECK();

			if (has_bindless_textures) {
				GLuint64 bindless_handle = glGetTextureHandleARB(texture);
				glMakeTextureHandleResidentARB(bindless_handle);
				tex_handles.push_back(bindless_handle);
				GL_CHECK();
			}
		}
		free(buffer);
		fclose(blob);
	}

	{ // Load VBOs and IBO from "meshes.blob"
		glGenVertexArrays(1, &baked_vao);
		glBindVertexArray(baked_vao);
		glGenBuffers(4, baked_buffers);

		blob = fopen("meshes.blob", "rb");
		uint32_t num_vertices = 0;
		uint32_t num_indices = 0;
		fread(&num_vertices, sizeof(uint32_t), 1, blob);
		fread(&num_indices, sizeof(uint32_t), 1, blob);
		buffer = (uint8_t *)malloc(std::max(num_indices * sizeof(uint16_t), num_vertices * sizeof(glm::vec4)));

		// Upload indices
		fread_compressed(buffer, sizeof(uint16_t), num_indices, blob);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, baked_buffers[0]);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * num_indices, buffer, GL_STATIC_DRAW);

		// Upload vertex positions
		fread_compressed(buffer, sizeof(glm::vec3), num_vertices, blob);
		glBindBuffer(GL_ARRAY_BUFFER, baked_buffers[1]);
		glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * num_vertices, buffer, GL_STATIC_DRAW);
		glVertexAttribPointer(ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), NULL);
		glEnableVertexAttribArray(ATTRIB_POSITION);

		// Upload vertex colors
		fread_compressed(buffer, sizeof(glm::u8vec4), num_vertices, blob);
		glBindBuffer(GL_ARRAY_BUFFER, baked_buffers[2]);
		glBufferData(GL_ARRAY_BUFFER, sizeof(glm::u8vec4) * num_vertices, buffer, GL_STATIC_DRAW);
		glVertexAttribPointer(ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(glm::u8vec4), NULL);
		glEnableVertexAttribArray(ATTRIB_COLOR);

		// Upload texture coordinates
		fread_compressed(buffer, sizeof(glm::vec4), num_vertices, blob);
		glBindBuffer(GL_ARRAY_BUFFER, baked_buffers[3]);
		glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec4) * num_vertices, buffer, GL_STATIC_DRAW);
		glVertexAttribPointer(ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), NULL);
		glEnableVertexAttribArray(ATTRIB_TEXCOORD);

		fclose(blob);
		free(buffer);
		GL_CHECK();
	}

	{ // Load instance matrices from "instances.blob"
		blob = fopen("instances.blob", "rb");
		uint32_t num_instances = 0;
		fread(&num_instances, sizeof(uint32_t), 1, blob);

		buffer = (uint8_t *)calloc(sizeof(glm::mat4), num_instances);
		bytes_read = fread_compressed(buffer, sizeof(glm::mat4), num_instances, blob);
		fclose(blob);

		// Upload instance buffer to OpenGL
		glGenBuffers(1, &instance_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
		glBufferData(GL_ARRAY_BUFFER, bytes_read, buffer, GL_STATIC_DRAW);
		for (int row = 0; row < 4; ++row) {
			glVertexAttribPointer(ATTRIB_WORLD_MATRIX + row, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void *)(sizeof(glm::vec4) * row));
			glVertexAttribDivisor(ATTRIB_WORLD_MATRIX + row, 1);
			glEnableVertexAttribArray(ATTRIB_WORLD_MATRIX + row);
		}

		free(buffer);
		GL_CHECK();
	}

	{ // Load ordered draw calls from "drawables.blob"
		blob = fopen("drawables.blob", "rb");
		uint32_t num_draw_calls = 0;
		fread(&num_draw_calls, sizeof(uint32_t), 1, blob);

		buffer = (uint8_t *)calloc(sizeof(uint64_t) + sizeof(DrawCall), num_draw_calls);
		fread_compressed(buffer, sizeof(uint64_t), num_draw_calls, blob);
		fread_compressed(buffer + num_draw_calls * sizeof(uint64_t), sizeof(DrawCall), num_draw_calls, blob);
		fclose(blob);

		uint64_t *sort_keys = (uint64_t *)buffer;
		DrawCall *draw_calls = (DrawCall *)(sort_keys + num_draw_calls);
		for (uint32_t i = 0; i < num_draw_calls; ++i)
			ordered_draw_calls[sort_keys[i]] = draw_calls[i];

		free(buffer);
	}

	// Prepare shader sources
	const size_t SOURCE_LENGTH = 4096;
	char vertex_source[SOURCE_LENGTH];
	char fragment_source[SOURCE_LENGTH];
	int vertex_source_length = snprintf(vertex_source, SOURCE_LENGTH, "%s\n"
		"#define HAS_SHADER_DRAW_PARAMETERS %i\n"
		"#define HAS_BINDLESS_TEXTURE %i\n"
		"%s\n",
		GLSL_PREAMBLE, has_multi_draw_indirect && has_shader_draw_params, has_bindless_textures, GLSL_VERTEX_SHADER);
	int fragment_source_length = snprintf(fragment_source, SOURCE_LENGTH, "%s\n"
		"#define HAS_SHADER_DRAW_PARAMETERS %i\n"
		"#define HAS_BINDLESS_TEXTURE %i\n"
		"%s\n",
		GLSL_PREAMBLE, has_multi_draw_indirect && has_shader_draw_params, has_bindless_textures, GLSL_FRAGMENT_SHADER);
	assert(0 < vertex_source_length && vertex_source_length < SOURCE_LENGTH);
	assert(0 < fragment_source_length && fragment_source_length < SOURCE_LENGTH);

	// Load shaders and build program
	GLuint vsh = compile_glsl_source(GL_VERTEX_SHADER, vertex_source);
	GLuint fsh = compile_glsl_source(GL_FRAGMENT_SHADER, fragment_source);
	GLuint program = link_glsl(vsh, fsh);
	glDeleteShader(fsh);
	glDeleteShader(vsh);
	if (!vsh || !fsh || !program) {
		fprintf(stderr, "ERROR: SHADER COMPILATION FAILED\n");
		return 9;
	}
	glUseProgram(program);
	WORLD_MATRIX_UNIFORM = glGetUniformLocation(program, "u_WorldFromObject");
	VIEW_PROJ_MATRIX_UNIFORM = glGetUniformLocation(program, "u_ClipFromWorld");
	TEXTURE_0_UNIFORM = glGetUniformLocation(program, "u_Texture0");
	TEMP_TEX_IDX_UNIFORM = glGetUniformLocation(program, "u_TempTextureIdx");

	fprintf(stderr, "INFO: Compiled shaders\n");
	return 0;
}


int post_load()
{
	const uint64_t TEXTURE_ARRAY_MASK = 0xFFFFF000000ULL;

	// SSBO have offset alignment requirements - remember it!
	// We keep texture indices in SSBO which are indexed in shaders with gl_DrawIDARB (if supported)
	GLint ssbo_alignment = 4;
	glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_alignment);
	ssbo_alignment--;

	// Allocate indirect draw buffer on the GPU. It will contain all draw calls parameters.
	// NOTE: It is required only for the gl*Draw*Indirect() family of functions.
	glGenBuffers(1, &indirect_buffer);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buffer);
	glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawElementsIndirectCommand) * ordered_draw_calls.size(), NULL, GL_STATIC_DRAW);

	uint64_t prev_key = UINT64_MAX;
	std::vector<GLuint> texture_idx;
	std::vector<GLuint64> texture_handles;
	std::vector<DrawElementsIndirectCommand> call_args;
	uint32_t indirect_offset = 0;
	uint32_t texid_offset = 0;
	MultiDrawCall mdc = {};

	if (has_multi_draw_indirect && has_shader_draw_params) {
		// Batch the hell out of those instanced draw calls...
		// This loop groups all draw calls that use the same texture array to fill the indirect buffer.
		for (const auto &draw_call_pair : ordered_draw_calls) {
			const DrawCall &dc = draw_call_pair.second;
			const uint64_t key = draw_call_pair.first;
			const uint64_t changes = key ^ prev_key;

			// If there is a change in texture array then we have to flush the batch and start a new one
			if (changes & TEXTURE_ARRAY_MASK) {
				if (!call_args.empty()) {
					// Upload MultiDraw arguments
					mdc.texid_offset = texid_offset;
					mdc.indirect_count = call_args.size();
					mdc.indirect_offset = indirect_offset;
					const GLsizei args_size = sizeof(DrawElementsIndirectCommand) * call_args.size();
					glBufferSubData(GL_DRAW_INDIRECT_BUFFER, indirect_offset, args_size, call_args.data());
					indirect_offset += args_size;

					multicalls.push_back(mdc);
				}

				// Start a new batch from scratch
				prev_key = key;
				mdc = {};
				mdc.tex_array = dc.texture_array;
				call_args.clear();
				texid_offset = sizeof(float) * texture_idx.size();

				// Align the offset to meet the SSBO alignment requirements
				if (!has_bindless_textures) {
					const uint32_t aligned_offset = (texid_offset + ssbo_alignment) & ~ssbo_alignment;
					const uint32_t padding = aligned_offset - texid_offset;
					assert(0 == padding % 4); // I'm assuming that padding is a multiple of 4
					for (uint32_t _ = 0; _ < padding / 4; ++_)
						texture_idx.push_back(0);
					texid_offset = aligned_offset;
				}
			}

			if (has_shader_draw_params)
				texture_idx.push_back(dc.tex_index);
			if (has_bindless_textures)
				texture_handles.push_back(tex_handles[dc.texture_array]);

			DrawElementsIndirectCommand cmd = {
				dc.num_vertices, // = count
				dc.num_instances, // = instanceCount = primcount
				dc.index_offset / sizeof(uint16_t), // firstIndex = indexOffset / sizeofType
				dc.base_vertex,
				dc.base_instance
			};
			call_args.push_back(cmd);
		}

		// One MegaBuffer(TM) containing all texture indices of all draw calls.
		// Ideally this buffer will be indexed with gl_DrawIDARB during rendering.
		glGenBuffers(1, &texid_buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, texid_buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * texture_idx.size(), texture_idx.data(), GL_STATIC_DRAW);

		if (has_bindless_textures) {
			// This buffer contains all the bindless texture handles. Unfortunatelly this requires `GL_ARB_bindless_texture`
			glGenBuffers(1, &texhandle_buffer);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, texhandle_buffer);
			glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint64) * texture_handles.size(), texture_handles.data(), GL_STATIC_DRAW);
		}
	}

	return 0;
}


int initialize(int argc, char *argv[])
{
	if (0 != init_renderer())
		return 1;
	if (0 != load_content())
		return 2;
	if (0 != post_load())
		return 3;


	proj_mat = glm::perspective(45.0f, window_width / (float)window_height, 1.0f, 4000.0f);

	return 0;
}


int fixed_update(uint64_t delta_micros)
{
	// Process system events to avid hanging the application
	SDL_Event evt;
	while (SDL_PollEvent(&evt)) {
		if (SDL_QUIT == evt.type)
			return 1;
		else if (SDL_WINDOWEVENT == evt.type) {
			if (SDL_WINDOWEVENT_RESIZED == evt.window.event) {
				SDL_GetWindowSize(wnd, &window_width, &window_height);
				glViewport(0, 0, window_width, window_height);
				proj_mat = glm::perspective(45.0f, window_width / (float)window_height, 1.0f, 4000.0f);
			}
		}
	}

	// Handle camera
	glm::mat4 camera_matrix;
	{
		int mouse_x, mouse_y;
		uint32_t mouse_btns = SDL_GetRelativeMouseState(&mouse_x, &mouse_y);
		bool mouse_cam = mouse_btns & SDL_BUTTON(1);
		SDL_CaptureMouse((SDL_bool)mouse_cam);
		SDL_SetRelativeMouseMode((SDL_bool)mouse_cam);
		if (mouse_cam) {
			int half_width = window_width / 2;
			int half_height = window_height / 2;
			SDL_WarpMouseInWindow(wnd, half_width, half_height);

			float delta_yaw = mouse_x / (float)half_width;
			float delta_pitch = mouse_y / (float)half_height;
			const float CAMERA_SPEED = 0.2f;
			cam_yaw -= delta_yaw * CAMERA_SPEED;
			cam_pitch += delta_pitch * CAMERA_SPEED;
		}


		glm::mat3 look_mat = glm::mat3(glm::rotate(glm::mat4(1.0f), cam_yaw, glm::vec3(0.0f, 0.0, 1.0))
			* glm::rotate(glm::mat4(1.0f), cam_pitch, glm::vec3(1.0f, 0.0f, 0.0f)));
		glm::vec3 fwd = look_mat * LOOK_DIR;

		camera_matrix = glm::lookAt(cam_pos, cam_pos + fwd, look_mat[2]);


		float MOVE_SPEED = 0.6f;
		const Uint8 *keys = SDL_GetKeyboardState(NULL);
		if (keys[SDL_SCANCODE_LSHIFT])
			MOVE_SPEED *= 5.0f;
		if (keys[SDL_SCANCODE_W])
			cam_pos -= look_mat[1] * MOVE_SPEED;
		else if (keys[SDL_SCANCODE_S])
			cam_pos += look_mat[1] * MOVE_SPEED;
		if (keys[SDL_SCANCODE_A])
			cam_pos += look_mat[0] * MOVE_SPEED;
		else if (keys[SDL_SCANCODE_D])
			cam_pos -= look_mat[0] * MOVE_SPEED;
		if (keys[SDL_SCANCODE_E])
			cam_pos += look_mat[2] * MOVE_SPEED;
		else if (keys[SDL_SCANCODE_Q])
			cam_pos -= look_mat[2] * MOVE_SPEED;
	}

	view_proj = proj_mat * camera_matrix;

	return 0;
}


int post_update(uint64_t delta_micros)
{
	float delta_time = delta_micros / 1000000.0f; // 1 second = 1000000 microseconds
	int fps = 1.0f / delta_time;
	fprintf(stderr, "Frame: f=%i Hz\t time=%g sec\tdraw calls=%i\n", fps, delta_time, draw_call_counter);
	draw_call_counter = 0;

	return 0;
}


int render(void)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUniformMatrix4fv(VIEW_PROJ_MATRIX_UNIFORM, 1, GL_FALSE, glm::value_ptr(view_proj));

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, texid_buffer);
	if (has_multi_draw_indirect && has_shader_draw_params) {
		if (has_bindless_textures) {
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, texhandle_buffer);

			//
			// ~~~~~~~~~~~~~~~~~~~~ THIS IS IT! ONE DRAW CALL! ~~~~~~~~~~~~~~~~~~~~
			//
			glMultiDrawElementsIndirect(GL_TRIANGLE_STRIP, GL_UNSIGNED_SHORT, (void *)0, ordered_draw_calls.size(), sizeof(DrawElementsIndirectCommand));
			++draw_call_counter;
		} else {
			// We don't have bindless textures... but ~31 draw calls is not THAT bad either...
			for (const MultiDrawCall &mdc : multicalls) {
				glBindTexture(GL_TEXTURE_2D_ARRAY, mdc.tex_array);
				glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 3, texid_buffer, mdc.texid_offset, sizeof(float) * mdc.indirect_count);
				glMultiDrawElementsIndirect(GL_TRIANGLE_STRIP, GL_UNSIGNED_SHORT, (void *)mdc.indirect_offset, mdc.indirect_count, sizeof(DrawElementsIndirectCommand));
				++draw_call_counter;
			}
		}
	} else {
		// This is the ultimate nightmare... fallback to 13932 draw calls :(
		// But hey, at least we are using instancing
		uint64_t previous_key = UINT64_MAX;
		const uint64_t TEXTURE_ARRAY_MASK = 0xFFFFF000000ULL;
		for (const auto &draw_call_pair : ordered_draw_calls) {
			const DrawCall &dc = draw_call_pair.second;
			const uint64_t key = draw_call_pair.first;
			const uint64_t changes = key ^ previous_key;

			if (changes & TEXTURE_ARRAY_MASK) {
				glBindTexture(GL_TEXTURE_2D_ARRAY, dc.texture_array);
				previous_key = key;
			}

			glUniform1f(TEMP_TEX_IDX_UNIFORM, dc.tex_index);
			glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLE_STRIP, dc.num_vertices, GL_UNSIGNED_SHORT, (void *)dc.index_offset, dc.num_instances, dc.base_vertex, dc.base_instance);
			++draw_call_counter;
		}
	}

	GL_CHECK();
	SDL_GL_SwapWindow(wnd);

	return 0;
}


void cleanup(void)
{
	// Release GPU allocated memory
	if (has_bindless_textures) {
		for (GLuint64 bindless_texture : tex_handles)
			glMakeTextureHandleNonResidentARB(bindless_texture);
		glDeleteBuffers(1, &texhandle_buffer);
	}
	glDeleteTextures(textures.size(), textures.data());
	glDeleteVertexArrays(1, &baked_vao);
	glDeleteBuffers(4, baked_buffers);
	glDeleteBuffers(1, &instance_buffer);
	glDeleteBuffers(1, &indirect_buffer);
	glDeleteBuffers(1, &texid_buffer);

	stop_opengl_log();
}
