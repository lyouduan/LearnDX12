#pragma once
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>

#include <stdexcept>
#include <cassert>
#include <array>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include "d3dx12.h"

#include <DirectXColors.h>
#include <DirectXMath.h>
#include <DirectxPackedVector.h>
#include <DirectXCollision.h>

#include "MathHelper.h"
#include "d3dUtil.h"
#include "GameTime.h"
#include "UploadBuffer.h"
#include "ShadowMap.h"
#include "ShadowRenderTarget.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include "Wave.h"
#include "DDSTextureLoader.h"
#include "Camera.h"
#include "CubeRenderTarget.h"
