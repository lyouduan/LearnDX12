#pragma once
#include "stdafx.h"

template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer);
	~UploadBuffer();

	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;

	ID3D12Resource* Resource() const;
	void CopyData(int elementIndex, const T& data);
private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
	BYTE* mMappedData = nullptr;

	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};

template<typename T>
inline UploadBuffer<T>::UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
	:mIsConstantBuffer(isConstantBuffer)
{
	mElementByteSize = sizeof(T);
	if (isConstantBuffer)
		mElementByteSize = CalcConstantBufferByteSize<T>();
	ThrowIfFailed(device->CreateCommittedResource(
		get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
		D3D12_HEAP_FLAG_NONE,
		get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize*elementCount)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mUploadBuffer)
	));

	ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));
}

template<typename T>
inline UploadBuffer<T>::~UploadBuffer()
{
	if (mUploadBuffer != nullptr)
		mUploadBuffer->Unmap(0, nullptr);

	mMappedData = nullptr;
}

template<typename T>
inline ID3D12Resource* UploadBuffer<T>::Resource() const
{
	return mUploadBuffer.Get();
}

template<typename T>
inline void UploadBuffer<T>::CopyData(int elementIndex, const T& data)
{
	memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
}

