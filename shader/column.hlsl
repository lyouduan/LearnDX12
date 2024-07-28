#define MaxLights 16

#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#define N_SAMPLE 16
static float2 poissonDisk[16] =
{
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};
struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction; // directional/spot light only
    float FalloffEnd; // point/spot light only
    float3 Position; // point light only
    float SpotPower; // spot light only
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};

struct MaterialData
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform;
    uint DiffuseMapIndex;
    uint NormalMapIndex;
    uint MatPad0;
    uint MatPad1;
};
cbuffer cbPerObject : register(b0)
{
    float4x4 world;
    float4x4 texTransform;
    uint gMaterialIndex;
    uint gObjPad0;
    uint gObjPad1;
    uint gObjPad2;

}

cbuffer cbPerObject : register(b1)
{
    float4x4 viewProj;
    float3 gEyePosW;
    float gTotalTime;
    float4 gAmbientLight;
    float4x4 gShadowTransform;
    float4x4 view;
    float4x4 proj;
    float4x4 invProj;
    Light gLights[MaxLights];
}
TextureCube gCubeMap : register(t0); //所有漫反射贴图
Texture2D  gShadowMap : register(t1);
Texture2D  gShadowTarget : register(t2);

Texture2D gDiffuseMap[10] : register(t3); //所有漫反射贴图

StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);

//6个不同类型的采样器
SamplerState gSamPointWrap : register(s0);
SamplerState gSamPointClamp : register(s1);
SamplerState gSamLinearWrap : register(s2);
SamplerState gSamLinearClamp : register(s3);
SamplerState gSamAnisotropicWrap : register(s4);
SamplerState gSamAnisotropicClamp : register(s5);
SamplerComparisonState gSamShadow : register(s6);

float CalcAttenuation(float d, float falloffStart, float falloffEnd);
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec);
float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat);
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye);
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye);
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye);
float4 ComputeLighting(Light gLights[MaxLights], Material mat,
                       float3 pos, float3 normal, float3 toEye,
                       float3 shadowFactor);

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW);

float CalcShadowFactor(float4 shadowPosH);

float CalcShadowPCSS(float4 shadowPosH);
float nrand(float2 uv);

float CalcShadowVSSM(float4 shadowPosH);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
    float3 Tangent : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float3 WorldPos : POSITION1;
    float3 WorldNormal : NORMAL;
    float2 Tex : TEXCOORD;
    float3 TangentW : TANGENT;
};

VertexOut VSMain(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to homogeneous clip space.
    float4 PosW = mul(float4(vin.PosL, 1.0f), world);
    vout.WorldPos = PosW;
    
    vout.PosH = mul(PosW, viewProj);
    // 均匀缩放
    vout.WorldNormal = mul(vin.Normal, (float3x3) world);
    
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 texC = mul(float4(vin.Tex, 0.0f, 1.0f), texTransform);
    vout.Tex = texC.xy;
    
    vout.TangentW = mul(vin.Tangent, (float3x3) world);
    
    vout.ShadowPosH = mul(PosW, gShadowTransform);
    return vout;
}

