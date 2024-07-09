#include "D3D12InitApp.h"

D3D12InitApp::D3D12InitApp()
{
}

D3D12InitApp::~D3D12InitApp()
{
}

bool D3D12InitApp::Init(HINSTANCE hInstance, int nShowCmd)
{
	if(!D3DApp::Init(hInstance, nShowCmd))
		return false;
	ThrowIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));

	loadTexutres();

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildSkullGeometry();
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

void D3D12InitApp::Draw()
{
	auto currCmdAllocator = mCurrFrameResource->cmdAllocator;
	ThrowIfFailed(currCmdAllocator->Reset());
	ThrowIfFailed(cmdList->Reset(currCmdAllocator.Get(), pipelineState.Get()));

	cmdList->RSSetViewports(1, &viewPort);
	cmdList->RSSetScissorRects(1, &scissorRect);

	UINT& ref_mCurrentBackBuffer = mCurrentBackBuffer;
	auto trans1 = CD3DX12_RESOURCE_BARRIER::Transition(
		swapChainBuffer[ref_mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT,//转换资源为后台缓冲区资源
		D3D12_RESOURCE_STATE_RENDER_TARGET);//从呈现到渲染目标转换
	cmdList->ResourceBarrier(1, &trans1);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart(), ref_mCurrentBackBuffer, rtvDescriptorSize);
	//const float clearColor[] = { 0.2f, 0.2f, 0.4f, 1.0f };
	cmdList->ClearRenderTargetView(rtvHandle, DirectX::Colors::SkyBlue, 0, nullptr);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cmdList->ClearDepthStencilView(dsvHandle,	//DSV描述符句柄
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

	cmdList->SetPipelineState(psos["gsSphere"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::geoSphere]);

	cmdList->SetPipelineState(psos["gsSphere2"].Get());
	DrawRenderItems(ritemLayer[(int)RenderLayer::geoSpherePoint]);

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

void D3D12InitApp::Update()
{
	onKeybordInput(gt);

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	if (mCurrFrameResource->fence != 0 && fence->GetCompletedValue() < mCurrFrameResource->fence)
	{
		eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(fence->SetEventOnCompletion(mCurrFrameResource->fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
	
	// 这里有一个bug，初始值NumFramesDirty变了
	UpdateObjectCBs();
	UpdateMainPassCB();
	UpdateMatCB();
}

void D3D12InitApp::OnResize()
{
	D3DApp::OnResize();
	//构建投影矩阵

	camera.SetLen(XM_PIDIV4, static_cast<float>(width) / height, 1.0f, 1000.0f);
}

void D3D12InitApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	lastMousePos.x = x;
	lastMousePos.y = y;
	SetCapture(mhMainWnd);
}

void D3D12InitApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void D3D12InitApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(static_cast<float>(x - lastMousePos.x));
		float dy = XMConvertToRadians(static_cast<float>(y - lastMousePos.y));

		camera.Pitch(dy);
		camera.RotateY(dx);
	}
	
	lastMousePos.x = x;
	lastMousePos.y = y;
}

void D3D12InitApp::onKeybordInput(const GameTime& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		camera.Walk(10.0f*dt);
	if (GetAsyncKeyState('S') & 0x8000)
		camera.Walk(-10.0f * dt);
	if (GetAsyncKeyState('A') & 0x8000)
		camera.Strafe(-10.0f * dt);
	if (GetAsyncKeyState('D') & 0x8000)
		camera.Strafe(10.0f * dt);

	camera.UpdateViewMatrix();

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

void D3D12InitApp::UpdateObjectCBs()
{
	ObjectConstants objConstants;

	auto currObjectCB = mCurrFrameResource->objCB.get();

	for (auto& e : allRitems)
	{
		// 存在一个bug, NumFramesDirty变动了，并非初始化4，导致无法更新
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX w = XMLoadFloat4x4(&e->world);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->texTransform);
			//XMMATRIX赋值给XMFLOAT4X4
			XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(w));
			XMStoreFloat4x4(&objConstants.texTransform, XMMatrixTranspose(texTransform));
			//将数据拷贝至GPU缓存
			currObjectCB->CopyData(e->objCBIndex, objConstants);
			e->NumFramesDirty--;
		}
	}
}

