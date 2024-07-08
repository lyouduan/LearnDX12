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
Texture2D gDiffuseMap : register(t0); //所有漫反射贴图

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

struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float3 PosL : POSITION;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to homogeneous clip space.
    vout.PosL = vin.PosL;
    
    return vout;
}

//////////////////////////
// tessellation phase

// 常量外壳着色器constant hull shader
// 输出网格的曲面细分因子
struct PatchTess
{
    float EdgeTess[4] : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};

PatchTess ConstantHS(InputPatch<VertexOut, 16> patch, uint patchID : SV_PrimitiveID)
{
    PatchTess pt;
    
    pt.EdgeTess[0] = 25; // 左
    pt.EdgeTess[1] = 25; // 上
    pt.EdgeTess[2] = 25; // 右
    pt.EdgeTess[3] = 25; // 下
	
    pt.InsideTess[0] = 25; // u轴内部细分列数
    pt.InsideTess[1] = 25; // v轴内部细分列数
	
    return pt;
}
 
struct HullOut
{
    float3 PosL : POSITIONT;
};

[domain("quad")] // 片面类型
[partitioning("integer")] // 细分模式
[outputtopology("triangle_cw")] // 三角形的绕序
[outputcontrolpoints(16)] //外壳着色器的执行次数，每次执行输出1个控制点
[patchconstantfunc("ConstantHS")] // 常量外壳着色器函数名称
[maxtessfactor(64.0f)] //曲面细分因子的最大值1-64
HullOut HS(InputPatch<VertexOut, 16> p,
            uint i : SV_OutputControlPointID,
            uint patchId : SV_PrimitiveID)
{
    HullOut hout;
    hout.PosL = p[i].PosL;
    
    return hout;
}


struct DomainOut
{
    float4 PosH : SV_POSITION;
};

float4 BernsteinBasis(float t)
{
    float invT = 1.0f - t;
    return float4(
                invT * invT * invT,
                3.0 * t * invT * invT,
                3.0 * t * t * invT,
                t * t * t
            );

}

float4 dBernsteinBasis(float t)
{
    float invT = 1.0f - t;
    
    return float4(
                -3.0 * invT * invT,
                3.0 * invT * invT - 6 * t * invT,
                6 * t * invT - 3 * t * t,
                3 * t * t
            );
}

float3 CubicBezierSum(const OutputPatch<HullOut, 16> bezpatch, float4 basisU, float4 basisV)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    sum  = basisV.x * (basisU.x * bezpatch[0].PosL + basisU.y * bezpatch[1].PosL + basisU.z * bezpatch[2].PosL + basisU.w * bezpatch[3].PosL);
    sum += basisV.y * (basisU.x * bezpatch[4].PosL + basisU.y * bezpatch[5].PosL + basisU.z * bezpatch[6].PosL + basisU.w * bezpatch[7].PosL);
    sum += basisV.z * (basisU.x * bezpatch[8].PosL + basisU.y * bezpatch[9].PosL + basisU.z * bezpatch[10].PosL + basisU.w * bezpatch[11].PosL);
    sum += basisV.w * (basisU.x * bezpatch[12].PosL + basisU.y * bezpatch[13].PosL + basisU.z * bezpatch[14].PosL + basisU.w * bezpatch[15].PosL);

    return sum;
}

[domain("quad")]
DomainOut DS(PatchTess patchTess,
            float2 uv : SV_DomainLocation,
            const OutputPatch<HullOut, 16> bezPatch)
{
    DomainOut dout;
    
    float4 basisU = BernsteinBasis(uv.x);
    float4 basisV = BernsteinBasis(uv.y);

    float3 p = CubicBezierSum(bezPatch, basisU, basisV);
    
    float4 posW = mul(float4(p, 1.0f), world);
    dout.PosH = mul(posW, viewProj);
    
    return dout;
}

float4 PS(DomainOut pin) : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