float4 PSMain(VertexOut pin) : SV_Target
{
    MaterialData matData = gMaterialData[gMaterialIndex];
    
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float roughness = matData.Roughness;
    uint diffuseTexIndex = matData.DiffuseMapIndex;
    uint normalMapIndex = matData.NormalMapIndex;
    
    diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gSamLinearWrap, pin.Tex);
    
    pin.WorldNormal = normalize(pin.WorldNormal);
    
    // normal map
    float4 normalMap = gDiffuseMap[normalMapIndex].Sample(gSamAnisotropicWrap, pin.Tex);
    float3 bumpNormalW = NormalSampleToWorldSpace(normalMap.rgb, pin.WorldNormal, pin.TangentW);
    
    //bumpNormalW = pin.WorldNormal;
    //pin.WorldNormal = normalize(bumpNormalW);
    
    float3 toEyeW = normalize(gEyePosW - pin.WorldPos);
    
    float4 ambient = gAmbientLight * diffuseAlbedo;
    
    Material mat = { diffuseAlbedo, fresnelR0, roughness };
    
    float3 shadowFactor = float3(1.0f, 1.0f, 1.0f);
    shadowFactor[0] = CalcShadowPCSS(pin.ShadowPosH);
    
    float4 directLight = ComputeLighting(gLights, mat, pin.WorldPos,
        bumpNormalW, toEyeW, shadowFactor);
    
    float4 litColor = ambient + directLight;
    
    float3 r = reflect(-toEyeW, bumpNormalW);
    float4 reflectionColor = gCubeMap.Sample(gSamLinearWrap, r);
    float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpNormalW, r);
    litColor.rgb += (1-roughness) * fresnelFactor * reflectionColor.rgb;
    
    litColor.a = diffuseAlbedo.a;
    
    return litColor;
}
float nrand(in float2 uv)
{
    float2 noise =
      (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
    return abs(noise.x + noise.y) * 0.5;
}

float CalcShadowPCSS(float4 shadowPosH)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // Depth in NDC space.
    float depth = shadowPosH.z;
    
    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);
    // Texel size.
    float dx = 1.0f / (float) width;
    
    // 1. blocker search : average depth
    float blockerDepth = 0.0f;
    float numBlocker = 0;
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
            float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
            float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float shadowMap = gShadowMap.Sample(gSamLinearWrap, shadowPosH.xy + offsets[i]).r;
        // 计算遮挡的深度
        if (shadowMap < depth)
        {
            blockerDepth += shadowMap;
            numBlocker++;
        }
    }
    float shadowFactor = 1.0;
    // current object is visible if not block.
    if(numBlocker < 1.0)
    {
        return shadowFactor;
    }
    
    blockerDepth /= numBlocker;
    // 2. Penumbra estimation
    const float lightSize = 5.0;
    float penmbraRadius = (depth - blockerDepth) * lightSize / blockerDepth;
    
    // 3. Filtering
    shadowFactor = 0.0;
    float rot_theta = nrand(shadowPosH.xy);
    float cos_theta = cos(rot_theta);
    float sin_theta = sin(rot_theta);
    float search_radius = penmbraRadius / width * 2;
    float2x2 rot_mat = float2x2(cos_theta, sin_theta, -sin_theta, cos_theta);
    for (int i = 0; i < N_SAMPLE; ++i)
    {
        float2 p = mul(poissonDisk[i], rot_mat);
        float2 offset = float2(p * search_radius);
        shadowFactor += gShadowMap.SampleCmpLevelZero(gSamShadow,
                                                shadowPosH.xy + offset, depth);
    }

    return shadowFactor / (float)N_SAMPLE;
}
float CalcShadowFactor(float4 shadowPosH)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // Depth in NDC space.
    float depth = shadowPosH.z;
    // if not set pso
    float bias = 0.0f;

    // PCF
    if(true){
        uint width, height, numMips;
        gShadowMap.GetDimensions(0, width, height, numMips);

    // Texel size.
        float dx = 1.0f / (float) width;

        float percentLit = 0.0f;
        const float2 offsets[9] =
        {
            float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
            float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
            float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
        };

        [unroll]
        for (int i = 0; i < 9; ++i)
        {
            percentLit += gShadowMap.SampleCmpLevelZero(gSamShadow,
            shadowPosH.xy + offsets[i], depth - bias).r;
        }
    
        return percentLit / 9.0f;
    }
    else
    {
        float shadowDepth = gShadowMap.Sample(gSamLinearWrap, shadowPosH.xy).r;
        return depth < shadowDepth + bias ? 1 : 0;
    }
}

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // Linear falloff.
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// Schlick gives an approximation to Fresnel reflectance (see pg. 233 "Real-Time Rendering 3rd Ed.").
// R0 = ( (n-1)/(n+1) )^2, where n is the index of refraction.
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);

    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);

    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);

    float3 specAlbedo = fresnelFactor * roughnessFactor;

    // Our spec formula goes outside [0,1] range, but we are 
    // doing LDR rendering.  So scale it down a bit.
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for directional lights.
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    // The light vector aims opposite the direction the light rays travel.
    float3 lightVec = -L.Direction;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for point lights.
//---------------------------------------------------------------------------------------
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;

    // The distance from surface to light.
    float d = length(lightVec);

    // Range test.
    if (d > L.FalloffEnd)
        return 0.0f;

    // Normalize the light vector.
    lightVec /= d;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // Attenuate light by distance.
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for spot lights.
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;

    // The distance from surface to light.
    float d = length(lightVec);

    // Range test.
    if (d > L.FalloffEnd)
        return 0.0f;

    // Normalize the light vector.
    lightVec /= d;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // Attenuate light by distance.
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    // Scale by spotlight
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float4 ComputeLighting(Light gLights[MaxLights], Material mat,
                       float3 pos, float3 normal, float3 toEye,
                       float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif 

    return float4(result, 0.0f);
}

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
    float3 normalT = 2.0f * normalMapSample - 1.0f;

	// Build orthonormal basis.
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

	// Transform from tangent space to world space.
    float3 bumpedNormalW = mul(normalT, TBN);

    return bumpedNormalW;
}