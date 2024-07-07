#include "GaussianBlur.h"

GaussianBlur::GaussianBlur()
{
}

GaussianBlur::~GaussianBlur()
{
}

bool GaussianBlur::Init(HINSTANCE hInstance, int nShowCmd)
{
	if (!D3DApp::Init(hInstance, nShowCmd))
		return false;
	ThrowIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
	mBlurFilter = std::make_unique<BlurFilter>(device.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

	LoadTextures();
	BuildRootSignature();
	BuildPostProcessRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildBoxGeometry();
	BuildMaterials();
	BuildWavesGeometryBuffers();
	BuildTreeBillboardGeometry();

	BuildRenderItem();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildPSO();

	ThrowIfFailed(cmdList->Close());
	ID3D12CommandList* cmdLists[] = { cmdList.Get() };
	cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	FlushCmdQueue();

	return true;
}

void GaussianBlur::Draw()
{
	auto currCmdAllocator = mCurrFrameResource->cmdAllocator;
	ThrowIfFailed(currCmdAllocator->Reset());
	ThrowIfFailed(cmdList->Reset(currCmdAllocator.Get(), psos["opaque"].Get()));

	cmdList->RSSetViewports(1, &viewPort);
	cmdList->RSSetScissorRects(1, &scissorRect);

	UINT& ref_mCurrentBackBuffer = mCurrentBackBuffer;
	auto trans1 = CD3DX12_RESOURCE_BARRIER::Transition(
		swapChainBuffer[ref_mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT,//转换资源为后台缓冲区资源
		D3D12_RESOURCE_STATE_RENDER_TARGET);//从呈现到渲染目标转换
	cmdList->ResourceBarrier(1, &trans1);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart(), ref_mCurrentBackBuffer, rtvDescriptorSize);
	//const float clearColor[] = { 0.4f, 0.2f, 0.2f, 1.0f };
	cmdList->ClearRenderTargetView(rtvHandle, (float*)&passConstants.fogColor, 0, nullptr);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cmdList->ClearDepthStencilView(
		dsvHandle,	//DSV描述符句柄
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,	//FLAG
		1.0f,	//默认深度值
		0,	//默认模板值
		0,	//裁剪矩形数量
		nullptr);	//裁剪矩形指针

	cmdList->OMSetRenderTargets(1,//待绑定的RTV数量
		&rtvHandle,	//指向RTV数组的指针
		true,	//RTV对象在堆内存中是连续存放的
		&dsvHandle);	//指向DSV的指针

	ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
	cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

	cmdList->SetGraphicsRootSignature(rootSignature.Get());

	auto passCB = mCurrFrameResource->passCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());

	cmdList->SetPipelineState(psos["opaque"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::Opaque]);

	cmdList->SetPipelineState(psos["alphaTested"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::AlphaTest]);

	cmdList->SetPipelineState(psos["transparent"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::Transparent]);

	mBlurFilter->Execute(cmdList.Get(), postProcessRootSignature.Get(),
		psos["horzBlur"].Get(), psos["vertBlur"].Get(), swapChainBuffer[ref_mCurrentBackBuffer].Get(), 10);

	cmdList->ResourceBarrier(
		1,
		get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(
			swapChainBuffer[ref_mCurrentBackBuffer].Get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_COPY_DEST))
	);
	cmdList->CopyResource(swapChainBuffer[ref_mCurrentBackBuffer].Get(), mBlurFilter->Output());

	auto trans2 = CD3DX12_RESOURCE_BARRIER::Transition(swapChainBuffer[ref_mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	cmdList->ResourceBarrier(1, &trans2);//从渲染目标到呈现
	ThrowIfFailed(cmdList->Close());

	ID3D12CommandList* cmdLists[] = { cmdList.Get() };
	cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	ThrowIfFailed(swapChain->Present(0, 0));
	ref_mCurrentBackBuffer = (ref_mCurrentBackBuffer + 1) % 2;

	//FlushCmdQueue();
	mCurrFrameResource->fence = ++mCurrentFence;
	cmdQueue->Signal(fence.Get(), mCurrentFence);
}

void GaussianBlur::Update()
{
	OnKeyboardInput(gt);

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	if (mCurrFrameResource->fence != 0 && fence->GetCompletedValue() < mCurrFrameResource->fence)
	{
		eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(fence->SetEventOnCompletion(mCurrFrameResource->fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}


	AnimateMaterials(gt);
	UpdateObjectCBs();
	UpdateMainPassCB();
	UpdateMatCB();
	UpdateWaves(gt);

}

void GaussianBlur::OnResize()
{
	D3DApp::OnResize();
	//构建投影矩阵
	XMMATRIX p = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(width) / height, 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, p);

	if (mBlurFilter != nullptr)
	{
		mBlurFilter->OnResize((UINT)width, (UINT)height);
	}
}

void GaussianBlur::OnMouseDown(WPARAM btnState, int x, int y)
{
	lastMousePos.x = x;
	lastMousePos.y = y;
	SetCapture(mhMainWnd);
}

void GaussianBlur::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void GaussianBlur::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(0.25 * static_cast<float>(lastMousePos.x - x));
		float dy = XMConvertToRadians(0.25 * static_cast<float>(lastMousePos.y - y));
		theta += dx;
		phi += dy;
		theta = MathHelper::Clamp(theta, 0.1f, XM_PI - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.02f * static_cast<float>(x - lastMousePos.x);
		float dy = 0.02f * static_cast<float>(y - lastMousePos.y);
		//根据鼠标输入更新摄像机可视范围半径
		radius += dx - dy;
		//限制可视范围半径
		radius = MathHelper::Clamp(radius, 5.0f, 150.0f);
	}
	lastMousePos.x = x;
	lastMousePos.y = y;
}

void GaussianBlur::OnKeyboardInput(const GameTime& gt)
{
	const float dt = gt.DeltaTime();

	//左右键改变平行光的Theta角，上下键改变平行光的Phi角
	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
		sunTheta -= 1.0f * dt;
	if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
		sunTheta += 1.0f * dt;
	if (GetAsyncKeyState(VK_UP) & 0x8000)
		sunPhi -= 1.0f * dt;
	if (GetAsyncKeyState(VK_DOWN) & 0x8000)
		sunPhi += 1.0f * dt;

	//将Phi约束在[0, PI/2]之间
	sunPhi = MathHelper::Clamp(sunPhi, 0.1f, XM_PIDIV2);
}

void GaussianBlur::UpdateObjectCBs()
{

	auto currObjectCB = mCurrFrameResource->objCB.get();
	ObjectConstants objConstants;
	for (auto& e : allRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			mWorld = e->world;
			XMMATRIX w = XMLoadFloat4x4(&mWorld) * XMMatrixTranslation(0.0, -20.0, 50.f);
			//XMMATRIX赋值给XMFLOAT4X4
			XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(w));

			XMFLOAT4X4 texTransform = e->texTransform;
			XMMATRIX texTransMatrix = XMLoadFloat4x4(&texTransform);
			XMStoreFloat4x4(&objConstants.texTransform, XMMatrixTranspose(texTransMatrix));

			//将数据拷贝至GPU缓存
			currObjectCB->CopyData(e->objCBIndex, objConstants);

			e->NumFramesDirty--;
		}
	}
}

