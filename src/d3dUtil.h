#pragma once
#include "stdafx.h"
#include <type_traits>

using namespace Microsoft::WRL;
using namespace DirectX;


template<typename T>
UINT CalcConstantBufferByteSize()
{
    UINT byteSize = sizeof(T);
    return (byteSize + 255) & ~255;
}

template<typename T>
    requires(!std::is_lvalue_reference_v<T>)
T* get_rvalue_ptr(T&& v) {
    return &v;
}

inline std::string HrToString(HRESULT hr)
{
    char s_str[64] = {};
    sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
    return std::string(s_str);
}

class HrException : public std::runtime_error
{
public:
    HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
    HRESULT Error() const { return m_hr; }
private:
    const HRESULT m_hr;
};

#define SAFE_RELEASE(p) if (p) (p)->Release()

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw HrException(hr);
    }
}

struct SubmeshGeometry
{
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    INT BaseVertexLocation = 0;

    BoundingBox Bounds;
    // Bounding box of the geometry defined by this submesh. 
    // This is used in later chapters of the book.
    //DirectX::BoundingBox Bounds;
};

struct MeshGeometry
{
    std::string name;

    ComPtr<ID3DBlob> vertexBufferCpu = nullptr;  // CPU内存上顶点数据
    ComPtr<ID3D12Resource> vertexBufferUploader = nullptr; // GPU上传堆
    ComPtr<ID3D12Resource> vertexBufferGpu = nullptr; // GPU默认缓冲区

    ComPtr<ID3DBlob> indexBufferCpu = nullptr;  // CPU内存上顶点数据
    ComPtr<ID3D12Resource> indexBufferUploader = nullptr; // GPU上传堆
    ComPtr<ID3D12Resource> indexBufferGpu = nullptr; // GPU默认缓冲区

    UINT vertexBufferByteSize = 0;
    UINT vertexByteStride = 0;
    UINT indexBufferByteSize = 0;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;

    std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

    D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
    {
        D3D12_VERTEX_BUFFER_VIEW vbv;
        vbv.BufferLocation = vertexBufferGpu->GetGPUVirtualAddress();
        vbv.SizeInBytes = vertexBufferByteSize;
        vbv.StrideInBytes = vertexByteStride;
        return vbv;
    }

    D3D12_INDEX_BUFFER_VIEW IndexBufferView() const
    {
        D3D12_INDEX_BUFFER_VIEW ibv;
        ibv.BufferLocation = indexBufferGpu->GetGPUVirtualAddress();
        ibv.Format = indexFormat;
        ibv.SizeInBytes = indexBufferByteSize;
        return ibv;
    }

    // free the memory after we finish upload to the GPU
    void DisposeUploaders()
    {
        vertexBufferUploader = nullptr;
        indexBufferUploader  = nullptr;
    }
};

struct Material
{
    std::string name;
    int matCBIndex = -1; // 材质常量缓冲区索引
    int numFramesDirty = 4; // 更新脏数据标志

    int diffuseSrvHeapIndex = -1;
    int normalSrvHeapIndex = -1;

    XMFLOAT4 diffuseAlbedo = { 1.0, 1.0, 1.0, 1.0 };
    XMFLOAT3 fresnelR0 = { 0.01, 0.01, 0.01 };
    float roughness = 0.25; 

    XMFLOAT4X4 matTransform = MathHelper::Identity4x4();
};

#define MAX_LIGHTS 16
struct Light 
{
    XMFLOAT3 strength = { 0.5, 0.5, 0.5 };
    float falloffStart = 1.0f;
    XMFLOAT3 direction = { 0.0, -1.0, 0.0 };
    float fallOffEnd = 10.0f;
    XMFLOAT3 position = { 0.0, 0.0, 0.0 };
    float spotPower = 64.0f;
};

struct Texture
{
    std::string name;
    std::wstring Filename;

    ComPtr<ID3D12Resource> Resource = nullptr;
    ComPtr<ID3D12Resource> UploadHeap = nullptr;

};

static ComPtr<ID3DBlob> CompileShader(
    const std::wstring& filename,
    const D3D_SHADER_MACRO* defines,
    const std::string& entrypoint,
    const std::string& target)
{
    UINT compileFlags = 0;

#if defined(DEBUG) || defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = S_OK;
    ComPtr<ID3DBlob> byteCode = nullptr;
    ComPtr<ID3DBlob> errors;

    hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

    if (errors != nullptr)
        OutputDebugStringA((char*)errors->GetBufferPointer());

    ThrowIfFailed(hr);

    return byteCode;
}