#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#include "input_structures.glsl"

layout (location = 0) out vec3 texCoords;

struct Vertex {

	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
	vec4 tangent;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer{ 
	Vertex vertices[];
};

layout( push_constant ) uniform constants
{
	mat4 render_matrix;
	VertexBuffer vertexBuffer;
	// int useNormal;
} PushConstants;

void main()
{
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	vec4 position = vec4(v.position, 1.0f);
    texCoords = v.position;

    vec4 finalPosition = sceneData.viewproj * PushConstants.render_matrix * position;
    finalPosition.z = 0;
    //tricking the depth test with w = 0 to make it fail the depth test
    // why w = 0 and not w = 1? because I'm using a reverse z setup.
	gl_Position = finalPosition;
}  
