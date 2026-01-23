#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "input_structures.glsl"

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec4 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outPositionWorld;
layout (location = 4) out vec4 lightview_position;
layout (location = 5) out vec4 outPositionTangent;
layout (location = 6) out vec4 cameraPositionTangent;
layout (location = 7) out vec4 lightDirectionTangent;
layout (location = 8) out int useSceneNormal;
layout (location = 9) out vec3 skyboxTexCoords;

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

//push constants block
layout( push_constant ) uniform constants
{
	mat4 render_matrix;
	VertexBuffer vertexBuffer;
	int useNormal;
	int useMetalTex;
	int useAOTex;
} PushConstants;

void main() 
{
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	
	vec4 position = vec4(v.position, 1.0f);

	vec3 tangent;
	//all zeros is for the sake of identification
	// to suggest that no tangents have been loaded
	// so we calculate our own tangent
	if(v.tangent == vec4(0.0, 0.0, 0.0, 0.0)){
		
	
	    vec3 pos1 = vec3(-1.0,  1.0, 0.0);
	    vec3 pos2 = vec3(-1.0, -1.0, 0.0);
	    vec3 pos3 = vec3( 1.0, -1.0, 0.0);
	    vec3 pos4 = vec3( 1.0,  1.0, 0.0);
	    // // texture coordinates
	    vec2 uv1 = vec2(0.0, 1.0);
	    vec2 uv2 = vec2(0.0, 0.0);
	    vec2 uv3 = vec2(1.0, 0.0);
	    vec2 uv4 = vec2(1.0, 1.0);
    
	    vec3 edge1 = pos2 - pos1;
	    vec3 edge2 = pos3 - pos1;
	    vec2 deltaUV1 = uv2 - uv1;
	    vec2 deltaUV2 = uv3 - uv1;  

	    float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

	    tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
	    tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
	    tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
	    // vec3 c1 = cross(v.normal, vec3(0.0, 0.0, 1.0)); 
     //    vec3 c2 = cross(v.normal, vec3(0.0, 1.0, 0.0)); 
        
     //    // Pick the cross product that produces a longer vector (to avoid 0-length results)
     //    if(length(c1) > length(c2)) {
     //        tangent = c1;
     //    } else {
     //        tangent = c2;
     //    }
	    useSceneNormal = 0;
    }else{
    	tangent = v.tangent.rgb;
    	useSceneNormal = 1;
    }
    //using the gram-shmidt process to calculate the bitangent vector;
    vec3 T = normalize(vec3(PushConstants.render_matrix * vec4(tangent, 0.0)));
    // vec3 B = normalize(vec3(normalMatrix * vec4(bitangent, 0.0)));
    vec3 N = normalize(vec3(PushConstants.render_matrix * vec4(v.normal, 0.0)));
    T = normalize(T - (dot(T, N) * N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    TBN = transpose(TBN);
    outPositionTangent = vec4(TBN * (PushConstants.render_matrix * position).xyz, 1.0);
    cameraPositionTangent = vec4(TBN * sceneData.cameraPosition.xyz, 1.0);
    lightDirectionTangent = vec4(TBN * sceneData.sunlightDirection.xyz, 1.0);
	
	gl_Position =  sceneData.viewproj * PushConstants.render_matrix *position;

	outNormal = normalize((PushConstants.render_matrix * vec4(v.normal, 0.f)).xyz);
	outColor = v.color * materialData.colorFactors;	
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;
	lightview_position = shadowSceneData.viewproj * PushConstants.render_matrix * position;
}
