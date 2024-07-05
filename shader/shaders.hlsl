cbuffer cbPerObject : register(b0)
{
    float4x4 world;
}

cbuffer cbPerObject : register(b1)
{
    float4x4 viewProj;
}

struct VertexIn
{
    float3 PosL : POSITION;
    float4 Color : COLOR;
};


struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
};

VertexOut VSMain(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to homogeneous clip space.
    vout.PosH = mul(mul(float4(vin.PosL, 1.0f), world), viewProj);
	
	// Just pass vertex color into the pixel shader.
    vout.Color = vin.Color;
    
    return vout;
}

float4 PSMain(VertexOut pin) : SV_Target
{
    return pin.Color;
}