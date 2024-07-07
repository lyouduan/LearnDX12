#pragma once

#pragma once
#include "D3DApp.h"
#include "BlurFilter.h"
using namespace DirectX::PackedVector;
using namespace DirectX;

const int gNumFrameResources = 4;
enum RenderLayer : int
{
	Opaque,
	Transparent,
	AlphaTest,
	TreeBillboardAlphaTest,
	Count
};
struct RenderItem
{
	RenderItem() = default;
	XMFLOAT4X4 world = MathHelper::Identity4x4();

	UINT objCBIndex = -1;
	D3D12_PRIMITIVE_TOPOLOGY primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	int NumFramesDirty = gNumFrameResources;

	MeshGeometry* geo = nullptr;
	Material* mat = nullptr;
	XMFLOAT4X4 texTransform = MathHelper::Identity4x4();

	UINT indexCount = 0;
	UINT startIndexLocation = 0;
	UINT baseVertexLocation = 0;
};


class GaussianBlur : public D3DApp
{
public:
	GaussianBlur();
	~GaussianBlur();
	virtual bool Init(HINSTANCE hInstance, int nShowCmd) override;
private:

	virtual void Draw() override;
	virtual void Update() override;
	virtual void OnResize() override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
	void OnKeyboardInput(const GameTime& gt);

	void UpdateObjectCBs();
	void UpdateMainPassCB();
	void UpdateMatCB();

	void UpdateWaves(const GameTime& gt);
	void BuildWavesGeometryBuffers();

	void AnimateMaterials(const GameTime& gt);

	ComPtr<ID3D12Resource> CreateDefaultBuffer(UINT64 byteSize, const void* initData, ComPtr<ID3D12Resource>& uploadBuffer);
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildPostProcessRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildBoxGeometry();
	void BuildTreeBillboardGeometry();

	void BuildMaterials();
	void LoadTextures();
	void BuildRenderItem();
	void BuildPSO();
	void DrawRenderItems(const std::vector<RenderItem*>& ritems);
	void BuildFrameResources();

	float GetHillsHeight(float x, float z);
	XMFLOAT3 GetHillsNormal(float x, float z) const;
protected:
	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12RootSignature> postProcessRootSignature;
	//ComPtr<ID3D12PipelineState> pipelineState;
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayoutDesc;
	std::vector<D3D12_INPUT_ELEMENT_DESC> treeBillboardInputLayoutDesc;
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	std::unique_ptr<Waves> mWaves;
	RenderItem* mWavesRitem = nullptr;

	std::unique_ptr<BlurFilter> mBlurFilter;

	std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> psos;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> materials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
	std::vector<RenderItem*> ritemLayer[(int)RenderLayer::Count];
	std::vector<std::unique_ptr<RenderItem>> allRitems;
	UINT objCount = 0;

	ComPtr<ID3D12DescriptorHeap> cbvHeap;
	ComPtr<ID3D12DescriptorHeap> srvHeap;

	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<PassConstants>> mPassCB = nullptr;
	PassConstants passConstants;

	// MVP
	XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	// syn
	int mCurrFrameResourceIndex = 0;
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;

	POINT lastMousePos;

	// sun 
	float sunTheta = 1.25f * XM_PI;
	float sunPhi = XM_PIDIV4;

	float theta = 1.5f * XM_PI;;
	float phi = XM_PIDIV4 - 0.1;
	float radius = 50.0;
};

