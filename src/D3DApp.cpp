#include "D3DApp.h"
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return D3DApp::GetApp()->MsgPro(hwnd, msg, wParam, lParam);
}

LRESULT D3DApp::MsgPro(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	//��Ϣ����
	switch (msg)
	{
	case WM_SIZE:
		width  = LOWORD(lParam);
		height = HIWORD(lParam);
		if (device)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mAppPaused = true;
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mAppPaused = false;
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{

				// Restoring from minimized state?
				if (mMinimized)
				{
					mAppPaused = false;
					mMinimized = false;
					OnResize();
				}

				// Restoring from maximized state?
				else if (mMaximized)
				{
					mAppPaused = false;
					mMaximized = false;
					OnResize();
				}
				else if (mResizing)
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					OnResize();
				}
			}
		}
		return 0;

	//��갴������ʱ�Ĵ����������ң�
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	//��갴��̧��ʱ�Ĵ����������ң�
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	//����ƶ��Ĵ���
	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	//�����ڱ�����ʱ����ֹ��Ϣѭ��
	case WM_DESTROY:
		PostQuitMessage(0);	//��ֹ��Ϣѭ����������WM_QUIT��Ϣ
		return 0;
	default:
		break;
	}
	//������û�д������Ϣת����Ĭ�ϵĴ��ڹ���
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

D3DApp* D3DApp::mApp = nullptr;

D3DApp::D3DApp()
{
	assert(mApp == nullptr);
	mApp = this;
}

D3DApp::~D3DApp()
{
}

bool D3DApp::Init(HINSTANCE hInstance, int nShowCmd)
{
	if (!InitWindow(hInstance, nShowCmd))
	{
		return false;
	}
	if (!InitDirectX())
	{
		return false;
	}

	// ���µ���
	OnResize();

	return true;
}

bool D3DApp::InitWindow(HINSTANCE hInstance, int nShowCmd)
{
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;	//����������߸ı䣬�����»��ƴ���
	wc.lpfnWndProc = MainWndProc;	//ָ�����ڹ���
	wc.cbClsExtra = 0;	//�����������ֶ���Ϊ��ǰӦ�÷��������ڴ�ռ䣨���ﲻ���䣬������0��
	wc.cbWndExtra = 0;	//�����������ֶ���Ϊ��ǰӦ�÷��������ڴ�ռ䣨���ﲻ���䣬������0��
	wc.hInstance = hInstance;	//Ӧ�ó���ʵ���������WinMain���룩
	wc.hIcon = LoadIcon(0, IDC_ARROW);	//ʹ��Ĭ�ϵ�Ӧ�ó���ͼ��
	wc.hCursor = LoadCursor(0, IDC_ARROW);	//ʹ�ñ�׼�����ָ����ʽ
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);	//ָ���˰�ɫ������ˢ���
	wc.lpszMenuName = 0;	//û�в˵���
	wc.lpszClassName = L"MainWnd";	//������
	//������ע��ʧ��
	if (!RegisterClass(&wc))
	{
		//��Ϣ����������1����Ϣ���������ھ������ΪNULL������2����Ϣ����ʾ���ı���Ϣ������3�������ı�������4����Ϣ����ʽ
		MessageBox(0, L"RegisterClass Failed", 0, 0);
		return 0;
	}

	//������ע��ɹ�
	RECT R;	//�ü�����
	R.left = 0;
	R.top = 0;
	R.right = width;
	R.bottom = height;
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);	//���ݴ��ڵĿͻ�����С���㴰�ڵĴ�С
	int width = R.right - R.left;
	int hight = R.bottom - R.top;

	//��������,���ز���ֵ
	mhMainWnd = CreateWindow(L"MainWnd", L"DX12Initialize", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, hight, 0, 0, hInstance, 0);
	//���ڴ���ʧ��
	if (!mhMainWnd)
	{
		MessageBox(0, L"CreatWindow Failed", 0, 0);
		return 0;
	}
	//���ڴ����ɹ�,����ʾ�����´���
	ShowWindow(mhMainWnd, nShowCmd);
	UpdateWindow(mhMainWnd);

	return true;
}

int D3DApp::Run()
{
	MSG msg = { 0 };
	gt.Reset();
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			gt.Tick();
			if (!gt.IsStopped())
			{
				CalculateFrameState();
				Update();
				Draw();
			}
			else
			{
				Sleep(100);
			}
		}
	}

	return (int)msg.wParam;
}

bool D3DApp::InitDirectX()
{
/*����D3D12���Բ�*/
#if defined(DEBUG) || defined(_DEBUG)
	{
		ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif

	CreateDevice();
	CreateFence();
	GetDescriptorSize();
	SetMSAA();
	CreateCommandObject();
	CreateSwapChain();
	CreateDescriptorHeap();
	

	return true;
}


D3DApp* D3DApp::GetApp()
{
	return mApp;
}

void D3DApp::Draw()
{
	
}

void D3DApp::OnResize()
{
	assert(device);
	assert(swapChain);
	assert(cmdAllocator);
	// ͬ��
	FlushCmdQueue();
	ThrowIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));
	cmdList->Close();
	// �ͷ���Դ
	for (int i = 0; i < FrameCount; i++)
	{
		swapChainBuffer[i].Reset();
	}
	depthStencilBuffer.Reset();

	// resize
	ThrowIfFailed(swapChain->ResizeBuffers(
		2,
		width,
		height,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
		));

	mCurrentBackBuffer = 0;

	CreateRTV();
	CreateDSV();
	CreateViewPortAndScissorRect();
}

