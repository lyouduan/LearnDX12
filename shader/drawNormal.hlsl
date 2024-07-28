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
Texture2D gShadowMap : register(t1);
Texture2D gShadowTarget : register(t2);

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
    float3 WorldNormal : NORMAL;
    float2 Tex : TEXCOORD;
    float3 TangentW : TANGENT;
};

VertexOut VSMain(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to homogeneous clip space.
    float4 PosW = mul(float4(vin.PosL, 1.0f), world);
    
    vout.PosH = mul(PosW, viewProj);
    // 均匀缩放
    vout.WorldNormal = mul(vin.Normal, (float3x3) world);
    
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 texC = mul(float4(vin.Tex, 0.0f, 1.0f), texTransform);
    vout.Tex = texC.xy;
    
    vout.TangentW = mul(vin.Tangent, (float3x3) world);
    
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
    
    float3 normalV = mul(pin.WorldNormal, (float3x3) view);
    return float4(normalV, 0.0f);
}
