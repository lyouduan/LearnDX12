#include "./src/stdafx.h"

#define Instance

#ifdef Instance
#include "./src/InstancingAndCullingApp.h"
#define APP InstancingAndCullingApp
#endif // 


#ifdef Tess
#include "./src/BasicTess.h"
#define APP BasicTess
#endif // 


#ifdef Stencil
#include "./src/StencilApp.h"
#define APP StencilApp
#endif // 

#ifdef D3D12
#include "./src/D3D12InitApp.h"
#define APP D3D12InitApp
#endif // 

#ifdef Land
#include "./src/LandAndWavesApp.h"
#define APP LandAndWavesApp
#endif // 

#ifdef CS
#include "./src/GaussianBlur.h"
#define APP GaussianBlur
#endif // 

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int nShowCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	APP app;
	if (!app.Init(hInstance, nShowCmd))
		return 0;
	return app.Run();
}