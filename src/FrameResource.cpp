#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objCount, UINT matCount, UINT waveVertCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&cmdAllocator)
	));

	objCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objCount, true);
	passCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);

	WavesVB = std::make_unique<UploadBuffer<Vertex>>(device, waveVertCount, false);//һ�����㼴һ���ӻ�����

	matCB = std::make_unique<UploadBuffer<MatConstants>>(device, matCount, true);

}
FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objCount, UINT matCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&cmdAllocator)
	));

	objCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objCount, true);
	passCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	matCB = std::make_unique<UploadBuffer<MatConstants>>(device, matCount, true);

}
FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&cmdAllocator)
	));

	objCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objCount, true);
	passCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
}

FrameResource::~FrameResource()
{
}
