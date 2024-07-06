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
cbuffer cbPerObject : register(b0)
{
    float4x4 world;
    float4x4 texTransform;
}
cbuffer cbMatObject : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
}
cbuffer cbPerObject : register(b2)
{
    float4x4 viewProj;
    float3 gEyePosW;
    float gTotalTime;
    float4 gAmbientLight;
    Light gLights[MaxLights];
}
Texture2D gDiffuseMap : register(t0); //所有漫反射贴图
//6个不同类型的采样器
SamplerState gSamPointWrap : register(s0);
SamplerState gSamPointClamp : register(s1);
SamplerState gSamLinearWarp : register(s2);
SamplerState gSamLinearClamp : register(s3);
SamplerState gSamAnisotropicWarp : register(s4);
SamplerState gSamAnisotropicClamp : register(s5);

float CalcAttenuation(float d, float falloffStart, float falloffEnd);
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec);
float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat);
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye);
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye);
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye);
float4 ComputeLighting(Light gLights[MaxLights], Material mat,
                       float3 pos, float3 normal, float3 toEye,
                       float3 shadowFactor);
struct VertexIn
{
    float3 PosL : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VertexOut
{
    float3 PosL : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct GeoOut
{
    float4 PosH : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 WorldNormal : NORMAL;
    float2 Tex : TEXCOORD;
};

VertexOut VSMain(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to homogeneous clip space.
    vout.PosL = vin.PosL;

    vout.Normal = vin.Normal;
    
    vout.Tex = vin.Tex;
    return vout;
}

[maxvertexcount(2)]
void GSMain2(point VertexOut gin[1],
    inout LineStream< GeoOut> lineStream)
{
    VertexOut geoOutVert[2];
    float L = 1.0f;
    geoOutVert[0].PosL = gin[0].PosL;
    geoOutVert[1].PosL = geoOutVert[0].PosL + gin[0].Normal * L;
    
    geoOutVert[0].Normal = gin[0].Normal;
    geoOutVert[1].Normal = geoOutVert[0].Normal;
    
    GeoOut geoOut[2];
    [unroll]
    for (uint i = 0; i < 2; i++)
    {
        float4 PosW = mul(float4(geoOutVert[i].PosL, 1.0f), world);
        geoOut[i].PosH = mul(PosW, viewProj);
        lineStream.Append(geoOut[i]);
    }
}
float4 PSMain2(GeoOut pin) : SV_Target
{
    
    return float4(0, 0, 1, 1);
}

[maxvertexcount(8)]
void GSMain(triangle VertexOut gin[3],
    inout TriangleStream<GeoOut> triStream)
{
    if (true)
    {
        VertexOut m[3];
        m[0].PosL = (gin[0].PosL + gin[1].PosL) * 0.5;
        m[1].PosL = (gin[1].PosL + gin[2].PosL) * 0.5;
        m[2].PosL = (gin[0].PosL + gin[2].PosL) * 0.5;
        
        m[0].PosL = normalize(m[0].PosL);
        m[1].PosL = normalize(m[1].PosL);
        m[2].PosL = normalize(m[2].PosL);
        
        m[0].Normal = m[0].PosL;
        m[1].Normal = m[1].PosL;
        m[2].Normal = m[2].PosL;
        
        m[0].Tex = 0.5 * (gin[0].Tex + gin[1].Tex);
        m[1].Tex = 0.5 * (gin[1].Tex + gin[2].Tex);
        m[2].Tex = 0.5 * (gin[2].Tex + gin[0].Tex);
        
        VertexOut geoOutVertex[6];
        geoOutVertex[0] = gin[0];
        geoOutVertex[1] = m[0];
        geoOutVertex[2] = m[2];
        geoOutVertex[3] = m[1];
        geoOutVertex[4] = gin[2];
        geoOutVertex[5] = gin[1];
        
        GeoOut geoOut[6];
        [unroll]
        for (uint i = 0; i < 6; i++)
        {
            geoOut[i].WorldPos = mul(float4(geoOutVertex[i].PosL, 1.0f), world);
            geoOut[i].PosH = mul(float4(geoOut[i].WorldPos, 1.0f), viewProj);
            geoOut[i].WorldNormal = mul(geoOutVertex[i].Normal, (float3x3)world);
            geoOut[i].Tex = geoOutVertex[i].Tex;
        }
        [unroll]
        for (uint j = 0; j < 5; j++)
        {
            triStream.Append(geoOut[j]);
        }
        triStream.RestartStrip();
        triStream.Append(geoOut[1]);
        triStream.Append(geoOut[5]);
        triStream.Append(geoOut[3]);
    }
    if(!true)
    {
       //float3 triangleNormal = gin[0].Normal + gin[1].Normal + gin[2].Normal;
       //float p = 0.5f;
       //gin[0].PosL += triangleNormal * p * (sin(gTotalTime) + 1);
       //gin[1].PosL += triangleNormal * p * (sin(gTotalTime) + 1);
       //gin[2].PosL += triangleNormal * p * (sin(gTotalTime) + 1);
       //GeoOut geoOut[3];
       //[unroll]
       //for (uint i = 0; i < 3; i++)
       //{
       //    geoOut[i].WorldPos = mul(float4(gin[i].PosL, 1.0f), world);
       //    geoOut[i].PosH = mul(float4(geoOut[i].WorldPos, 1.0f), viewProj);
       //    geoOut[i].WorldNormal = mul(gin[i].Normal, (float3x3) world);
       //    geoOut[i].Tex = gin[i].Tex;
       //    triStream.Append(geoOut[i]);
       //}
    }
   
}

float4 PSMain(GeoOut pin) : SV_Target
{
    float4 diffuseAlbedo = gDiffuseMap.Sample(gSamAnisotropicWarp, pin.Tex) * gDiffuseAlbedo;
    pin.WorldNormal = normalize(pin.WorldNormal);
    float3 toEyeW = normalize(gEyePosW - pin.WorldPos);
    
    float4 ambient = gAmbientLight * diffuseAlbedo;
    
    Material mat = { diffuseAlbedo, gFresnelR0, gRoughness };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.WorldPos,
        pin.WorldNormal, toEyeW, shadowFactor);
    
    float4 litColor = ambient + directLight;
    litColor.a = diffuseAlbedo.a;
    
    return litColor;
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