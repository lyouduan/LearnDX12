#pragma once
#include "./stdafx.h"

using namespace Microsoft::WRL;
using namespace DirectX;
class D3DApp
{
public:
	D3DApp();
	virtual ~D3DApp();
	int Run();
	virtual bool Init(HINSTANCE hInstance, int nShowCmd);
	bool InitWindow(HINSTANCE hInstance, int nShowCmd);
	bool InitDirectX();

	virtual LRESULT CALLBACK MsgPro(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static D3DApp* GetApp();


	virtual void Draw();
	virtual void OnResize();
	virtual void Update();

	virtual void OnMouseDown(WPARAM btnState, int x, int y) {}
	virtual void OnMouseUp(WPARAM btnState, int x, int y) {}
	virtual void OnMouseMove(WPARAM btnState, int x, int y) {}

	void CreateDevice();
	void CreateFence();
	void GetDescriptorSize();
	void SetMSAA();
	void CreateCommandObject();
	void CreateSwapChain();
	virtual void CreateDescriptorHeap();
	virtual void CreateRTV();
	virtual void CreateDSV();
	void CreateViewPortAndScissorRect();

	void FlushCmdQueue();
	void CalculateFrameState();

protected:
	static D3DApp* mApp;
	static const UINT FrameCount = 2;
	std::wstring mMainWndCaption = L"d3d App";
	HWND mhMainWnd = 0;
	int width = 1280;
	int height = 720;
	bool      mAppPaused = false;  // is the application paused?
	bool      mMinimized = false;  // is the application minimized?
	bool      mMaximized = false;  // is the application maximized?
	bool      mResizing = false;   // are the resize bars being dragged?
	bool      mFullscreenState = false;// fullscreen enabled

	
	GameTime gt;

	ComPtr<IDXGIFactory4> dxgiFactory;
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12CommandQueue> cmdQueue;
	ComPtr<ID3D12CommandAllocator> cmdAllocator;
	ComPtr<ID3D12GraphicsCommandList> cmdList;
	ComPtr<IDXGISwapChain> swapChain;
	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	ComPtr<ID3D12DescriptorHeap> dsvHeap;


	ComPtr<ID3D12Resource> swapChainBuffer[FrameCount];
	ComPtr<ID3D12Resource> depthStencilBuffer;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle;

	ComPtr<ID3D12Fence> fence;
	HANDLE eventHandle;
	UINT mCurrentFence = 0;

	D3D12_VIEWPORT viewPort;
	D3D12_RECT scissorRect;

	UINT rtvDescriptorSize = 0;
	UINT dsvDescriptorSize = 0;
	UINT cbvDescriptorSize = 0;
	UINT srvDescriptorSize = 0;
	UINT mCurrentBackBuffer = 0;

	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
};