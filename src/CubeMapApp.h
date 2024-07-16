#pragma once
#include "D3DApp.h"
using namespace DirectX::PackedVector;

const int gNumFrameResources = 4;
enum RenderLayer : int
{
	Opaque,
	Transparent,
	AlphaTest,
	SkyBox,
	Count
};
struct RenderItem
{
	RenderItem() = default;
	XMFLOAT4X4 world = MathHelper::Identity4x4();
	XMFLOAT4X4 texTransform = MathHelper::Identity4x4();

	UINT objCBIndex = -1;
	D3D12_PRIMITIVE_TOPOLOGY primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	MeshGeometry* geo = nullptr;
	Material* mat = nullptr;

	int NumFramesDirty = gNumFrameResources;

	UINT indexCount = 0;
	UINT startIndexLocation = 0;
	UINT baseVertexLocation = 0;
};


class CubeMapApp : public D3DApp
{
public:
	CubeMapApp();
	~CubeMapApp();
	virtual bool Init(HINSTANCE hInstance, int nShowCmd) override;
private:

	virtual void Draw() override;
	virtual void Update() override;
	virtual void OnResize() override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
	void onKeybordInput(const GameTime& gt);

	void UpdateObjectCBs();
	void UpdateMainPassCB();
	void UpdateMatCB();

	void loadTexutres();

	ComPtr<ID3D12Resource> CreateDefaultBuffer(UINT64 byteSize, const void* initData, ComPtr<ID3D12Resource>& uploadBuffer);
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildSkySphereGeometry();

	void BuildMaterials();
	void BuildSkullGeometry();
	void BuildRenderItem();
	void BuildPSO();
	void DrawRenderItems(std::vector<RenderItem*>);
	void BuildFrameResources();
protected:
	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12PipelineState> pipelineState;
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayoutDesc;
	std::vector<D3D12_INPUT_ELEMENT_DESC> gsInputLayoutDesc;

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	std::vector<RenderItem*> ritemLayer[(int)RenderLayer::Count];
	RenderItem*  mSkullRitem;

	std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> psos;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> materials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures;

	std::vector<std::unique_ptr<RenderItem>> allRitems;
	UINT objCount = 0;
	UINT matCount = 0;
	UINT mSkyTexHeapIndex = 0;

	ComPtr<ID3D12DescriptorHeap> cbvHeap;
	ComPtr<ID3D12DescriptorHeap> srvHeap;

	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<PassConstants>> mPassCB = nullptr;
	PassConstants passConstants;
	MaterialData matData;

	// syn
	int mCurrFrameResourceIndex = 0;
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;

	POINT lastMousePos;

	float sunTheta = 1.25f * XM_PI;
	float sunPhi = XM_PIDIV4;

	Camera camera;
};