void GaussianBlur::UpdateMainPassCB()
{
	float y = radius * cosf(phi);
	float x = radius * sinf(phi) * cosf(theta);
	float z = radius * sinf(phi) * sinf(theta);

	XMVECTOR pos = XMVectorSet(x, y, z, 1.0);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0, 1.0, 0.0, 0.0);
	XMMATRIX view = XMMatrixLookAtLH(pos, target, up); // 左手坐标系

	//  viewProjection
	XMStoreFloat4x4(&mView, view);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX vp = view * proj;
	XMStoreFloat4x4(&passConstants.viewProj, XMMatrixTranspose(vp));

	passConstants.eyePosW = { x, y, z };

	// light
	passConstants.ambientLight = { 0.25f,0.25f,0.35f,1.0f };
	passConstants.lights[0].direction = { 0.57735f, -0.57735f, 0.57735f };
	passConstants.lights[0].strength = { 0.9f, 0.9f, 0.9f };
	passConstants.lights[1].direction = { -0.57735f, -0.57735f, 0.57735f };
	passConstants.lights[1].strength = { 0.5f, 0.5f, 0.5f };
	passConstants.lights[2].direction = { 0.0f, -0.707f, -0.707f };
	passConstants.lights[2].strength = { 0.2f, 0.2f, 0.2f };
	XMVECTOR sunDir = -MathHelper::SphericalToCartesian(1.0, sunTheta, sunPhi);
	XMStoreFloat3(&passConstants.lights[0].direction, sunDir);

	passConstants.fogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	passConstants.fogStart = 20.0f;
	passConstants.fogRange = 200.f;
	mCurrFrameResource->passCB->CopyData(0, passConstants);
}