void D3D12InitApp::UpdateMainPassCB()
{
	
	XMMATRIX view = camera.GetView(); // 左手坐标系
	XMMATRIX proj = camera.GetProj();

	// view存到mView中
	XMMATRIX vp = view * proj;
	XMStoreFloat4x4(&passConstants.viewProj, XMMatrixTranspose(vp));

	passConstants.eyePosW = camera.GetPosition3f();

	passConstants.ambientLight = { 0.25f,0.25f,0.25f,1.0f };

	passConstants.lights[0].direction = { 0.57735f, -0.57735f, 0.57735f };
	passConstants.lights[0].strength = { 0.6f, 0.6f, 0.6f };
	passConstants.lights[1].direction = { -0.57735f, -0.57735f, 0.57735f };
	passConstants.lights[1].strength = { 0.3f, 0.3f, 0.3f };
	passConstants.lights[2].direction = { 0.0f, -0.707f, -0.707f };
	passConstants.lights[2].strength = { 0.15f, 0.15f, 0.15f };

	passConstants.totalTime = gt.TotalTime();
	XMVECTOR sunDir = -MathHelper::SphericalToCartesian(1.0f, sunTheta, sunPhi);
	XMStoreFloat3(&passConstants.lights[0].direction, sunDir);

	mCurrFrameResource->passCB->CopyData(0, passConstants);
}

void D3D12InitApp::UpdateMatCB()
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

			mCurrFrameResource->matCB->CopyData(mat->matCBIndex, matConstants);
			mat->numFramesDirty--;
		}

	}
}

void D3D12InitApp::loadTexutres()
{

	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Filename = L"./model/bricks.dds";
	bricksTex->name = "bricksTex";
	
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		bricksTex->Filename.c_str(),
		bricksTex->Resource,
		bricksTex->UploadHeap
	));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Filename = L"./model/stone.dds";
	stoneTex->name = "stoneTex";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		stoneTex->Filename.c_str(),
		stoneTex->Resource,
		stoneTex->UploadHeap
	));

	auto tileTex = std::make_unique<Texture>();
	tileTex->Filename = L"./model/tile.dds";
	tileTex->name = "tileTex";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device.Get(),
		cmdList.Get(),
		tileTex->Filename.c_str(),
		tileTex->Resource,
		tileTex->UploadHeap
	));

	textures[bricksTex->name] = std::move(bricksTex);
	textures[stoneTex->name]  = std::move(stoneTex);
	textures[tileTex->name]   = std::move(tileTex);
}

ComPtr<ID3D12Resource> D3D12InitApp::CreateDefaultBuffer(UINT64 byteSize, const void* initData, ComPtr<ID3D12Resource>& uploadBuffer)
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

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> D3D12InitApp::GetStaticSamplers()
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

void D3D12InitApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = (objCount + matCount + 1) * gNumFrameResources; // (objCB + passCB) * frameCounts
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&cbvHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
	srvHeapDesc.NumDescriptors = 4; 
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)));
}

