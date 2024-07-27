#include "ShadowRenderTarget.h"

ShadowRenderTarget::ShadowRenderTarget(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
	mDevice = device;
	mWidth = width;
	mHeight = height;

	mViewport = { 0.0, 0.0, (float)width, (float)height, 0.0, 1.0 };
	mScissorRect = { 0, 0, (int)width, (int)height };
	BuildResource();
}

ID3D12Resource* ShadowRenderTarget::Resource()
{
	return Shadow2Depth.Get();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowRenderTarget::Srv()
{
	return mhGpuSrv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowRenderTarget::Rtv()
{
	return mhCpuRtv;
}

D3D12_VIEWPORT ShadowRenderTarget::Viewport() const
{
	return mViewport;
}

D3D12_RECT ShadowRenderTarget::ScissorRect() const
{
	return mScissorRect;
}

void ShadowRenderTarget::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv, CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv)
{
	mhCpuSrv = hCpuSrv;
	mhGpuSrv = hGpuSrv;
	mhCpuRtv = hCpuRtv;

	BuildDescriptors();
}

void ShadowRenderTarget::OnResize(UINT newWidth, UINT newHeight)
{
	if ((mWidth != newWidth) || (mHeight != newHeight))
	{
		mWidth = newWidth;
		mHeight = newHeight;

		BuildResource();
		BuildDescriptors();
	}
}

void ShadowRenderTarget::BuildDescriptors()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0;

	mDevice->CreateShaderResourceView(Shadow2Depth.Get(), &srvDesc, mhCpuSrv);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = mFormat;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	mDevice->CreateRenderTargetView(Shadow2Depth.Get(), &rtvDesc, mhCpuRtv);
}

void ShadowRenderTarget::BuildResource()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = mFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// loadup map to default heaps
	ThrowIfFailed(mDevice->CreateCommittedResource(
		get_rvalue_ptr(D3D12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&Shadow2Depth)
	));
}
