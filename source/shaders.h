/*
 * Hardcoded GLSL shaders.
 *
 * Please excuse my ignorance and my lack of professionalism :)
 * Those shaders were loaded from files but later on I've added support
 * for optional codepaths... and ended up hardcoding them.
 */
#ifndef _SHADERS_INCLUDED
#define _SHADERS_INCLUDED


static const char *GLSL_PREAMBLE = "#version 420 core\n";


static const char *GLSL_VERTEX_SHADER =
        "#if HAS_SHADER_DRAW_PARAMETERS                                    \n"
        "// This is the 'fast path' which cuts the number                  \n"
        "// of draw calls to minimum (below 40).                           \n"
        "#extension GL_ARB_shader_draw_parameters : require                \n"
        "\n"
        "flat out uint DrawID;                                             \n"
        "#endif                                                            \n"
        "\n"
        "uniform mat4 u_WorldFromObject; // world                          \n"
        "uniform mat4 u_ClipFromWorld;   // projection * view              \n"
        "\n"
        "layout(location=0) in vec4 in_Position;                           \n"
        "layout(location=1) in vec3 in_Normal;                             \n"
        "layout(location=2) in vec4 in_Color;                              \n"
        "layout(location=3) in vec4 in_TexCoord;                           \n"
        "\n"
        "layout(location=12) in mat4 in_WorldFromObject;                   \n"
        "\n"
        "out vec3 v_Normal;                                                \n"
        "out vec4 v_Color;                                                 \n"
        "out vec2 v_TexCoord0;                                             \n"
        "out vec2 v_TexCoord1;                                             \n"
        "\n"
        "void main()                                                       \n"
        "{                                                                 \n"
        "       mat4 ClipFromObject = u_ClipFromWorld * in_WorldFromObject;\n"
        "       gl_Position = ClipFromObject * in_Position;                \n"
        "       v_Normal = normalize(in_Normal);                           \n"
        "       v_Color = in_Color;                                        \n"
        "       v_TexCoord0 = in_TexCoord.xy;                              \n"
        "       v_TexCoord1 = in_TexCoord.zw;                              \n"
        "\n"
        "#if HAS_SHADER_DRAW_PARAMETERS                                    \n"
        "       DrawID = gl_DrawIDARB;                                     \n"
        "#endif                                                            \n"
        "}\n";


static const char *GLSL_FRAGMENT_SHADER =
        "#if HAS_SHADER_DRAW_PARAMETERS && HAS_BINDLESS_TEXTURE                                 \n"
        "// If the GPU driver supports bindless textures, then we can achive 1 draw call!       \n"
        "#extension GL_ARB_bindless_texture : require                                           \n"
        "\n"
        "layout(std430, binding=4) buffer TextureHandles {                                      \n"
        "       sampler2DArray textures[];                                                      \n"
        "};                                                                                     \n"
        "#else                                                                                  \n"
        "layout(binding=0) uniform sampler2DArray u_Texture0;                                   \n"
        "#endif                                                                                 \n"
        "\n"
        "#if HAS_SHADER_DRAW_PARAMETERS                                                         \n"
        "layout(std430, binding=3) buffer TextureIndices {                                      \n"
        "       int indices[];                                                                  \n"
        "};                                                                                     \n"
        "\n"
        "flat in uint DrawID;                                                                   \n"
        "#else                                                                                  \n"
        "// This is the slow path, where we use following uniform                               \n"
        "// to pass the texture index for each instanced draw call.                             \n"
        "uniform float u_TempTextureIdx;                                                        \n"
        "#endif                                                                                 \n"
        "\n"
        "in vec3 v_Normal;                                                                      \n"
        "in vec4 v_Color;                                                                       \n"
        "in vec2 v_TexCoord0;                                                                   \n"
        "in vec2 v_TexCoord1;                                                                   \n"
        "\n"
        "layout (location=0) out vec4 f_Color;                                                  \n"
        "\n"
        "void main()                                                                            \n"
        "{                                                                                      \n"
        "#if HAS_SHADER_DRAW_PARAMETERS                                                         \n"
        "       #if HAS_BINDLESS_TEXTURE                                                        \n"
        "               // This results in 1 draw call :O                                       \n"
        "               f_Color = texture(textures[DrawID], vec3(v_TexCoord0, indices[DrawID]));\n"
        "       #else                                                                           \n"
        "               // This results in about 31 draw calls :)                               \n"
        "               f_Color = texture(u_Texture0, vec3(v_TexCoord0, indices[DrawID]));      \n"
        "       #endif                                                                          \n"
        "#else                                                                                  \n"
        "       // This results in 13k draw calls :(                                            \n"
        "       f_Color = texture(u_Texture0, vec3(v_TexCoord0, u_TempTextureIdx));             \n"
        "#endif                                                                                 \n"
        "}\n";


#endif
