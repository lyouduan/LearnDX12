#include "StencilApp.h"

StencilApp::StencilApp()
{
}

StencilApp::~StencilApp()
{
	if (device != nullptr)
		FlushCmdQueue();
}

bool StencilApp::Init(HINSTANCE hInstance, int nShowCmd)
{
	if (!D3DApp::Init(hInstance, nShowCmd))
		return false;
	ThrowIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));

	LoadTextures();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildSkullGeometry();
	BuildRoomGeometry();
	BuildMaterials();
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

void StencilApp::Draw()
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

	auto passCBByteSize = CalcConstantBufferByteSize<PassConstants>();
	auto passCB = mCurrFrameResource->passCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());

	// draw opaque obj
	cmdList->SetPipelineState(psos["opaque"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::Opaque]);

	// 镜子模板测试，将镜面渲染到模板缓冲区，ref值为1
	cmdList->OMSetStencilRef(1);
	cmdList->SetPipelineState(psos["markMirrorStencil"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::Mirrors]);

	// 绘制镜像骷颅，模板值为1，比较函数为equal，即只显示在镜子内
	cmdList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
	cmdList->SetPipelineState(psos["reflectionsStencil"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::Reflect]);

	// 还原passCB和Ref值
	cmdList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());
	cmdList->OMSetStencilRef(0);

	cmdList->SetPipelineState(psos["transparent"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::Transparent]);

	// shadow
	cmdList->SetPipelineState(psos["shadow"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::Shadow]);

	cmdList->SetPipelineState(psos["alphaTested"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::AlphaTest]);

	auto trans2 = CD3DX12_RESOURCE_BARRIER::Transition(swapChainBuffer[ref_mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
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

void StencilApp::Update()
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

	UpdateObjectCBs();
	UpdateMainPassCB();
	UpdateReflectPassCB();
	UpdateMatCB();
}

void StencilApp::OnResize()
{
	D3DApp::OnResize();
	//构建投影矩阵
	XMMATRIX p = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(width) / height, 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, p);
}

void StencilApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	lastMousePos.x = x;
	lastMousePos.y = y;
	SetCapture(mhMainWnd);
}

void StencilApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void StencilApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(static_cast<float>(lastMousePos.x - x));
		float dy = XMConvertToRadians(static_cast<float>(lastMousePos.y - y));
		theta += dx;
		phi += dy;
		theta = MathHelper::Clamp(theta, 0.1f, XM_PI - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.005f * static_cast<float>(x - lastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - lastMousePos.y);
		//根据鼠标输入更新摄像机可视范围半径
		radius += dx - dy;
		//限制可视范围半径
		radius = MathHelper::Clamp(radius, 1.0f, 20.0f);
	}
	lastMousePos.x = x;
	lastMousePos.y = y;
}

void StencilApp::OnKeyboardInput(const GameTime& gt)
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


	if (GetAsyncKeyState('A') & 0x8000)
		mSkullTranslation.x -= 1.0f * dt;

	if (GetAsyncKeyState('D') & 0x8000)
		mSkullTranslation.x += 1.0f * dt;

	if (GetAsyncKeyState('W') & 0x8000)
		mSkullTranslation.y += 1.0f * dt;

	if (GetAsyncKeyState('S') & 0x8000)
		mSkullTranslation.y -= 1.0f * dt;

	mSkullTranslation.y = MathHelper::Max(mSkullTranslation.y, 0.0f);
	XMMATRIX skullRotate = XMMatrixRotationY(1.57f);
	XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
	XMMATRIX skullOffset = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);
	XMMATRIX skullWorld = skullRotate * skullScale * skullOffset;
	XMStoreFloat4x4(&mSkullRitem->world, skullWorld);

	// Update reflection world matrix.
	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	XMMATRIX R = XMMatrixReflect(mirrorPlane);
	XMStoreFloat4x4(&mReflectedSkullRitem->world, skullWorld * R);

	XMStoreFloat4x4(&mReflectedFloorRitem->world, R);

	// Update shadow world matrix.
	XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
	XMVECTOR toMainLight = -XMLoadFloat3(&passConstants.lights[0].direction);
	XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
	XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
	XMStoreFloat4x4(&mShadowedSkullRitem->world, skullWorld * S * shadowOffsetY);

	// Update shadow world matrix.
	XMVECTOR shadowPlane2 = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
	XMVECTOR toMainLight2 = -XMLoadFloat3(&reflectPassConstants.lights[0].direction);
	XMMATRIX S2 = XMMatrixShadow(shadowPlane2, toMainLight2);
	XMMATRIX shadowOffsetY2 = XMMatrixTranslation(0.0f, -0.001f, 0.0f);
	XMStoreFloat4x4(&mReflectSkullShadowRitem->world, skullWorld * R * S2 * shadowOffsetY2);

	mSkullRitem->NumFramesDirty = gNumFrameResources;
	mReflectedSkullRitem->NumFramesDirty = gNumFrameResources;
	mShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
	mReflectedFloorRitem->NumFramesDirty = gNumFrameResources;
	mReflectSkullShadowRitem->NumFramesDirty = gNumFrameResources;

}