void GaussianBlur::UpdateMatCB()
{
	for (auto& m : materials)
	{
		Material* mat = m.second.get();

		if (mat->numFramesDirty > 0)
		{
			MatConstants matConstants;
			matConstants.diffuseAlbedo = mat->diffuseAlbedo;
			matConstants.fresnelR0 = mat->fresnelR0;
			matConstants.roughness = mat->roughness;

			XMMATRIX matTransform = XMLoadFloat4x4(&mat->matTransform);

			XMStoreFloat4x4(&matConstants.matTransform, XMMatrixTranspose(matTransform));

			mCurrFrameResource->matCB->CopyData(mat->matCBIndex, matConstants);

			mat->numFramesDirty--;
		}
	}
}

void GaussianBlur::UpdateWaves(const GameTime& gt)
{
	static float t_base = 0.0f;
	if ((gt.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;	//0.25秒生成一个波浪
		//随机生成横坐标
		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		//随机生成纵坐标
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);
		//随机生成波的半径
		float r = MathHelper::RandF(0.2f, 0.5f);//float用RandF函数
		//使用波动方程函数生成波纹
		mWaves->Disturb(i, j, r);
	}

	mWaves->Update(gt.DeltaTime());

	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;
		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);
		v.Tex.x = 0.5f + v.Pos.x / mWaves->Width();
		v.Tex.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->geo->vertexBufferGpu = currWavesVB->Resource();
}

void GaussianBlur::BuildWavesGeometryBuffers()
{
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount());

	assert(mWaves->VertexCount() < 0x0000ffff);

	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; i++)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);
	auto geo = std::make_unique<MeshGeometry>();
	geo->name = "lakeGeo";

	// Set dynamically.
	geo->vertexBufferCpu = nullptr;
	geo->vertexBufferGpu = nullptr;

	geo->indexBufferGpu = CreateDefaultBuffer(ibByteSize, indices.data(), geo->indexBufferUploader);

	geo->vertexBufferByteSize = vbByteSize;
	geo->vertexByteStride = sizeof(Vertex);
	geo->indexBufferByteSize = ibByteSize;
	geo->indexFormat = DXGI_FORMAT_R16_UINT;

	SubmeshGeometry lakeSubmesh;
	lakeSubmesh.IndexCount = (UINT)indices.size();
	lakeSubmesh.StartIndexLocation = 0;
	lakeSubmesh.BaseVertexLocation = 0;

	geo->DrawArgs["lakeGrid"] = lakeSubmesh;

	geometries["lakeGeo"] = std::move(geo);

}

void GaussianBlur::AnimateMaterials(const GameTime& gt)
{
	auto matLake = materials["water"].get();
	float& du = matLake->matTransform(3, 0);
	float& dv = matLake->matTransform(3, 1);

	du += 0.1f * gt.DeltaTime();
	dv += 0.02f * gt.DeltaTime();

	if (du >= 1.0f)
		du = 0.0f;
	if (dv >= 1.0f)
		dv = 0.0f;
	//将du和dv存入矩阵
	matLake->matTransform(3, 0) = du;
	matLake->matTransform(3, 1) = dv;

	matLake->numFramesDirty = 3;

}

