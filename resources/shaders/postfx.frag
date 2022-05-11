#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "./common.h"

layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout(binding = 1, set = 0) uniform sampler2D inColor;

// For compat with quad3_vert
layout (location = 0 ) in FS_IN { vec2 texCoord; } vIn;

layout(location = 0) out vec4 out_fragColor;

void main()
{
    vec2 fragPos = vec2(gl_FragCoord.x/1024 ,gl_FragCoord.y/1024);
    //debugPrintfEXT("lightDir: %1.2v2f\n", fragPos);
    //out_fragColor = textureLod(inColor, vIn.texCoord, 0);
    out_fragColor = textureLod(inColor, fragPos, 0);
}