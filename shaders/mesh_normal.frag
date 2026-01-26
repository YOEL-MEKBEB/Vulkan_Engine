#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUV; //tex coords
layout (location = 3) in vec3 inPositionWorld;
layout (location = 4) in vec4 lightview_position;
layout (location = 5) in vec4 inPositionTangent;
layout (location = 6) in vec4 cameraPositionTangent;
layout (location = 7) in vec4 lightDirectionTangent;
layout (location = 8) in flat int useSceneNormal; 

layout (location = 0) out vec4 outFragColor;

// layout( push_constant ) uniform constants
// {
// 	layout (offset = 72) int useNormal;
// 	layout (offset = 76) int useMetalTex;
// 	layout (offset = 80) int useAOTex;
// } PushConstants;
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
	int useORM;
	int useEmissive;
} PushConstants;


const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom + 0.001;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k + 0.001;
	
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}   


void main(){
    vec3 nTangent;
    if(PushConstants.useNormal == 0 && useSceneNormal == 0){
        nTangent = vec3(0, 0, 1);
        // nTangent = normalize(inNormal);
    }else{
        nTangent = texture(normalTex, inUV).rgb;
        nTangent = normalize(nTangent * 2.0 - 1.0);
    }

    float lightValue = max(dot(nTangent, lightDirectionTangent.xyz), 0.1f);

    vec3 viewDir = normalize(cameraPositionTangent.xyz - inPositionTangent.xyz);
    

    vec3 lightDir = normalize(lightDirectionTangent.xyz);
    vec3 H = normalize(viewDir + lightDir);

////////////// phong illumination model

	// vec4 color = inColor * texture(colorTex,inUV);
 //    vec3 diffuse = sceneData.sunlightColor.xyz * sceneData.sunlightColor.w *  max(dot(nTangent, lightDir), 0.0);
 //    vec3 ambient = sceneData.ambientColor.xyz * sceneData.ambientColor.w;
 //    vec3 specular = sceneData.specularColor.xyz * sceneData.specularColor.w * pow(max(dot(nTangent, H), 0.0), float(sceneData.shininess));

	// // outFragColor = vec4(color * lightValue *  sceneData.sunlightColor.xyz + ambient ,1.0f);
 //    // vec3 ambientDiffuse= ambient + diffuse;
 //    // vec3 surfaceColor = ambientDiffuse * color.rgb;

 //    vec3 p = lightview_position.xyz/lightview_position.w;

 //    // outFragColor = vec4(surfaceColor + specular, 1.0f);
 //    // outFragColor.rgb = pow(outFragColor.rgb, vec3(1.0/gamma));
 //    float shadow = 0.0;
 //    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

 //    // Sample a 3x3 grid around the center pixel
 //    for(int x = -1; x <= 1; ++x)
 //    {
 //        for(int y = -1; y <= 1; ++y)
 //        {
 //            float pcfDepth = texture(shadowMap, p.xy + vec2(x, y) * texelSize).r;
       
 //            shadow += (pcfDepth > (p.z + 0.0002)) ? 0.0 : 1.0;        
 //        }
 //    }

 //    // Average the 9 samples
 //    shadow /= 9.0;
 //    vec3 finalColor = (ambient * color.rgb + diffuse * shadow * color.rgb + specular * shadow);

////////////////////////////////////////////////


// /////////////////////cook torrence model ////////////////////////
 
    // float distance    = length(lightPositions - WorldPos);
    // float attenuation = 1.0 / (distance *  distance); // I only have directional light at the moment

	vec3 albedo = inColor.rgb * pow(texture(colorTex, inUV).rgb, vec3(2.2));
    float ao = PushConstants.useORM == 0 ? (PushConstants.useAOTex == 0 ? 1.0 : texture(aoTex, inUV).r) : texture(metalRoughTex, inUV).r; //most disgusting ternary I've created so far. I'm definitely going to forget how this works in the future.
    float roughness = max(materialData.metal_rough_factors.y * texture(metalRoughTex, inUV).g, 0.1);
    float metallic = PushConstants.useMetalTex == 0 ? 0.0 : materialData.metal_rough_factors.x * texture(metalRoughTex, inUV).b;
    vec3 emissive = PushConstants.useEmissive == 0 ? vec3(0.0) : materialData.emissive_factors.rgb * texture(emissiveTex, inUV).rgb;

    vec3 radiance = sceneData.sunlightColor.xyz * sceneData.sunlightColor.w;
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);
    vec3 F  = fresnelSchlick(max(dot(H, viewDir), 0.0), F0);
    
    float NDF = DistributionGGX(nTangent, H, roughness);       
    float G = GeometrySmith(nTangent, viewDir, lightDir, roughness); 

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(nTangent, viewDir), 0.0) * max(dot(nTangent, lightDir), 0.0)  + 0.0001;
    vec3 specularLo = numerator / denominator;  

    // vec3 kS = F;
    // vec3 kD = vec3(1.0) - kS;

    // kD *= 1.0 - metallic;	

  

    F = fresnelSchlickRoughness(max(dot(nTangent, viewDir), 0.0), F0, roughness);
   
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    vec3 irradiance = texture(irradianceMap, nTangent).rgb;
    // vec3 ambient    = (kD * diffuse) * ao; 
    // vec3 ambient = vec3(0.03) * albedo * ao;

    vec3 reflection = reflect(-viewDir, nTangent);
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(skybox, reflection,  roughness * MAX_REFLECTION_LOD).rgb;   //skybox contains the prefilter map 
    vec2 envBRDF  = texture(brdfLUT, vec2(max(dot(nTangent, viewDir), 0.0), roughness)).rg;
        
    float shadow = 0.0;
    vec3 p = lightview_position.xyz/lightview_position.w;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, p.xy + vec2(x, y) * texelSize).r;
       
            shadow += (pcfDepth > (p.z + 0.0002)) ? 0.0 : 1.0;        
        }
    }
    
    shadow /= 9.0;
    vec3 diffuse = irradiance * albedo;
    vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);
    vec3 ambient = (kD * diffuse + specular) * ao; 
    // vec3 ambient    = (kD * diffuse + specular); 

    float NdotL = max(dot(nTangent, lightDir), 0.0);        
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    vec3 finalColor = ambient + Lo * shadow + emissive; 
    // vec3 finalColor = emissive; 
/////////////////////cook torrence model ////////////////////////

    float gamma = 2.2;
    outFragColor = vec4(finalColor, 1.0f);
    outFragColor.rgb = pow(outFragColor.rgb, vec3(1.0/gamma));

    /////environment mapping reflection
    // vec3 I = normalize(inPositionWorld - sceneData.cameraPosition.xyz);
    // vec3 R = reflect(I, normalize(inNormal));
    // outFragColor = vec4(texture(skybox, R).rgb, 1.0);
    /////////////////////

    /////environment mapping refracion
    // float ratio = 1.00 / 1.52;
    // R = refract(I, normalize(inNormal), ratio);
    // outFragColor = vec4(texture(skybox, R).rgb, 1.0);
   ////////////////////////
}

