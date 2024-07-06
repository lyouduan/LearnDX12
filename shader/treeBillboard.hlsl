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

Texture2DArray gTreeMapArray : register(t0); //所有漫反射贴图

//6个不同类型的采样器
SamplerState gSamPointWrap : register(s0);
SamplerState gSamPointClamp : register(s1);
SamplerState gSamLinearWarp : register(s2);
SamplerState gSamLinearClamp : register(s3);
SamplerState gSamAnisotropicWarp : register(s4);
SamplerState gSamAnisotropicClamp : register(s5);

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
    float4x4 gMatTransform;
}
cbuffer cbPassObject : register(b2)
{
    float4x4 viewProj;
    float3 gEyePosW;
    float gTotalTime;
    float4 gAmbientLight;
    Light gLights[MaxLights];
    
    float4 gFogColor;
    float gFogStart;
    float gFogRange;
    
}
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
    float3 VertexW : POSITION;
    float2 SizeW : SIZE;
};

struct VertexOut
{
    float3 CenterPosW : POSITION;
    float2 SizeW : SIZE;
};

struct GeoOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexCoord : TEXCOORD;
    uint PrimID : SV_PrimitiveID;
};

VertexOut VS(VertexIn vin, uint vertID : SV_VertexID)
{
    VertexOut vout;
	
    vout.CenterPosW = vin.VertexW;
    vout.SizeW = vin.SizeW;
    //vout.SizeW = float2(2 + vertID, 2 + vertID);
    
    return vout;
}

[maxvertexcount(4)]
void GS(point VertexOut gin[1],
    uint primID : SV_PrimitiveID,
    inout TriangleStream<GeoOut> triStream)
{
    // 
    float3 up = float3(0.0, 1.0, 0.0);
    float3 look = gEyePosW - gin[0].CenterPosW;
    look.y = 0.0;
    look = normalize(look);
    float3 right = cross(up, look);
    
    float halfWidth  = 0.5 * gin[0].SizeW.x;
    float halfHeight = 0.5 * gin[0].SizeW.y;
    
    float4 v[4];
    v[0] = float4(gin[0].CenterPosW + right * halfWidth - up * halfHeight, 1.0f);
    v[1] = float4(gin[0].CenterPosW + right * halfWidth + up * halfHeight, 1.0f);
    v[2] = float4(gin[0].CenterPosW - right * halfWidth - up * halfHeight, 1.0f);
    v[3] = float4(gin[0].CenterPosW - right * halfWidth + up * halfHeight, 1.0f);
    
    float2 texCoord[4] =
    {
        float2(0.0, 1.0),
        float2(0.0, 0.0),
        float2(1.0, 1.0),
        float2(1.0, 0.0)
    };

    GeoOut geoOut;
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        geoOut.PosH = mul(v[i], viewProj);
        geoOut.PosW = v[i];
        geoOut.NormalW = up;
        geoOut.TexCoord = texCoord[i];
        geoOut.PrimID = primID;
        
        triStream.Append(geoOut);
    }

}

float4 PS(GeoOut pin) : SV_Target
{
    float3 uvw = float3(pin.TexCoord, pin.PrimID % 3);
    float4 diffuseAlbedo = gTreeMapArray.Sample(gSamAnisotropicWarp, uvw) * gDiffuseAlbedo;
    
#ifdef ALPHA_TEST
    // discard the alpha less than 0.1, this operation could speed up the shader process
    clip(diffuseAlbedo.a - 0.1f);
#endif
    
    float3 worldNormal = normalize(pin.NormalW);
    float3 toEyeW = gEyePosW - pin.PosW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize
    
    
    float4 ambient = gAmbientLight * diffuseAlbedo;
    
    Material mat = { diffuseAlbedo, gFresnelR0, gRoughness };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);
    
    float4 litColor = ambient + directLight;
    
#ifdef FOG
    float s = saturate((distToEye - gFogStart) / gFogRange);
    litColor = lerp(litColor, gFogColor, s);
#endif
    
    litColor.a = diffuseAlbedo.a * 0.3f;
    
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
    for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif 

    return float4(result, 0.0f);
}