void D3DApp::Update()
{
}


void D3DApp::CreateDevice()
{
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));

	ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
}

void D3DApp::CreateFence()
{
	ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
}

void D3DApp::GetDescriptorSize()
{
	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	cbvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void D3DApp::SetMSAA()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaQualityLevels;
	msaaQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	msaaQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msaaQualityLevels.NumQualityLevels = 0;
	msaaQualityLevels.SampleCount = 1;

	ThrowIfFailed(device->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, 
		&msaaQualityLevels,
		sizeof(msaaQualityLevels)));
	assert(msaaQualityLevels.NumQualityLevels > 0);
}

void D3DApp::CreateCommandObject()
{
	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {}; // default initialized value
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(device->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&cmdQueue)));

	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator)));

	ThrowIfFailed(device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		cmdAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&cmdList)));

	cmdList->Close();
}

void D3DApp::CreateSwapChain()
{
	swapChain.Reset();

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	swapChainDesc.BufferDesc.Width = width;
	swapChainDesc.BufferDesc.Height = height;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = mhMainWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ThrowIfFailed(dxgiFactory->CreateSwapChain(cmdQueue.Get(), &swapChainDesc, swapChain.GetAddressOf()));

}

void D3DApp::CreateDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)));
}

void D3DApp::CreateRTV()
{
	rtvHeapHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();

	for (int i = 0; i < FrameCount; i++)
	{
		swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainBuffer[i].GetAddressOf()));

		device->CreateRenderTargetView(
			swapChainBuffer[i].Get(),
			nullptr,
			rtvHeapHandle
		);
		rtvHeapHandle.Offset(1, dsvDescriptorSize);
	}
}

void D3DApp::CreateDSV()
{
	D3D12_RESOURCE_DESC dsvResourceDesc;
	dsvResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	dsvResourceDesc.Alignment = 0;
	dsvResourceDesc.DepthOrArraySize = 1;
	dsvResourceDesc.Width = width;
	dsvResourceDesc.Height = height;
	dsvResourceDesc.MipLevels = 1;
	dsvResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	dsvResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	dsvResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvResourceDesc.SampleDesc.Count = 1;
	dsvResourceDesc.SampleDesc.Quality = 0;
	
	CD3DX12_CLEAR_VALUE optClear;
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1;
	optClear.DepthStencil.Stencil = 0;

	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(
		&prop,
		D3D12_HEAP_FLAG_NONE,
		&dsvResourceDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(&depthStencilBuffer)
	));

	device->CreateDepthStencilView(
		depthStencilBuffer.Get(),
		nullptr,
		dsvHeap->GetCPUDescriptorHandleForHeapStart());

}

void D3DApp::FlushCmdQueue()
{
	mCurrentFence++;
	cmdQueue->Signal(fence.Get(), mCurrentFence);
	if (fence->GetCompletedValue() < mCurrentFence)
	{
		eventHandle = CreateEvent(nullptr, false, false, L"FenceSetDone");
		fence->SetEventOnCompletion(mCurrentFence, eventHandle);
		WaitForSingleObject(eventHandle, INFINITE);

		CloseHandle(eventHandle);
	}
}

void D3DApp::CreateViewPortAndScissorRect()
{
	viewPort.TopLeftX = 0;
	viewPort.TopLeftY = 0;
	viewPort.Width = width;
	viewPort.Height = height;
	viewPort.MaxDepth = 1.0f;
	viewPort.MinDepth = 0.0f;

	//�ü��������ã�����������ض������޳���
	//ǰ����Ϊ���ϵ����꣬������Ϊ���µ�����
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = width;
	scissorRect.bottom = height;
}

void D3DApp::CalculateFrameState()
{
	static int frameCnt = 0;
	static float timeElapsed = 0.0f;

	frameCnt++;
	/*std::wstring text = std::to_wstring(gt.TotalTime());
	std::wstring windowText = text;
	SetWindowText(mhMainWnd, windowText.c_str());*/

	if (gt.TotalTime() - timeElapsed >= 1.0f)
	{
		float fps = (float)frameCnt;//ÿ�����֡
		float mspf = 1000.0f / fps;	//ÿ֡���ٺ���

		std::wstring fpsStr = std::to_wstring(fps);//תΪ���ַ�
		std::wstring mspfStr = std::to_wstring(mspf);
		//��֡������ʾ�ڴ�����
		std::wstring windowText = L"D3D12Init    fps:" + fpsStr + L"    " + L"mspf" + mspfStr;
		SetWindowText(mhMainWnd, windowText.c_str());

		//Ϊ������һ��֡��ֵ������
		frameCnt = 0;
		timeElapsed += 1.0f;

	}
}