void StencilApp::UpdateObjectCBs()
{

	auto currObjectCB = mCurrFrameResource->objCB.get();
	ObjectConstants objConstants;
	for (auto& e : allRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			mWorld = e->world;
			XMMATRIX w = XMLoadFloat4x4(&mWorld);
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

void StencilApp::UpdateMainPassCB()
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
	passConstants.fogStart = 1.0f;
	passConstants.fogRange = 150.f;

	mCurrFrameResource->passCB->CopyData(0, passConstants);
}

void StencilApp::UpdateReflectPassCB()
{
	reflectPassConstants = passConstants;
	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	XMMATRIX R = XMMatrixReflect(mirrorPlane);
	//镜像灯光
	for (int i = 0; i < 3; ++i)
	{
		XMVECTOR lightDir = XMLoadFloat3(&passConstants.lights[i].direction);
		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&reflectPassConstants.lights[i].direction, reflectedLightDir);
	}
	mCurrFrameResource->passCB->CopyData(1, reflectPassConstants);
}

void StencilApp::UpdateMatCB()
{
	auto currMaterialCB = mCurrFrameResource->matCB.get();
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

			currMaterialCB->CopyData(mat->matCBIndex, matConstants);
			mat->numFramesDirty--;
		}
	}
}

ComPtr<ID3D12Resource> StencilApp::CreateDefaultBuffer(UINT64 byteSize, const void* initData, ComPtr<ID3D12Resource>& uploadBuffer)
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

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> StencilApp::GetStaticSamplers()
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

void StencilApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = (objCount + 1) * gNumFrameResources; // (objCB + passCB) * frameCounts
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&cbvHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
	srvHeapDesc.NumDescriptors = 4; // (objCB + passCB) * frameCounts
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)));
}

void StencilApp::BuildConstantBuffers()
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
	auto white1x1Tex = textures["bricksTex"]->Resource;
	auto floorTex = textures["checkboardTex"]->Resource;
	auto wallTex = textures["iceTex"]->Resource;
	auto mirrorTex = textures["white1x1Tex"]->Resource;

	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = white1x1Tex->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = -1;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		device->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, srvHandle);

		srvHandle.Offset(1, srvDescriptorSize);
		srvDesc.Format = floorTex->GetDesc().Format;
		device->CreateShaderResourceView(floorTex.Get(), &srvDesc, srvHandle);

		srvHandle.Offset(1, srvDescriptorSize);
		srvDesc.Format = wallTex->GetDesc().Format;
		device->CreateShaderResourceView(wallTex.Get(), &srvDesc, srvHandle);

		srvHandle.Offset(1, srvDescriptorSize);
		srvDesc.Format = mirrorTex->GetDesc().Format;
		device->CreateShaderResourceView(mirrorTex.Get(), &srvDesc, srvHandle);
	}
}

void StencilApp::BuildRootSignature()
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