ComPtr<ID3D12Resource> GaussianBlur::CreateDefaultBuffer(UINT64 byteSize, const void* initData, ComPtr<ID3D12Resource>& uploadBuffer)
{
	// 创建上传堆
	ThrowIfFailed(device->CreateCommittedResource(
		get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
		D3D12_HEAP_FLAG_NONE,
		get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(byteSize)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer)
	));

	// 默认堆
	ComPtr<ID3D12Resource> defaultBuffer;
	ThrowIfFailed(device->CreateCommittedResource(
		get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
		D3D12_HEAP_FLAG_NONE,
		get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(byteSize)),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&defaultBuffer)
	));

	// 从数据从CPU内存拷贝到GPU缓存
	D3D12_SUBRESOURCE_DATA subResourceData;
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;
	// 转换资源状态
	cmdList->ResourceBarrier(1,
		get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(
			defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST
		)));
	// 将数据从上传堆复制到默认堆
	UpdateSubresources<1>(cmdList.Get(), defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

	// 转换资源状态
	cmdList->ResourceBarrier(1,
		get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(
			defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ))
	);

	return defaultBuffer;
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GaussianBlur::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

void GaussianBlur::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = (objCount + 1) * gNumFrameResources; // (objCB + passCB) * frameCounts
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&cbvHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 7; // 3 + 4
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)));
}

void GaussianBlur::BuildConstantBuffers()
{
	// obj cBuffer
	UINT objCBByteSize = CalcConstantBufferByteSize<ObjectConstants>();
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress;
	for (int frameIndex = 0; frameIndex < gNumFrameResources; frameIndex++)
	{
		auto mObjectCB = mFrameResources[frameIndex]->objCB->Resource();
		for (int i = 0; i < objCount; i++)
		{
			cbAddress = mObjectCB->GetGPUVirtualAddress();

			cbAddress += i * objCBByteSize;

			auto cbvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbvHeap->GetCPUDescriptorHandleForHeapStart());
			cbvHandle.Offset(objCount * frameIndex + i, cbvDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			device->CreateConstantBufferView(&cbvDesc, cbvHandle);
		}
	}

	int passOffset = objCount * gNumFrameResources;

	for (int frameIndex = 0; frameIndex < gNumFrameResources; frameIndex++)
	{
		// pass cBuffer
		auto mPassCB = mFrameResources[frameIndex]->passCB->Resource();

		UINT passCBByteSize = CalcConstantBufferByteSize<PassConstants>();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress2 = mPassCB->GetGPUVirtualAddress();

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc2;
		cbvDesc2.BufferLocation = cbAddress2;
		cbvDesc2.SizeInBytes = passCBByteSize;

		auto cbvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbvHeap->GetCPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(passOffset + frameIndex, cbvDescriptorSize);

		device->CreateConstantBufferView(&cbvDesc2, cbvHandle);
	}

	auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvHeap->GetCPUDescriptorHandleForHeapStart());
	auto grassTex = textures["grassTex"]->Resource;
	auto waterTex = textures["waterTex"]->Resource;
	auto fenceTex = textures["fenceTex"]->Resource;
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = grassTex->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = -1;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		device->CreateShaderResourceView(grassTex.Get(), &srvDesc, srvHandle);

		srvHandle.Offset(1, srvDescriptorSize);
		srvDesc.Format = waterTex->GetDesc().Format;
		device->CreateShaderResourceView(waterTex.Get(), &srvDesc, srvHandle);

		srvHandle.Offset(1, srvDescriptorSize);
		srvDesc.Format = fenceTex->GetDesc().Format;
		device->CreateShaderResourceView(fenceTex.Get(), &srvDesc, srvHandle);

	}
	auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvHeap->GetCPUDescriptorHandleForHeapStart());
	auto handle2 = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvHeap->GetGPUDescriptorHandleForHeapStart());
	mBlurFilter->BuildDescriptors(
		handle,
		3,
		handle2,
		3,
		srvDescriptorSize);

}

