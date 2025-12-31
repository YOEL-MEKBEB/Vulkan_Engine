#version 450

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inPositionWorld;

layout (location = 0) out vec4 outFragColor;

void main(){
    float lightValue = max(dot(inNormal, sceneData.sunlightDirection.xyz), 0.1f);


    vec3 viewDir = normalize(sceneData.cameraPosition.xyz - inPositionWorld);
    // vec3 totalIllumination = vec3(0.0f, 0.0f, 0.0f); 
    
	vec4 color = inColor * texture(colorTex,inUV);
    vec3 ambient = sceneData.ambientColor.xyz * sceneData.ambientColor.w;

    vec3 lightDir = normalize(sceneData.sunlightDirection.xyz);
    vec3 diffuse = sceneData.sunlightColor.xyz * sceneData.sunlightColor.w *  max(dot(inNormal, lightDir), 0.0);
    vec3 H = normalize(viewDir + lightDir);
    vec3 specular = sceneData.specularColor.xyz * sceneData.specularColor.w * pow(max(dot(inNormal, H), 0.0), float(sceneData.shininess));

	// outFragColor = vec4(color * lightValue *  sceneData.sunlightColor.xyz + ambient ,1.0f);
    vec3 ambientDiffuse= ambient + diffuse;
    vec3 surfaceColor = ambientDiffuse * color.rgb;
    outFragColor = vec4(surfaceColor + specular, 1.0f);
    
}