void StencilApp::BuildShadersAndInputLayout()
{

	inputLayoutDesc =
	{
		  { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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

}



void StencilApp::BuildRoomGeometry()
{

	//顶点缓存
	std::array<Vertex, 20> vertices =
	{
		//地板模型的顶点Pos、Normal、TexCoord
		Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0 
		Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
		Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
		Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

		//墙壁模型的顶点Pos、Normal、TexCoord
		Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8 
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
		Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
		Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

		//镜子模型的顶点Pos、Normal、TexCoord
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
	};

	//索引缓存
	std::array<std::int16_t, 30> indices =
	{
		// 地板（Floor）
		0, 1, 2,
		0, 2, 3,

		// 墙壁（Walls）
		4, 5, 6,
		4, 6, 7,

		8, 9, 10,
		8, 10, 11,

		12, 13, 14,
		12, 14, 15,

		// 镜子（Mirror）
		16, 17, 18,
		16, 18, 19
	};

	SubmeshGeometry floorSubmesh;
	floorSubmesh.IndexCount = 6;
	floorSubmesh.StartIndexLocation = 0;
	floorSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry wallSubmesh;
	wallSubmesh.IndexCount = 18;
	wallSubmesh.StartIndexLocation = 6;
	wallSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry mirrorSubmesh;
	mirrorSubmesh.IndexCount = 6;
	mirrorSubmesh.StartIndexLocation = 24;
	mirrorSubmesh.BaseVertexLocation = 0;

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->name = "roomGeo";
	
	geo->vertexBufferGpu = CreateDefaultBuffer(vbByteSize, vertices.data(), geo->vertexBufferUploader);
	geo->indexBufferGpu = CreateDefaultBuffer(ibByteSize, indices.data(), geo->indexBufferUploader);

	geo->vertexByteStride = sizeof(Vertex);
	geo->vertexBufferByteSize = vbByteSize;
	geo->indexFormat = DXGI_FORMAT_R16_UINT;
	geo->indexBufferByteSize = ibByteSize;

	geo->DrawArgs["floor"] = floorSubmesh;
	geo->DrawArgs["wall"] = wallSubmesh;
	geo->DrawArgs["mirror"] = mirrorSubmesh;

	geometries[geo->name] = std::move(geo);
}

void StencilApp::BuildMaterials()
{
	auto bricks = std::make_unique<Material>();
	bricks->name = "bricks";
	bricks->diffuseSrvHeapIndex = 0;
	bricks->matCBIndex = 0;
	bricks->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks->fresnelR0 = XMFLOAT3(0.05, 0.05, 0.05);
	bricks->roughness = 0.25f;

	auto checkertile = std::make_unique<Material>();
	checkertile->name = "checkertile";
	checkertile->diffuseSrvHeapIndex = 1;
	checkertile->matCBIndex = 1;
	checkertile->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	checkertile->fresnelR0 = XMFLOAT3(0.07, 0.07, 0.07);
	checkertile->roughness = 0.3f;

	auto icemirror = std::make_unique<Material>();
	icemirror->name = "icemirror";
	icemirror->diffuseSrvHeapIndex = 2;
	icemirror->matCBIndex = 2;
	icemirror->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	icemirror->fresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	icemirror->roughness = 0.5f;

	auto skullMat = std::make_unique<Material>();
	skullMat->name = "skullMat";
	skullMat->diffuseSrvHeapIndex = 3;
	skullMat->matCBIndex = 3;
	skullMat->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->fresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skullMat->roughness = 0.3f;

	auto shadowMat = std::make_unique<Material>();
	shadowMat->name = "shadowMat";
	shadowMat->diffuseSrvHeapIndex = 3;
	shadowMat->matCBIndex = 4;
	shadowMat->diffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.9f);
	shadowMat->fresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	shadowMat->roughness = 0.0f;

	materials["bricks"] = std::move(bricks);
	materials["checkertile"] = std::move(checkertile);
	materials["icemirror"] = std::move(icemirror);
	materials["skullMat"] = std::move(skullMat);
	materials["shadowMat"] = std::move(shadowMat);
}

void StencilApp::BuildSkullGeometry()
{
	std::ifstream fin("./model/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"File not found", 0, 0);
		return;
	}

	UINT vertexCount = 0;
	UINT triangleCount = 0;
	std::string ignore;

	fin >> ignore >> vertexCount;
	fin >> ignore >> triangleCount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex> vertices(vertexCount);
	for (UINT i = 0; i < vertexCount; i++)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z; // position
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z; // normal
	}
	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(triangleCount * 3);
	for (UINT i = 0; i < triangleCount; i++)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();


	const UINT vbByteSize = vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->name = "skullGeo";

	geo->vertexBufferGpu = CreateDefaultBuffer(vbByteSize, vertices.data(), geo->vertexBufferUploader);
	geo->indexBufferGpu = CreateDefaultBuffer(ibByteSize, indices.data(), geo->indexBufferUploader);

	geo->vertexBufferByteSize = vbByteSize;
	geo->vertexByteStride = sizeof(Vertex);
	geo->indexBufferByteSize = ibByteSize;
	geo->indexFormat = DXGI_FORMAT_R32_UINT;

	SubmeshGeometry skullSubMesh;
	skullSubMesh.BaseVertexLocation = 0;
	skullSubMesh.StartIndexLocation = 0;
	skullSubMesh.IndexCount = (UINT)indices.size();

	geo->DrawArgs["skull"] = skullSubMesh;

	geometries["skullGeo"] = std::move(geo);
}

