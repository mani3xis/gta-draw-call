#include <stdio.h>
#include <memory.h>
#include <algorithm>
#include <GL/glew.h>
#include <Windows.h>

static FILE *gl_log_file;


#define CASE_STRING(var, caseval)	{ case caseval: var = #caseval; break; }

static void APIENTRY opengl_log_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *usrParam)
{
	char *source_str = "";
	switch (source) {
		CASE_STRING(source_str, GL_DEBUG_SOURCE_API);
		CASE_STRING(source_str, GL_DEBUG_SOURCE_WINDOW_SYSTEM);
		CASE_STRING(source_str, GL_DEBUG_SOURCE_SHADER_COMPILER);
		CASE_STRING(source_str, GL_DEBUG_SOURCE_THIRD_PARTY);
		CASE_STRING(source_str, GL_DEBUG_SOURCE_APPLICATION);
		CASE_STRING(source_str, GL_DEBUG_SOURCE_OTHER);
	}

	char *type_str = "";
	switch (type) {
		CASE_STRING(type_str, GL_DEBUG_TYPE_ERROR);
		CASE_STRING(type_str, GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR);
		CASE_STRING(type_str, GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR);
		CASE_STRING(type_str, GL_DEBUG_TYPE_PORTABILITY);
		CASE_STRING(type_str, GL_DEBUG_TYPE_PERFORMANCE);
		CASE_STRING(type_str, GL_DEBUG_TYPE_MARKER);
		CASE_STRING(type_str, GL_DEBUG_TYPE_PUSH_GROUP);
		CASE_STRING(type_str, GL_DEBUG_TYPE_POP_GROUP);
		CASE_STRING(type_str, GL_DEBUG_TYPE_OTHER);
	}

	char *severity_str = "";
	switch (severity) {
		CASE_STRING(severity_str, GL_DEBUG_SEVERITY_HIGH);
		CASE_STRING(severity_str, GL_DEBUG_SEVERITY_MEDIUM);
		CASE_STRING(severity_str, GL_DEBUG_SEVERITY_LOW);
		CASE_STRING(severity_str, GL_DEBUG_SEVERITY_NOTIFICATION);
	}

	fprintf(gl_log_file, "%s;%s;%s;%u;%s\n", severity_str, type_str, source_str, id, message);
	fflush(gl_log_file);
}


void start_opengl_log(const char *filename)
{
	gl_log_file = fopen(filename, "w");
	fprintf(gl_log_file, "severity;type;source;id;message\n");
	glDebugMessageCallback(opengl_log_callback, NULL);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
}


void stop_opengl_log()
{
	if (gl_log_file)
		fclose(gl_log_file);
	gl_log_file = NULL;
}


GLuint compile_glsl_source(GLenum type, char *source)
{
	GLuint shader = 0;
	const size_t byte_size = strlen(source);
	if (byte_size > 0) {
		// Compile shader
		shader = glCreateShader(type);
		glShaderSource(shader, 1, (GLchar **)&source, NULL);
		glCompileShader(shader);

		GLint compilation_status;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compilation_status);
		if (GL_FALSE == compilation_status) {
			// We recycle the `source` buffer for error messages.
			// If compilation log is too long it will be truncated depending on source length
			glGetShaderInfoLog(shader, byte_size, NULL, source);
			fprintf(stderr, "ERROR: Failed to compile shader from source: %s\n", source);
			glDeleteShader(shader);
			shader = 0;
		}
	}
	return shader;
}


GLuint link_glsl(GLuint vertex_shader, GLuint fragment_shader)
{
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	GLint link_status;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (GL_FALSE == link_status) {
		GLsizei log_len = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
		GLchar *log = new GLchar[log_len + 1];
		memset(log, 0, log_len + 1);
		glGetProgramInfoLog(program, log_len, &log_len, log);
		fprintf(stderr, "ERROR: Failed to link shaders: %s\n", log);
		delete[] log;
		glDeleteProgram(program);
		return 0;
	}

	return program;
}
