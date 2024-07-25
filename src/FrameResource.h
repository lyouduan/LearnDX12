#pragma once
#include "stdafx.h"

using namespace DirectX;
using namespace Microsoft::WRL;

struct Vertex
{
	Vertex() = default;
	Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v) :
		Pos(x, y, z),
		Normal(nx, ny, nz),
		Tex(u, v) {}

	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 Tex;
	DirectX::XMFLOAT3 TangentU;

};

struct MaterialData
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };

	float Roughness = 0.5;

	DirectX::XMFLOAT4X4 MatFransform = MathHelper::Identity4x4();

	UINT DiffuseMapIndex = 0;
	UINT NormalMapIndex = 0;
	UINT MaterialPad0;
	UINT MaterialPad1;
};

struct InstanceData
{
	DirectX::XMFLOAT4X4 world = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 texTransform = MathHelper::Identity4x4();

	UINT MaterialIndex;
	UINT InstancePad0;
	UINT InstancePad1;
	UINT InstancePad2;
};

struct ObjectConstants
{
	XMFLOAT4X4 world = MathHelper::Identity4x4();
	XMFLOAT4X4 texTransform = MathHelper::Identity4x4();

	UINT materialIndex = 0;
	UINT Pad0;
	UINT Pad1;
	UINT Pad2;
};

struct PassConstants
{
	XMFLOAT4X4 viewProj = MathHelper::Identity4x4();
	XMFLOAT3 eyePosW = { 0.0, 0.0, 0.0 };
	float totalTime = 0.0;

	
	XMFLOAT4 ambientLight = { 0.0, 0.0, 0.0, 1.0 };

	Light lights[MAX_LIGHTS];

	XMFLOAT4 fogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	float fogStart = 5.0;
	float fogRange = 150.0;
	XMFLOAT2 pad2 = { 0.0, 0.0 }; // padding

	XMFLOAT2 renderTargetSize = { 0.0f, 0.0f };
	float nearZ = 0.0f;
	float farZ = 0.0f;
	XMFLOAT4X4 shadowTransform = MathHelper::Identity4x4();
};

struct MatConstants
{
	XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };//材质反照率
	XMFLOAT3 fresnelR0 = { 0.01f, 0.01f, 0.01f };//RF(0)值，即材质的反射属性
	float roughness = 0.25f;//材质的粗糙度

	XMFLOAT4X4 matTransform = MathHelper::Identity4x4();

	UINT DiffuseMapIndex = 0;
	UINT NormalMapIndex = 0;
	UINT MaterialPad0;
	UINT MaterialPad1;
};


struct FrameResource
{
	FrameResource(ID3D12Device* device, UINT passCount, UINT objCount, UINT matCount, UINT waveVertCount);
	FrameResource(ID3D12Device* device, UINT passCount, UINT objCount, UINT matCount);
	FrameResource(ID3D12Device* device, UINT passCount, UINT objCount);
	// 防止拷贝或赋值，避免多个对象读写相同的帧资源
	// delete copy constructor
	FrameResource(const FrameResource& rhs) = delete;
	// delete copy assignment constructor
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	ComPtr<ID3D12CommandAllocator> cmdAllocator;
	std::unique_ptr<UploadBuffer<ObjectConstants>> objCB = nullptr;
	std::unique_ptr<UploadBuffer<PassConstants>> passCB = nullptr;
	std::unique_ptr<UploadBuffer<Vertex>> WavesVB = nullptr;
	std::unique_ptr<UploadBuffer<MatConstants>> matCB = nullptr;

	std::unique_ptr<UploadBuffer<MaterialData>> matBuffer = nullptr;
	std::unique_ptr<UploadBuffer<InstanceData>> instanceBuffer = nullptr;

	UINT64 fence = 0;

};