void D3D12InitApp::BuildConstantBuffers()
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

	int matOffset = objCount * gNumFrameResources;
	UINT matCBByteSize = CalcConstantBufferByteSize<MatConstants>();
	for (int frameIndex = 0; frameIndex < gNumFrameResources; frameIndex++)
	{
		auto mMatCB = mFrameResources[frameIndex]->matCB->Resource();
		for (int i = 0; i < matCount; i++)
		{
			cbAddress = mMatCB->GetGPUVirtualAddress();

			cbAddress += i * matCBByteSize;

			auto cbvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbvHeap->GetCPUDescriptorHandleForHeapStart());
			cbvHandle.Offset(matOffset + matCount * frameIndex + i, cbvDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = matCBByteSize;

			device->CreateConstantBufferView(&cbvDesc, cbvHandle);
		}
	}

	int passOffset = (objCount + matCount) * gNumFrameResources;
	UINT passCBByteSize = CalcConstantBufferByteSize<PassConstants>();
	for (int frameIndex = 0; frameIndex < gNumFrameResources; frameIndex++)
	{
		// pass cBuffer
		auto mPassCB = mFrameResources[frameIndex]->passCB->Resource();

		D3D12_GPU_VIRTUAL_ADDRESS cbAddress2 = mPassCB->GetGPUVirtualAddress();

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc2;
		cbvDesc2.BufferLocation = cbAddress2;
		cbvDesc2.SizeInBytes = passCBByteSize;

		auto cbvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbvHeap->GetCPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(passOffset + frameIndex, cbvDescriptorSize);

		device->CreateConstantBufferView(&cbvDesc2, cbvHandle);
	}

	auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvHeap->GetCPUDescriptorHandleForHeapStart());
	auto bricksTex = textures["bricksTex"]->Resource;
	auto stoneTex = textures["stoneTex"]->Resource;
	auto tileTex = textures["tileTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	device->CreateShaderResourceView(bricksTex.Get(), &srvDesc, srvHandle);

	srvHandle.Offset(1, srvDescriptorSize);
	srvDesc.Format = stoneTex->GetDesc().Format;
	device->CreateShaderResourceView(stoneTex.Get(), &srvDesc, srvHandle);
	
	srvHandle.Offset(1, srvDescriptorSize);
	srvDesc.Format = tileTex->GetDesc().Format;
	device->CreateShaderResourceView(tileTex.Get(), &srvDesc, srvHandle);

}