void GaussianBlur::BuildRootSignature()
{
	// 每个根参数只能绑定一个寄存器空间中的特定类型资源
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	//CD3DX12_DESCRIPTOR_RANGE cbvTable[2];
	//cbvTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	//cbvTable[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
	//slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable[0]);
	//slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable[1]);

	auto staticSamplers = GetStaticSamplers();
	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &srvTable);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);


	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4,
		slotRootParameter,
		(UINT)staticSamplers.size(),
		staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> error = nullptr;
	ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &error));
	if (error != nullptr)
	{
		OutputDebugStringA((char*)error->GetBufferPointer());
	}

	ThrowIfFailed(device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature)
	));
}

void GaussianBlur::BuildPostProcessRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE uavTable;
	uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	slotRootParameter[0].InitAsConstants(12, 0);
	slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);
	slotRootParameter[2].InitAsDescriptorTable(1, &uavTable);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3,
		slotRootParameter,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(postProcessRootSignature.GetAddressOf())
	));
}

void GaussianBlur::BuildShadersAndInputLayout()
{

	inputLayoutDesc =
	{
		  { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	treeBillboardInputLayoutDesc =
	{
		  { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "SIZE",   0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		"FOG", "1",
		NULL, NULL
	};

	shaders["standardVS"] = CompileShader(std::wstring(L"shader/default.hlsl"), nullptr, "VSMain", "vs_5_0");
	shaders["opaquePS"] = CompileShader(std::wstring(L"shader/default.hlsl"), defines, "PSMain", "ps_5_0");
	shaders["alphaTestedPS"] = CompileShader(std::wstring(L"shader/default.hlsl"), alphaTestDefines, "PSMain", "ps_5_0");

	shaders["treeBillboardVS"] = CompileShader(std::wstring(L"shader/treeBillboard.hlsl"), nullptr, "VS", "vs_5_0");
	shaders["treeBillboardGS"] = CompileShader(std::wstring(L"shader/treeBillboard.hlsl"), nullptr, "GS", "gs_5_0");
	shaders["treeBillboardPS"] = CompileShader(std::wstring(L"shader/treeBillboard.hlsl"), alphaTestDefines, "PS", "ps_5_0");
	
	
	shaders["horzBlurCS"] = CompileShader(std::wstring(L"shader/gaussion.hlsl"), nullptr, "HorzBlurCS", "cs_5_0");
	shaders["vertBlurCS"] = CompileShader(std::wstring(L"shader/gaussion.hlsl"), nullptr, "VertBlurCS", "cs_5_0");
}

void GaussianBlur::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

	// concatenate all the geometry into one big vertex/index buffer
	// **********************

	size_t vertexCount = grid.Vertices.size();

	std::vector<Vertex> vertices(vertexCount);

	for (size_t i = 0; i < vertexCount; ++i)
	{
		vertices[i].Pos = grid.Vertices[i].Position;
		vertices[i].Pos.y = GetHillsHeight(vertices[i].Pos.x, vertices[i].Pos.z);
		vertices[i].Normal = GetHillsNormal(vertices[i].Pos.x, vertices[i].Pos.z);
		vertices[i].Tex = grid.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = grid.GetIndices16();

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->name = "landGeo";

	geo->vertexBufferGpu = CreateDefaultBuffer(vbByteSize, vertices.data(), geo->vertexBufferUploader);
	geo->indexBufferGpu = CreateDefaultBuffer(ibByteSize, indices.data(), geo->indexBufferUploader);

	geo->vertexBufferByteSize = vbByteSize;
	geo->vertexByteStride = sizeof(Vertex);
	geo->indexBufferByteSize = ibByteSize;
	geo->indexFormat = DXGI_FORMAT_R16_UINT;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)indices.size();
	gridSubmesh.StartIndexLocation = 0;
	gridSubmesh.BaseVertexLocation = 0;

	geo->DrawArgs["landGrid"] = gridSubmesh;
	geometries["landGeo"] = std::move(geo);
}

void GaussianBlur::BuildBoxGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); i++)
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].Tex = box.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->name = "boxGeo";

	geo->vertexBufferGpu = CreateDefaultBuffer(vbByteSize, vertices.data(), geo->vertexBufferUploader);
	geo->indexBufferGpu = CreateDefaultBuffer(ibByteSize, indices.data(), geo->indexBufferUploader);

	geo->vertexBufferByteSize = vbByteSize;
	geo->vertexByteStride = sizeof(Vertex);
	geo->indexBufferByteSize = ibByteSize;
	geo->indexFormat = DXGI_FORMAT_R16_UINT;

	SubmeshGeometry submesh;
	submesh.BaseVertexLocation = 0;
	submesh.StartIndexLocation = 0;
	submesh.IndexCount = (UINT)indices.size();

	geo->DrawArgs["box"] = submesh;

	geometries["boxGeo"] = std::move(geo);
}

