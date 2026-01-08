#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inPositionWorld;
layout (location = 4) in vec4 lightview_position;
layout (location = 5) in vec4 inPositionTangent;
layout (location = 6) in vec4 cameraPositionTangent;
layout (location = 7) in vec4 lightDirectionTangent;
layout (location = 8) in flat int useSceneNormal; //the flat keyworkd signifies no interpolation

layout (location = 0) out vec4 outFragColor;
layout( push_constant ) uniform constants
{
	layout (offset = 72) int useNormal;
} PushConstants;


void main(){
    vec3 nTangent;
    if(PushConstants.useNormal == 0 && useSceneNormal == 0){
        nTangent = vec3(0, 0, 1);
    }else{
        nTangent = texture(normalTex, inUV).rgb;
        nTangent = normalize(nTangent * 2.0 - 1.0);
    }

    float lightValue = max(dot(nTangent, lightDirectionTangent.xyz), 0.1f);

    vec3 viewDir = normalize(cameraPositionTangent.xyz - inPositionTangent.xyz);
    
	vec4 color = inColor * texture(colorTex,inUV);
    vec3 ambient = sceneData.ambientColor.xyz * sceneData.ambientColor.w;

    vec3 lightDir = normalize(lightDirectionTangent.xyz);
    vec3 diffuse = sceneData.sunlightColor.xyz * sceneData.sunlightColor.w *  max(dot(nTangent, lightDir), 0.0);
    vec3 H = normalize(viewDir + lightDir);
    vec3 specular = sceneData.specularColor.xyz * sceneData.specularColor.w * pow(max(dot(nTangent, H), 0.0), float(sceneData.shininess));

	// outFragColor = vec4(color * lightValue *  sceneData.sunlightColor.xyz + ambient ,1.0f);
    // vec3 ambientDiffuse= ambient + diffuse;
    // vec3 surfaceColor = ambientDiffuse * color.rgb;
    float gamma = 2.2;

    vec3 p = lightview_position.xyz/lightview_position.w;

    // outFragColor = vec4(surfaceColor + specular, 1.0f);
    // outFragColor.rgb = pow(outFragColor.rgb, vec3(1.0/gamma));
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    // Sample a 3x3 grid around the center pixel
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, p.xy + vec2(x, y) * texelSize).r;
       
            shadow += (pcfDepth > (p.z + 0.0002)) ? 0.0 : 1.0;        
        }
    }

    // Average the 9 samples
    shadow /= 9.0;
    vec3 finalColor = (ambient * color.rgb + diffuse * shadow * color.rgb + specular * shadow);
    outFragColor = vec4(finalColor, 1.0f);
    outFragColor.rgb = pow(outFragColor.rgb, vec3(1.0/gamma));
   
}

