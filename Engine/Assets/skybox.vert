#version 450 core

// Prevents generating code that needs ClipDistance which is not available.
out gl_PerVertex { vec4 gl_Position; };

layout (location = 0) in vec3 aPosition;

layout (set = 0, binding = 0) uniform UBO 
{
    mat4 localToClip;
} ubo;

layout (location = 0) out vec3 vTexCoord;

void main()
{
    gl_Position = ubo.localToClip * vec4( aPosition.xyz, 1.0 );

    vTexCoord = aPosition;
}