void GaussianBlur::BuildTreeBillboardGeometry()
{
	struct TreeBillboardVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	const int treeCount = 16;

	std::array<TreeBillboardVertex, treeCount> vertices;
	for (UINT i = 0; i < treeCount; i++)
	{
		float x = MathHelper::RandF(-45.0f, 45.0f);
		float z = MathHelper::RandF(-45.0f, 45.0f);
		float y = GetHillsHeight(x, z);
		y -= 12.0f;
		z += 50.0f;

		vertices[i].Pos = XMFLOAT3(x, y, z);
		vertices[i].Size = XMFLOAT2(15.0f, 15.0f);
	}

	std::array<std::uint16_t, treeCount> indices;
	UINT j = 0;
	for (UINT i = 0; i < treeCount; i++)
	{
		indices[i] = i;
	}

	const UINT vbByteSize = (UINT)treeCount * sizeof(TreeBillboardVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);


	auto geo = std::make_unique<MeshGeometry>();

	geo->name = "treeBillboardGeo";
	geo->vertexBufferByteSize = vbByteSize;
	geo->vertexByteStride = sizeof(TreeBillboardVertex);
	geo->indexBufferByteSize = ibByteSize;
	geo->indexFormat = DXGI_FORMAT_R16_UINT;

	geo->vertexBufferGpu = CreateDefaultBuffer(vbByteSize, vertices.data(), geo->vertexBufferUploader);
	geo->indexBufferGpu = CreateDefaultBuffer(ibByteSize, indices.data(), geo->indexBufferUploader);

	SubmeshGeometry submesh;
	submesh.BaseVertexLocation = 0;
	submesh.StartIndexLocation = 0;
	submesh.IndexCount = (UINT)indices.size();
	geo->DrawArgs["treeBillboard"] = submesh;

	geometries[geo->name] = std::move(geo);

}

void GaussianBlur::BuildMaterials()
{

	auto grass = std::make_unique<Material>();
	grass->name = "grass";
	grass->matCBIndex = 0;
	grass->diffuseSrvHeapIndex = 0;
	grass->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->fresnelR0 = XMFLOAT3(0.01, 0.01, 0.01);
	grass->roughness = 0.125f;

	auto water = std::make_unique<Material>();
	water->name = "water";
	water->matCBIndex = 1;
	water->diffuseSrvHeapIndex = 1;
	water->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	water->fresnelR0 = XMFLOAT3(0.2, 0.2, 0.2);
	water->roughness = 0.0f;

	auto WireFence = std::make_unique<Material>();
	WireFence->name = "WireFence";
	WireFence->matCBIndex = 2;
	WireFence->diffuseSrvHeapIndex = 2;
	WireFence->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	WireFence->fresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	WireFence->roughness = 0.25f;

	auto treeBillboard = std::make_unique<Material>();
	treeBillboard->name = "treeBillboard";
	treeBillboard->matCBIndex = 3;
	treeBillboard->diffuseSrvHeapIndex = 3;
	treeBillboard->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeBillboard->fresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeBillboard->roughness = 0.8f;

	materials["WireFence"] = std::move(WireFence);
	materials["grass"] = std::move(grass);
	materials["water"] = std::move(water);
}