void D3D12InitApp::BuildRootSignature()
{
	// 每个根参数只能绑定一个寄存器空间中的特定类型资源
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
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

void D3D12InitApp::BuildShadersAndInputLayout()
{
	inputLayoutDesc =
	{
		  { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	shaders["opaqueVS"] = CompileShader(std::wstring(L"shader/column.hlsl").c_str(), nullptr, "VSMain", "vs_5_0");
	shaders["opaquePS"] = CompileShader(std::wstring(L"shader/column.hlsl").c_str(), nullptr, "PSMain", "ps_5_0");

	gsInputLayoutDesc =
	{
		  { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		  { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	shaders["gsVS"] = CompileShader(std::wstring(L"shader/gsSphere.hlsl").c_str(), nullptr, "VSMain", "vs_5_0");
	shaders["gsGS"] = CompileShader(std::wstring(L"shader/gsSphere.hlsl").c_str(), nullptr, "GSMain", "gs_5_0");
	shaders["gsPS"] = CompileShader(std::wstring(L"shader/gsSphere.hlsl").c_str(), nullptr, "PSMain", "ps_5_0");
	
	shaders["gsGSPoint"] = CompileShader(std::wstring(L"shader/gsSphere.hlsl").c_str(), nullptr, "GSMain2", "gs_5_0");
	shaders["gsPSPoint"] = CompileShader(std::wstring(L"shader/gsSphere.hlsl").c_str(), nullptr, "PSMain2", "ps_5_0");

}

void D3D12InitApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	//GeometryGenerator::MeshData sphere = geoGen.CreateGeosphere(0.5f, 3);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData gsSpehre = geoGen.CreateGeoSphere20Face(0.8f);

	// concatenate all the geometry into one big vertex/index buffer
	// **********************

	// the vertex offset to each object
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT gsSpehreVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	// the index offset to each object
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT gsSpehreIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	//
	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry gsSpehreSubmesh;
	gsSpehreSubmesh.IndexCount = (UINT)gsSpehre.Indices32.size();
	gsSpehreSubmesh.StartIndexLocation = gsSpehreIndexOffset;
	gsSpehreSubmesh.BaseVertexLocation = gsSpehreVertexOffset;

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() + 
		gsSpehre.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].Tex = box.Vertices[i].TexC;
	}
	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].Tex = grid.Vertices[i].TexC;
	}
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].Tex = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].Tex = cylinder.Vertices[i].TexC;
	}
	for (size_t i = 0; i < gsSpehre.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = gsSpehre.Vertices[i].Position;
		vertices[k].Normal = gsSpehre.Vertices[i].Normal;
		vertices[k].Tex = gsSpehre.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(gsSpehre.GetIndices16()), std::end(gsSpehre.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->name = "shapeGeo";
	geo->vertexBufferGpu = CreateDefaultBuffer(vbByteSize, vertices.data(), geo->vertexBufferUploader);
	geo->indexBufferGpu = CreateDefaultBuffer(ibByteSize, indices.data(), geo->indexBufferUploader);

	geo->vertexByteStride = sizeof(Vertex);
	geo->vertexBufferByteSize = vbByteSize;
	geo->indexFormat = DXGI_FORMAT_R16_UINT;
	geo->indexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["gsSpehre"] = gsSpehreSubmesh;

	geometries["shapeGeo"] = std::move(geo);
}


void D3D12InitApp::BuildMaterials()
{
	auto grid = std::make_unique<Material>();
	grid->name = "grid";
	grid->matCBIndex = 0;
	grid->diffuseSrvHeapIndex = 2;
	grid->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grid->fresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	grid->roughness = 0.8f;

	auto sphere = std::make_unique<Material>();
	sphere->name = "sphere";
	sphere->matCBIndex = 1;
	sphere->diffuseSrvHeapIndex = 1;
	sphere->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sphere->fresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sphere->roughness = 0.1f;

	auto cylinder = std::make_unique<Material>();
	cylinder->name = "cylinder";
	cylinder->matCBIndex = 2;
	cylinder->diffuseSrvHeapIndex = 0;
	cylinder->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	cylinder->fresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	cylinder->roughness = 0.2f;

	auto box = std::make_unique<Material>();
	box->name = "box";
	box->matCBIndex = 3;
	box->diffuseSrvHeapIndex = 2;
	box->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	box->fresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	box->roughness = 0.2f;

	auto skull = std::make_unique<Material>();
	skull->name = "skull";
	skull->matCBIndex = 4;
	skull->diffuseSrvHeapIndex = 1;
	skull->diffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skull->fresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skull->roughness = 0.3f;

	materials[grid->name] = std::move(grid);
	materials[sphere->name] = std::move(sphere);
	materials[cylinder->name] = std::move(cylinder);
	materials[box->name] = std::move(box);
	materials[skull->name] = std::move(skull);

	matCount = materials.size();
}

void D3D12InitApp::BuildSkullGeometry()
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
		fin >> indices[i*3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
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

void D3D12InitApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputLayoutDesc.data(), (unsigned int)inputLayoutDesc.size() };
	psoDesc.pRootSignature = rootSignature.Get();
	psoDesc.VS = {
		reinterpret_cast<BYTE*>(shaders["opaqueVS"]->GetBufferPointer()),
		shaders["opaqueVS"]->GetBufferSize()
	};
	
	psoDesc.PS = {
		reinterpret_cast<BYTE*>(shaders["opaquePS"]->GetBufferPointer()),
		shaders["opaquePS"]->GetBufferSize()
	};
	CD3DX12_RASTERIZER_DESC Raster(D3D12_DEFAULT);
	//Raster.FillMode = D3D12_FILL_MODE_WIREFRAME;
	//Raster.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState = Raster;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psos["opaque"] = nullptr;

	ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&psos["opaque"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gsPsoDesc = psoDesc;
	psoDesc.InputLayout = { gsInputLayoutDesc.data(), (unsigned int)gsInputLayoutDesc.size() };
	gsPsoDesc.VS = {
		reinterpret_cast<BYTE*>(shaders["gsVS"]->GetBufferPointer()),
		shaders["gsVS"]->GetBufferSize()
	};
	gsPsoDesc.GS = {
		reinterpret_cast<BYTE*>(shaders["gsGS"]->GetBufferPointer()),
		shaders["gsGS"]->GetBufferSize()
	};
	gsPsoDesc.PS = {
		reinterpret_cast<BYTE*>(shaders["gsPS"]->GetBufferPointer()),
		shaders["gsPS"]->GetBufferSize()
	};
	Raster.FillMode = D3D12_FILL_MODE_WIREFRAME;
	Raster.CullMode = D3D12_CULL_MODE_NONE;
	gsPsoDesc.RasterizerState = Raster;
	psos["gsSphere"] = nullptr;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&gsPsoDesc, IID_PPV_ARGS(&psos["gsSphere"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gsPsoDesc2 = gsPsoDesc;
	gsPsoDesc2.GS = {
		reinterpret_cast<BYTE*>(shaders["gsGSPoint"]->GetBufferPointer()),
		shaders["gsGSPoint"]->GetBufferSize()
	};
	gsPsoDesc2.PS = {
		reinterpret_cast<BYTE*>(shaders["gsPSPoint"]->GetBufferPointer()),
		shaders["gsPSPoint"]->GetBufferSize()
	};
	gsPsoDesc2.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psos["gsSphere2"] = nullptr;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&gsPsoDesc2, IID_PPV_ARGS(&psos["gsSphere2"])));
}

void D3D12InitApp::BuildRenderItem()
{
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->world, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	XMStoreFloat4x4(&boxRitem->texTransform, XMMatrixScaling(1.0f, 1.f, 1.0f));
	boxRitem->NumFramesDirty = gNumFrameResources;
	boxRitem->objCBIndex = 0;
	boxRitem->mat = materials["box"].get();
	boxRitem->geo = geometries["shapeGeo"].get();
	boxRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->indexCount = boxRitem->geo->DrawArgs["box"].IndexCount;
	boxRitem->baseVertexLocation = boxRitem->geo->DrawArgs["box"].BaseVertexLocation;
	boxRitem->startIndexLocation = boxRitem->geo->DrawArgs["box"].StartIndexLocation;
	ritemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	allRitems.push_back(std::move(boxRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->world = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->texTransform, XMMatrixScaling(7.0f, 7.f, 1.0f));
	gridRitem->NumFramesDirty = gNumFrameResources;
	gridRitem->objCBIndex = 1;
	gridRitem->mat = materials["grid"].get();
	gridRitem->geo = geometries["shapeGeo"].get();
	gridRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->indexCount = gridRitem->geo->DrawArgs["grid"].IndexCount;
	gridRitem->baseVertexLocation = gridRitem->geo->DrawArgs["grid"].BaseVertexLocation;
	gridRitem->startIndexLocation = gridRitem->geo->DrawArgs["grid"].StartIndexLocation;
	ritemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	allRitems.push_back(std::move(gridRitem));

	//auto skullRitem = std::make_unique<RenderItem>();
	//XMStoreFloat4x4(&skullRitem->world, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	//skullRitem->NumFramesDirty = gNumFrameResources;
	//skullRitem->objCBIndex = 2;//skull常量数据（world矩阵）在objConstantBuffer索引1上
	//skullRitem->mat = materials["skull"].get();
	//skullRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//skullRitem->geo = geometries["skullGeo"].get();
	//skullRitem->indexCount = skullRitem->geo->DrawArgs["skull"].IndexCount;
	//skullRitem->baseVertexLocation = skullRitem->geo->DrawArgs["skull"].BaseVertexLocation;
	//skullRitem->startIndexLocation = skullRitem->geo->DrawArgs["skull"].StartIndexLocation;
	//allRitems.push_back(std::move(skullRitem));

	auto gsSpehreRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gsSpehreRitem->world, XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixTranslation(0.0f, 3.0f, 0.0f));
	gsSpehreRitem->NumFramesDirty = gNumFrameResources;
	gsSpehreRitem->objCBIndex = 2; //skull常量数据（world矩阵）在objConstantBuffer索引1上
	gsSpehreRitem->mat = materials["skull"].get();
	gsSpehreRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gsSpehreRitem->geo = geometries["shapeGeo"].get();
	gsSpehreRitem->indexCount = gsSpehreRitem->geo->DrawArgs["gsSpehre"].IndexCount;
	gsSpehreRitem->baseVertexLocation = gsSpehreRitem->geo->DrawArgs["gsSpehre"].BaseVertexLocation;
	gsSpehreRitem->startIndexLocation = gsSpehreRitem->geo->DrawArgs["gsSpehre"].StartIndexLocation;
	ritemLayer[(int)RenderLayer::geoSphere].push_back(gsSpehreRitem.get());

	auto gsSpehreRitem2 = std::make_unique<RenderItem>();
	*gsSpehreRitem2 = *gsSpehreRitem;
	gsSpehreRitem2->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	ritemLayer[(int)RenderLayer::geoSpherePoint].push_back(gsSpehreRitem2.get());

	allRitems.push_back(std::move(gsSpehreRitem));
	allRitems.push_back(std::move(gsSpehreRitem2));

	UINT objCBIndex = 4;
	for (int i = 0; i < 5; i++) {
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&(leftCylRitem->world), leftCylWorld);
		XMStoreFloat4x4(&leftCylRitem->texTransform, XMMatrixScaling(7.0f, 7.f, 1.0f));
		leftCylRitem->NumFramesDirty = gNumFrameResources;
		leftCylRitem->objCBIndex = objCBIndex++;
		leftCylRitem->mat = materials["cylinder"].get();
		leftCylRitem->geo = geometries["shapeGeo"].get();
		leftCylRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->indexCount = leftCylRitem->geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->baseVertexLocation = leftCylRitem->geo->DrawArgs["cylinder"].BaseVertexLocation;
		leftCylRitem->startIndexLocation = leftCylRitem->geo->DrawArgs["cylinder"].StartIndexLocation;

		XMStoreFloat4x4(&rightCylRitem->world, rightCylWorld);
		XMStoreFloat4x4(&rightCylRitem->texTransform, XMMatrixScaling(7.0f, 7.f, 1.0f));
		rightCylRitem->NumFramesDirty = gNumFrameResources;
		rightCylRitem->objCBIndex = objCBIndex++;
		rightCylRitem->mat = materials["cylinder"].get();
		rightCylRitem->geo = geometries["shapeGeo"].get();
		rightCylRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->indexCount = rightCylRitem->geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->baseVertexLocation = rightCylRitem->geo->DrawArgs["cylinder"].BaseVertexLocation;
		rightCylRitem->startIndexLocation = rightCylRitem->geo->DrawArgs["cylinder"].StartIndexLocation;

		XMStoreFloat4x4(&leftSphereRitem->world, leftSphereWorld);
		XMStoreFloat4x4(&leftSphereRitem->texTransform, XMMatrixScaling(7.0f, 7.f, 1.0f));
		leftSphereRitem->NumFramesDirty = gNumFrameResources;
		leftSphereRitem->objCBIndex = objCBIndex++;
		leftSphereRitem->mat = materials["sphere"].get();
		leftSphereRitem->geo = geometries["shapeGeo"].get();
		leftSphereRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->indexCount = leftSphereRitem->geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->baseVertexLocation = leftSphereRitem->geo->DrawArgs["sphere"].BaseVertexLocation;
		leftSphereRitem->startIndexLocation = leftSphereRitem->geo->DrawArgs["sphere"].StartIndexLocation;

		XMStoreFloat4x4(&rightSphereRitem->world, rightSphereWorld);
		XMStoreFloat4x4(&rightSphereRitem->texTransform, XMMatrixScaling(7.0f, 7.f, 1.0f));
		rightSphereRitem->NumFramesDirty = gNumFrameResources;
		rightSphereRitem->objCBIndex = objCBIndex++;
		rightSphereRitem->mat = materials["sphere"].get();
		rightSphereRitem->geo = geometries["shapeGeo"].get();
		rightSphereRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->indexCount = rightSphereRitem->geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->baseVertexLocation = rightSphereRitem->geo->DrawArgs["sphere"].BaseVertexLocation;
		rightSphereRitem->startIndexLocation = rightSphereRitem->geo->DrawArgs["sphere"].StartIndexLocation;

		ritemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		ritemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		ritemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
		ritemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());
		allRitems.push_back(std::move(leftCylRitem));
		allRitems.push_back(std::move(rightCylRitem));
		allRitems.push_back(std::move(leftSphereRitem));
		allRitems.push_back(std::move(rightSphereRitem));
		
	}
	objCount = allRitems.size();
}

void D3D12InitApp::DrawRenderItems(std::vector<RenderItem*> ritems)
{
	auto objCBByteSize = CalcConstantBufferByteSize<ObjectConstants>();
	auto matCBByteSize = CalcConstantBufferByteSize<MatConstants>();
	auto objCB = mCurrFrameResource->objCB->Resource();
	auto matCB = mCurrFrameResource->matCB->Resource();

	//遍历渲染项数组
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

void D3D12InitApp::BuildFrameResources()
{

	for (int i = 0; i < gNumFrameResources; i++)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(
			device.Get(),
			1,
			objCount,
			matCount
		));

	}
}