void StencilApp::LoadTextures()
{
	auto bricksTex = std::make_unique<Texture>();
	bricksTex->name = "bricksTex";
	bricksTex->Filename = L"./model/bricks3.dds";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		bricksTex->Filename.c_str(),
		bricksTex->Resource,
		bricksTex->UploadHeap
	));

	auto checkboardTex = std::make_unique<Texture>();
	checkboardTex->name = "checkboardTex";
	checkboardTex->Filename = L"./model/checkboard.dds";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		checkboardTex->Filename.c_str(),
		checkboardTex->Resource,
		checkboardTex->UploadHeap
	));

	auto iceTex = std::make_unique<Texture>();
	iceTex->name = "iceTex";
	iceTex->Filename = L"./model/ice.dds";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		iceTex->Filename.c_str(),
		iceTex->Resource,
		iceTex->UploadHeap
	));

	auto white1x1Tex = std::make_unique<Texture>();
	white1x1Tex->name = "white1x1Tex";
	white1x1Tex->Filename = L"./model/white1x1.dds";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		white1x1Tex->Filename.c_str(),
		white1x1Tex->Resource,
		white1x1Tex->UploadHeap
	));
	textures[bricksTex->name] = std::move(bricksTex);
	textures[checkboardTex->name] = std::move(checkboardTex);
	textures[iceTex->name] = std::move(iceTex);
	textures[white1x1Tex->name] = std::move(white1x1Tex);
}

void StencilApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = {};
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
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

	// set marking the stencil mirrors
	CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
	mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0; // 禁止颜色数据写入
	D3D12_DEPTH_STENCIL_DESC mirrorDepthStencil;
	mirrorDepthStencil.DepthEnable = true; 
	mirrorDepthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // disable depth write
	mirrorDepthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	mirrorDepthStencil.StencilEnable = true;
	mirrorDepthStencil.StencilReadMask = 0xff;
	mirrorDepthStencil.StencilWriteMask = 0xff;
	mirrorDepthStencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP; // 模板测试失败，保持原值
	mirrorDepthStencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;//
	mirrorDepthStencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;//失败、替换成Ref值
	mirrorDepthStencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;// 一直通过
	mirrorDepthStencil.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDepthStencil.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDepthStencil.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDepthStencil.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC mirrorPsoDesc = opaquePsoDesc;
	mirrorPsoDesc.BlendState = mirrorBlendState;
	mirrorPsoDesc.DepthStencilState = mirrorDepthStencil;
	psos["markMirrorStencil"] = nullptr;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&mirrorPsoDesc, IID_PPV_ARGS(&psos["markMirrorStencil"])));

	D3D12_DEPTH_STENCIL_DESC reflectionsDepthStencil;
	reflectionsDepthStencil.DepthEnable = true;
	reflectionsDepthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionsDepthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionsDepthStencil.StencilEnable = true;
	reflectionsDepthStencil.StencilReadMask = 0xff;
	reflectionsDepthStencil.StencilWriteMask = 0xff;
	reflectionsDepthStencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	reflectionsDepthStencil.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencil.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencil.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencil.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC reflectionPsoDesc = opaquePsoDesc;
	reflectionPsoDesc.DepthStencilState = reflectionsDepthStencil;
	reflectionPsoDesc.RasterizerState.FrontCounterClockwise = true;
	psos["reflectionsStencil"] = nullptr;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&reflectionPsoDesc, IID_PPV_ARGS(&psos["reflectionsStencil"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowBlendPsoDesc = transparentPsoDesc;
	D3D12_DEPTH_STENCIL_DESC shadowDepthStencil;
	shadowDepthStencil.DepthEnable = true;
	shadowDepthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDepthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDepthStencil.StencilEnable = true;
	shadowDepthStencil.StencilReadMask = 0xff;
	shadowDepthStencil.StencilWriteMask = 0xff;
	shadowDepthStencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDepthStencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDepthStencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	shadowDepthStencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDepthStencil.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDepthStencil.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDepthStencil.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	shadowDepthStencil.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;

	shadowBlendPsoDesc.DepthStencilState = shadowDepthStencil;
	psos["shadow"] = nullptr;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&shadowBlendPsoDesc, IID_PPV_ARGS(&psos["shadow"])));

}