void GaussianBlur::LoadTextures()
{
	auto grassTex = std::make_unique<Texture>();
	grassTex->name = "grassTex";
	grassTex->Filename = L"./model/grass.dds";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		grassTex->Filename.c_str(),
		grassTex->Resource,
		grassTex->UploadHeap
	));

	auto waterTex = std::make_unique<Texture>();
	waterTex->name = "waterTex";
	waterTex->Filename = L"./model/water1.dds";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		waterTex->Filename.c_str(),
		waterTex->Resource,
		waterTex->UploadHeap
	));

	auto fenceTex = std::make_unique<Texture>();
	fenceTex->name = "fenceTex";
	fenceTex->Filename = L"./model/WireFence.dds";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		fenceTex->Filename.c_str(),
		fenceTex->Resource,
		fenceTex->UploadHeap
	));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->name = "treeArrayTex";
	treeArrayTex->Filename = L"./model/treeArray.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource,
		treeArrayTex->UploadHeap
	));

	textures[grassTex->name] = std::move(grassTex);
	textures[waterTex->name] = std::move(waterTex);
	textures[fenceTex->name] = std::move(fenceTex);
	textures[treeArrayTex->name] = std::move(treeArrayTex);
}

void GaussianBlur::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = {};
	opaquePsoDesc.InputLayout = { inputLayoutDesc.data(), (unsigned int)inputLayoutDesc.size() };
	opaquePsoDesc.pRootSignature = rootSignature.Get();
	opaquePsoDesc.VS = {
		reinterpret_cast<BYTE*>(shaders["standardVS"]->GetBufferPointer()),
		shaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = {
		reinterpret_cast<BYTE*>(shaders["opaquePS"]->GetBufferPointer()),
		shaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
	CD3DX12_RASTERIZER_DESC Raster(D3D12_DEFAULT);
	//Raster.FillMode = D3D12_FILL_MODE_WIREFRAME;
	//Raster.CullMode = D3D12_CULL_MODE_BACK;
	opaquePsoDesc.RasterizerState = Raster;
	// default blendstate
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);;
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	opaquePsoDesc.SampleDesc.Count = 1;
	opaquePsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psos["opaque"] = nullptr;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&psos["opaque"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;
	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	psos["transparent"] = nullptr;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&psos["transparent"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaders["alphaTestedPS"]->GetBufferPointer()),
		shaders["alphaTestedPS"]->GetBufferSize()
	};

	// notes：disable the back cull, we need to see the back of the transparent obj
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	psos["alphaTested"] = nullptr;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&psos["alphaTested"])));


	// PSO for horizontal blur
	D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPso = {};
	horzBlurPso.pRootSignature = postProcessRootSignature.Get();
	horzBlurPso.CS = {
		reinterpret_cast<BYTE*>(shaders["horzBlurCS"]->GetBufferPointer()),
		shaders["horzBlurCS"]->GetBufferSize()
	};
	horzBlurPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	psos["horzBlur"] = nullptr;
	ThrowIfFailed(device->CreateComputePipelineState(&horzBlurPso, IID_PPV_ARGS(&psos["horzBlur"])));

	// PSO for vertical blur
	D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPso = {};
	vertBlurPso.pRootSignature = postProcessRootSignature.Get();
	vertBlurPso.CS = {
		reinterpret_cast<BYTE*>(shaders["vertBlurCS"]->GetBufferPointer()),
		shaders["vertBlurCS"]->GetBufferSize()
	};
	vertBlurPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	psos["vertBlur"] = nullptr;
	ThrowIfFailed(device->CreateComputePipelineState(&vertBlurPso, IID_PPV_ARGS(&psos["vertBlur"])));

}

