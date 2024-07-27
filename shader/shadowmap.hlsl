#define MaxLights 16

#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

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
//cbuffer cbMatObject : register(b1)
//{
//    float4 gDiffuseAlbedo;
//    float3 gFresnelR0;
//    float gRoughness;
//}

cbuffer cbPerObject : register(b1)
{
    float4x4 viewProj;
    float3 gEyePosW;
    float gTotalTime;
    float4 gAmbientLight;
    float4x4 gShadowTransform;
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

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD;
};

VertexOut VSMain(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to homogeneous clip space.
    float4 PosW = mul(float4(vin.PosL, 1.0f), world);
    vout.PosH = mul(PosW, viewProj);
    
    // 均匀缩放
    
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), texTransform);
    vout.Tex = texC.xy;
    
    return vout;
}

void PSMain(VertexOut pin)
{
    
}