void StencilApp::BuildRenderItem()
{
	auto floorRitem = std::make_unique<RenderItem>();
	floorRitem->world = MathHelper::Identity4x4();
	floorRitem->texTransform = MathHelper::Identity4x4();
	floorRitem->objCBIndex = 0;
	floorRitem->mat = materials["checkertile"].get();
	floorRitem->geo = geometries["roomGeo"].get();
	floorRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	floorRitem->indexCount = floorRitem->geo->DrawArgs["floor"].IndexCount;
	floorRitem->baseVertexLocation = floorRitem->geo->DrawArgs["floor"].BaseVertexLocation;
	floorRitem->startIndexLocation = floorRitem->geo->DrawArgs["floor"].StartIndexLocation;
	ritemLayer[(int)RenderLayer::Opaque].push_back(floorRitem.get());

	auto wallsRitem = std::make_unique<RenderItem>();
	wallsRitem->world = MathHelper::Identity4x4();
	wallsRitem->texTransform = MathHelper::Identity4x4();
	wallsRitem->objCBIndex = 1;
	wallsRitem->mat = materials["bricks"].get();
	wallsRitem->geo = geometries["roomGeo"].get();
	wallsRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallsRitem->indexCount = wallsRitem->geo->DrawArgs["wall"].IndexCount;
	wallsRitem->baseVertexLocation = wallsRitem->geo->DrawArgs["wall"].BaseVertexLocation;
	wallsRitem->startIndexLocation = wallsRitem->geo->DrawArgs["wall"].StartIndexLocation;
	ritemLayer[(int)RenderLayer::Opaque].push_back(wallsRitem.get());

	auto mirrorRitem = std::make_unique<RenderItem>();
	mirrorRitem->world = MathHelper::Identity4x4();
	mirrorRitem->texTransform = MathHelper::Identity4x4();
	mirrorRitem->objCBIndex = 2;
	mirrorRitem->mat = materials["icemirror"].get();
	mirrorRitem->geo = geometries["roomGeo"].get();
	mirrorRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mirrorRitem->indexCount = mirrorRitem->geo->DrawArgs["mirror"].IndexCount;
	mirrorRitem->baseVertexLocation = mirrorRitem->geo->DrawArgs["mirror"].BaseVertexLocation;
	mirrorRitem->startIndexLocation = mirrorRitem->geo->DrawArgs["mirror"].StartIndexLocation;
	ritemLayer[(int)RenderLayer::Mirrors].push_back(mirrorRitem.get());
	ritemLayer[(int)RenderLayer::Transparent].push_back(mirrorRitem.get());

	auto skullRitem = std::make_unique<RenderItem>();
	skullRitem->world = MathHelper::Identity4x4();
	skullRitem->texTransform = MathHelper::Identity4x4();
	skullRitem->objCBIndex = 3;
	skullRitem->mat = materials["skullMat"].get();
	skullRitem->geo = geometries["skullGeo"].get();
	skullRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->indexCount = skullRitem->geo->DrawArgs["skull"].IndexCount;
	skullRitem->baseVertexLocation = skullRitem->geo->DrawArgs["skull"].BaseVertexLocation;
	skullRitem->startIndexLocation = skullRitem->geo->DrawArgs["skull"].StartIndexLocation;
	mSkullRitem = skullRitem.get();
	ritemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());

	// reflected obj
	auto reflectedSkullRitem = std::make_unique<RenderItem>();
	*reflectedSkullRitem = *skullRitem;
	reflectedSkullRitem->objCBIndex = 4;
	mReflectedSkullRitem = reflectedSkullRitem.get();
	ritemLayer[(int)RenderLayer::Reflect].push_back(reflectedSkullRitem.get());

	auto reflectedFloorRitem = std::make_unique<RenderItem>();
	*reflectedFloorRitem = *floorRitem;
	reflectedFloorRitem->objCBIndex = 5;
	mReflectedFloorRitem = reflectedFloorRitem.get();
	ritemLayer[(int)RenderLayer::Reflect].push_back(reflectedFloorRitem.get());

	// shadow obj
	auto skullShadowRitem = std::make_unique<RenderItem>();
	*skullShadowRitem = *skullRitem;
	skullShadowRitem->objCBIndex = 6;
	skullShadowRitem->mat = materials["shadowMat"].get();
	mShadowedSkullRitem = skullShadowRitem.get();
	ritemLayer[(int)RenderLayer::Shadow].push_back(skullShadowRitem.get());

	auto reflectSkullShadowRitem = std::make_unique<RenderItem>();
	*reflectSkullShadowRitem = *reflectedSkullRitem;
	reflectSkullShadowRitem->objCBIndex = 7;
	reflectSkullShadowRitem->mat = materials["shadowMat"].get();
	mReflectSkullShadowRitem = reflectSkullShadowRitem.get();
	ritemLayer[(int)RenderLayer::Reflect].push_back(reflectSkullShadowRitem.get());
	ritemLayer[(int)RenderLayer::Shadow].push_back(reflectSkullShadowRitem.get());

	allRitems.push_back(std::move(floorRitem));
	allRitems.push_back(std::move(wallsRitem));
	allRitems.push_back(std::move(skullRitem));
	allRitems.push_back(std::move(mirrorRitem));
	allRitems.push_back(std::move(reflectedSkullRitem));
	allRitems.push_back(std::move(reflectedFloorRitem));
	allRitems.push_back(std::move(skullShadowRitem));
	allRitems.push_back(std::move(reflectSkullShadowRitem));

	objCount = allRitems.size();
}

void StencilApp::DrawRenderItems(const std::vector<RenderItem*>& ritems)
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

		//设置根描述符表
		//UINT objCbvIndex = mCurrFrameResourceIndex * objCount + ritem->objCBIndex;
		//auto handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbvHeap->GetGPUDescriptorHandleForHeapStart());
		//handle.Offset(objCbvIndex, cbvDescriptorSize);
		//cmdList->SetGraphicsRootDescriptorTable(0, //根参数的起始索引
		//	handle);
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

void StencilApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; i++)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(
			device.Get(),
			2,
			objCount,
			(UINT)materials.size()
		));

	}
}