void GaussianBlur::BuildRenderItem()
{
	auto landRitem = std::make_unique<RenderItem>();
	landRitem->world = MathHelper::Identity4x4();
	XMStoreFloat4x4(&landRitem->texTransform, XMMatrixScaling(7.0f, 7.f, 1.0f));
	landRitem->objCBIndex = 0;
	landRitem->mat = materials["grass"].get();
	landRitem->geo = geometries["landGeo"].get();
	landRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	landRitem->indexCount = landRitem->geo->DrawArgs["landGrid"].IndexCount;
	landRitem->baseVertexLocation = landRitem->geo->DrawArgs["landGrid"].BaseVertexLocation;
	landRitem->startIndexLocation = landRitem->geo->DrawArgs["landGrid"].StartIndexLocation;
	ritemLayer[(int)RenderLayer::Opaque].push_back(landRitem.get());

	auto lakeRitem = std::make_unique<RenderItem>();
	lakeRitem->world = MathHelper::Identity4x4();
	XMStoreFloat4x4(&lakeRitem->texTransform, XMMatrixScaling(7.0f, 7.f, 1.0f));
	lakeRitem->objCBIndex = 1;
	lakeRitem->mat = materials["water"].get();
	lakeRitem->geo = geometries["lakeGeo"].get();
	lakeRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	lakeRitem->indexCount = lakeRitem->geo->DrawArgs["lakeGrid"].IndexCount;
	lakeRitem->baseVertexLocation = lakeRitem->geo->DrawArgs["lakeGrid"].BaseVertexLocation;
	lakeRitem->startIndexLocation = lakeRitem->geo->DrawArgs["lakeGrid"].StartIndexLocation;

	ritemLayer[(int)RenderLayer::Transparent].push_back(lakeRitem.get());
	mWavesRitem = lakeRitem.get();

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->world, XMMatrixTranslation(0.0f, 4.0f, -7.0f));
	boxRitem->objCBIndex = 2;
	boxRitem->mat = materials["WireFence"].get();
	boxRitem->geo = geometries["boxGeo"].get();
	boxRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->indexCount = boxRitem->geo->DrawArgs["box"].IndexCount;
	boxRitem->baseVertexLocation = boxRitem->geo->DrawArgs["box"].BaseVertexLocation;
	boxRitem->startIndexLocation = boxRitem->geo->DrawArgs["box"].StartIndexLocation;
	ritemLayer[(int)RenderLayer::AlphaTest].push_back(boxRitem.get());

	allRitems.push_back(std::move(landRitem));
	allRitems.push_back(std::move(lakeRitem));
	allRitems.push_back(std::move(boxRitem));
	objCount = allRitems.size();
}

void GaussianBlur::DrawRenderItems(const std::vector<RenderItem*>& ritems)
{

	//遍历渲染项数组
	auto objCBByteSize = CalcConstantBufferByteSize<ObjectConstants>();
	auto matCBByteSize = CalcConstantBufferByteSize<MatConstants>();
	auto objCB = mCurrFrameResource->objCB->Resource();
	auto matCB = mCurrFrameResource->matCB->Resource();

	for (size_t i = 0; i < ritems.size(); i++)
	{
		auto ritem = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, get_rvalue_ptr(ritem->geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(get_rvalue_ptr(ritem->geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ritem->primitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(srvHeap->GetGPUDescriptorHandleForHeapStart());
		texHandle.Offset(ritem->mat->diffuseSrvHeapIndex, srvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, texHandle);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddres = objCB->GetGPUVirtualAddress();
		objCBAddres += ritem->objCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddres);

		D3D12_GPU_VIRTUAL_ADDRESS matCBAddres = matCB->GetGPUVirtualAddress();
		matCBAddres += ritem->mat->matCBIndex * matCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(2, matCBAddres);

		//绘制顶点（通过索引缓冲区绘制）
		cmdList->DrawIndexedInstanced(ritem->indexCount, //每个实例要绘制的索引数
			1,	//实例化个数
			ritem->startIndexLocation,	//起始索引位置
			ritem->baseVertexLocation,	//子物体起始索引在全局索引中的位置
			0);	//实例化的高级技术，暂时设置为0
	}
}

void GaussianBlur::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; i++)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(
			device.Get(),
			1,
			objCount,
			(UINT)materials.size(),
			mWaves->VertexCount()
		));

	}
}

float GaussianBlur::GetHillsHeight(float x, float z)
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 GaussianBlur::GetHillsNormal(float x, float z) const
{
	XMFLOAT3 normal = XMFLOAT3(
		-0.03f * z * cosf(0.1f * x) - cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.1f * x * sinf(0.1f * z)
	);
	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&normal));//归一化法向量
	XMStoreFloat3(&normal, unitNormal);//存为XMFLOAT3格式

	return normal;
}

