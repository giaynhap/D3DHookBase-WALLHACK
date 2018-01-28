// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#pragma comment(lib, "d3d11.lib")
#include <D3Dcompiler.h>//generateshader
#pragma comment(lib, "D3dcompiler.lib")
#include "Render.h"

typedef HRESULT(__stdcall *D3D11PresentHook) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef void(__stdcall *D3D11DrawIndexedHook) (ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
typedef void(__stdcall *D3D11ClearRenderTargetViewHook) (ID3D11DeviceContext* pContext, ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4]);
typedef void(__stdcall *D3D11CreateQueryHook) (ID3D11Device* pDevice, const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery);
typedef void(__stdcall *D3D11PSSetShaderResourcesHook) (ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews);

ID3D11Device *pDevice = NULL;
ID3D11DeviceContext *pContext = NULL;
Renderer* renderer;
DWORD_PTR* pSwapChainVtable = NULL;
DWORD_PTR* pDeviceContextVTable = NULL;

D3D11PresentHook phookD3D11Present = NULL;
D3D11DrawIndexedHook phookD3D11DrawIndexed = NULL;
D3D11ClearRenderTargetViewHook phookD3D11ClearRenderTargetView = NULL;
D3D11CreateQueryHook phookD3D11CreateQuery = NULL;
D3D11PSSetShaderResourcesHook phookD3D11PSSetShaderResources = NULL;


IFW1Factory *pFW1Factory = NULL;
IFW1FontWrapper *pFontWrapper = NULL;
bool firstTime2 = true;
bool firstTime = true;
void* detourBuffer[3];


ID3D11Buffer *veBuffer;
UINT Stride = 0;
UINT veBufferOffset = 0;
D3D11_BUFFER_DESC vedesc;
ID3D11Buffer *inBuffer;
DXGI_FORMAT inFormat;
UINT        inOffset;
D3D11_BUFFER_DESC indesc;
enum eDepthState
{
	ENABLED,
	DISABLED,
	READ_NO_WRITE,
	NO_READ_NO_WRITE,
	_DEPTH_COUNT
};
int stride = 24;
ID3D11DepthStencilState* myDepthStencilStates[static_cast<int>(eDepthState::_DEPTH_COUNT)];
ID3D11PixelShader* psRed = NULL;
ID3D11PixelShader* psGreen = NULL;

char *state;
ID3D11RasterizerState * rwState;
ID3D11RasterizerState * rsState;
//rendertarget
ID3D11Texture2D* RenderTargetTexture;
ID3D11RenderTargetView* RenderTargetView = NULL;
int numIndex = 17000;
int numIndexmin = 4300;
int IndexCountnum = 1000;
bool testColor = false;
#include <fstream>
char dlldir[320];
bool hackON = false;;
using namespace std;
bool showMenu = false;
char *GetDirectoryFile(char *filename)
{
	static char path[320];
	strcpy_s(path, dlldir);
	strcat_s(path, filename);
	return path;
}
ID3D11DepthStencilState* g_depthEnabled;
ID3D11DepthStencilState* g_depthDisabled;
//log
void drawMenu();
void getKEY()
{

	if (showMenu)
	{
		if (GetAsyncKeyState('O') & 1) stride += 1;
		if (GetAsyncKeyState('P') & 1) stride -= 1;

		if (GetAsyncKeyState('K') & 1) numIndex += 100;
		if (GetAsyncKeyState('L') & 1) numIndex -= 100;

		if (GetAsyncKeyState('H') & 1) numIndexmin += 100;
		if (GetAsyncKeyState('J') & 1) numIndexmin -= 100;

		if (GetAsyncKeyState('U') & 1) IndexCountnum += 100;
		if (GetAsyncKeyState('I') & 1) IndexCountnum -= 100;
	}


	if (GetAsyncKeyState('V') & 1) hackON = !hackON;
	if (GetAsyncKeyState('B') & 1) showMenu = !showMenu;
	if (GetAsyncKeyState('N') & 1) testColor = !testColor;


}
void Log(const char *fmt, ...)
{
	if (!fmt)	return;

	char		text[4096];
	va_list		ap;
	va_start(ap, fmt);
	vsprintf_s(text, fmt, ap);
	va_end(ap);

	ofstream logfile(GetDirectoryFile("log.txt"), ios::app);
	if (logfile.is_open() && text)	logfile << text << endl;
	logfile.close();
}

void SetDepthStencilState(eDepthState aState)
{
	pContext->OMSetDepthStencilState(myDepthStencilStates[aState], 1);

}
HRESULT GenerateShader(ID3D11Device* pD3DDevice, ID3D11PixelShader** pShader, float r, float g, float b)
{
	char szCast[] = "struct VS_OUT"
		"{"
		" float4 Position : SV_Position;"
		" float4 Color : COLOR0;"
		"};"

		"float4 main( VS_OUT input ) : SV_Target"
		"{"
		" float4 fake;"
		" fake.a = 0.4f;"
		" fake.r = %f;"
		" fake.g = %f;"
		" fake.b = %f;"
		" return fake;"
		"}";
 
	ID3D10Blob* pBlob;
	char szPixelShader[1000];

	sprintf_s(szPixelShader, szCast, r, g, b);

	ID3DBlob* d3dErrorMsgBlob;

	HRESULT hr = D3DCompile(szPixelShader, sizeof(szPixelShader), "shader", NULL, NULL, "main", "ps_4_0", NULL, NULL, &pBlob, &d3dErrorMsgBlob);

	if (FAILED(hr))
		return hr;

	hr = pD3DDevice->CreatePixelShader((DWORD*)pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, pShader);

	if (FAILED(hr))
		return hr;

	return S_OK;
}

ID3D11SamplerState *pSamplerState;
void drawMenu(ID3D11DeviceContext* pContext);
HRESULT __stdcall hookD3D11Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	if (firstTime)
	{
		pSwapChain->GetDevice(__uuidof(pDevice), (void**)&pDevice);
		pDevice->GetImmediateContext(&pContext);

		FW1CreateFactory(FW1_VERSION, &pFW1Factory);
		pFW1Factory->CreateFontWrapper(pDevice, L"Tahoma", &pFontWrapper);

		pFW1Factory->Release();

		 
		//create font
		HRESULT hResult = FW1CreateFactory(FW1_VERSION, &pFW1Factory);
		hResult = pFW1Factory->CreateFontWrapper(pDevice, L"Tahoma", &pFontWrapper);
		pFW1Factory->Release();

		// use the back buffer address to create the render target
		//if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&RenderTargetTexture))))
		if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&RenderTargetTexture)))
		{
			pDevice->CreateRenderTargetView(RenderTargetTexture, NULL, &RenderTargetView);
			RenderTargetTexture->Release();
		}
 
		Log("Init hack \n");

		firstTime = false;
	 
		ID3D11DepthStencilState* sState;
		pContext->OMGetDepthStencilState(&sState, NULL);
		D3D11_DEPTH_STENCIL_DESC sDesc;
		sDesc.DepthEnable = true;
		pDevice->CreateDepthStencilState(&sDesc, &g_depthEnabled);
		sDesc.DepthEnable = false;
		pDevice->CreateDepthStencilState(&sDesc, &g_depthDisabled);

		
		//create depthstencilstate
		D3D11_DEPTH_STENCIL_DESC  stencilDesc;
		stencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
		stencilDesc.StencilEnable = true;
		stencilDesc.StencilReadMask = 0xFF;
		stencilDesc.StencilWriteMask = 0xFF;
		stencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		stencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		stencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		stencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		stencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		stencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
		stencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		stencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		stencilDesc.DepthEnable = true;
		stencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		pDevice->CreateDepthStencilState(&stencilDesc, &myDepthStencilStates[static_cast<int>(eDepthState::ENABLED)]);

		stencilDesc.DepthEnable = false;
		stencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		pDevice->CreateDepthStencilState(&stencilDesc, &myDepthStencilStates[static_cast<int>(eDepthState::DISABLED)]);

		stencilDesc.DepthEnable = false;
		stencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		stencilDesc.StencilEnable = false;
		stencilDesc.StencilReadMask = UINT8(0xFF);
		stencilDesc.StencilWriteMask = 0x0;
		pDevice->CreateDepthStencilState(&stencilDesc, &myDepthStencilStates[static_cast<int>(eDepthState::NO_READ_NO_WRITE)]);

		stencilDesc.DepthEnable = true;
		stencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; //
		stencilDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
		stencilDesc.StencilEnable = false;
		stencilDesc.StencilReadMask = UINT8(0xFF);
		stencilDesc.StencilWriteMask = 0x0;

		stencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_ZERO;
		stencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_ZERO;
		stencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		stencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;

		stencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_ZERO;
		stencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_ZERO;
		stencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_ZERO;
		stencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_NEVER;
		pDevice->CreateDepthStencilState(&stencilDesc, &myDepthStencilStates[static_cast<int>(eDepthState::READ_NO_WRITE)]);
	 


		//8k2 4k3
		if (!psRed)
			GenerateShader(pDevice, &psRed, 1.0f, 0.0f, 0.0f);

		if (!psGreen)
			GenerateShader(pDevice, &psGreen, 0.0f, 1.0f, 0.0f);
		renderer = new Renderer(pDevice);

	}
	getKEY();
	drawMenu( pContext);

	return phookD3D11Present(pSwapChain, SyncInterval, Flags);
}

void drawMenu(ID3D11DeviceContext* pContext)
{
	if (!showMenu) return;
	if (renderer != NULL)
	{
		renderer->begin();
		renderer->drawFilledRect(Vec4(16.0f, 250.0f, 200.0f, 190.0f), Color{ 0.0f ,0.0f,0.0f,0.7f });
		renderer->drawFilledRect(Vec4(16.0f, 250.0f, 200.0f, 20.0f), Color{ 0.85f ,0.26f,0.21f,0.8f });
		renderer->draw();
		renderer->end();
	}
		
	if (!pFontWrapper) return;
	
	pFontWrapper->DrawString(pContext, L"Giay Nhap Hack SkivesOut", 12.0f, 20.0f, 255.0f, 0xFFFFFFFF, FW1_RESTORESTATE);


 
	wchar_t str_stride[128];


	if (hackON)
		pFontWrapper->DrawString(pContext, L"[HackEnable]", 14.0f, 23.0f, 275.0f, 0xFF00FF00, FW1_RESTORESTATE);
	else pFontWrapper->DrawString(pContext, L"[HackDisable]", 14.0f, 23.0f, 275.0f, 0xFFFFFFFF, FW1_RESTORESTATE);



	swprintf_s(str_stride, L"[+O P-]Stride: %d", stride);
	pFontWrapper->DrawString(pContext, str_stride, 13.0f, 20.0f, 335.0f, 0xFFFFFFFF, FW1_RESTORESTATE);

	swprintf_s(str_stride, L"[+K L-]MaxIndexNum: %d", numIndex);
	pFontWrapper->DrawString(pContext, str_stride, 13.0f, 20.0f, 365.0f, 0xFFFFFFFF, FW1_RESTORESTATE);

	swprintf_s(str_stride, L"[+H J-]MinIndexNum: %d", numIndexmin);
	pFontWrapper->DrawString(pContext, str_stride, 13.0f, 20.0f, 395.0f, 0xFFFFFFFF, FW1_RESTORESTATE);



	swprintf_s(str_stride, L"[+H J-]IndexCountnum: %d", IndexCountnum);
	pFontWrapper->DrawString(pContext, str_stride, 13.0f, 20.0f, 425.0f, 0xFFFFFFFF, FW1_RESTORESTATE);

	
	/*
	wchar_t str_stride[128];
	swprintf_s(str_stride, L"Stride: %d  numIndex %d  numIndexmin  %d ", stride, numIndex, numIndexmin);
	pFontWrapper->DrawString(pContext, str_stride, 16.0f, 16.0f, 40.0f, 0xffff1612, FW1_RESTORESTATE);
	
	*/


	



}


void __stdcall hookD3D11CreateQuery(ID3D11Device* pDevice, const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery)
{
	//Disable Occlusion which prevents rendering player models through certain objects (used by wallhack to see models through walls at all distances, REDUCES FPS)
	/*
	if (pQueryDesc->Query == D3D11_QUERY_OCCLUSION)
	{
		D3D11_QUERY_DESC oqueryDesc = CD3D11_QUERY_DESC();
		(&oqueryDesc)->MiscFlags = pQueryDesc->MiscFlags;
		(&oqueryDesc)->Query = D3D11_QUERY_TIMESTAMP;

		return phookD3D11CreateQuery(pDevice, &oqueryDesc, ppQuery);
	}*/

	return phookD3D11CreateQuery(pDevice, pQueryDesc, ppQuery);
}

D3D11_SHADER_RESOURCE_VIEW_DESC  Descr;
void __stdcall hookD3D11PSSetShaderResources(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  
	 /*
	 //alternative wallhack example for f'up games
	 if (Descr.Format == 56)
	 {
	 pContext->PSSetShader(psRed, NULL, NULL);
	 SetDepthStencilState(DISABLED);
	 }
	 else
	 if(pssrStartSlot == 1) //if black screen, find correct pssrStartSlot
	 SetDepthStencilState(READ_NO_WRITE);
	 */



	return phookD3D11PSSetShaderResources(pContext, StartSlot, NumViews, ppShaderResourceViews);
}
UINT stencilRef = 0;
ID3D11DepthStencilState* pDepthStencilState = NULL;

void __stdcall hookD3D11DrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{

	if (firstTime2)
	{
	
		firstTime2 = false;
	}
 
	pContext->IAGetVertexBuffers(0, 1, &veBuffer, &Stride, &veBufferOffset);
	if (veBuffer)
		veBuffer->GetDesc(&vedesc);
	
	if (veBuffer != NULL) { veBuffer->Release(); veBuffer = NULL; }

	//get indesc.ByteWidth
	pContext->IAGetIndexBuffer(&inBuffer, &inFormat, &inOffset);
	if (inBuffer)
		inBuffer->GetDesc(&indesc);

	if (inBuffer != NULL) { inBuffer->Release(); inBuffer = NULL; }
	

	

 
	if (hackON)
	{
		if (Stride == stride  && indesc.ByteWidth <  numIndex && numIndexmin < indesc.ByteWidth && IndexCount > IndexCountnum)
		{

		 

	 
		//	char log[512];
	//		sprintf_s(log, "StartIndexLocation: %d , IndexCount  %d  , BaseVertexLocation  %d, indesc.CPUAccessFlags %d, indesc.MiscFlags  %d, indesc.Usage	 %d, veBufferOffset %d , vedesc.StructureByteStride %d \n", StartIndexLocation,IndexCount,BaseVertexLocation, indesc.CPUAccessFlags, indesc.MiscFlags, indesc.Usage, veBufferOffset, vedesc.StructureByteStride);
	//		Log(log);
			if (!testColor)
	 		SetDepthStencilState(DISABLED);
			if (testColor)
			pContext->PSSetShader(psRed, NULL, NULL);
		 
			phookD3D11DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
			//pContext->RSSetState(rsState);   
	 	//pContext->PSSetShader(psGreen, NULL, NULL);

			//if (pssrStartSlot == 1) //if black screen, find correct pssrStartSlot
		 
			SetDepthStencilState(READ_NO_WRITE);
			SetDepthStencilState(ENABLED);
		}
	}
 
 
	return phookD3D11DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
}
 
void __stdcall hookD3D11ClearRenderTargetView(ID3D11DeviceContext* pContext, ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4])
{
	return phookD3D11ClearRenderTargetView(pContext, pRenderTargetView, ColorRGBA);
}

const void* DetourFuncVTable(SIZE_T* src, const BYTE* dest, const DWORD index)
{
	DWORD dwVirtualProtectBackup;
	SIZE_T* const indexPtr = &src[index];
	const void* origFunc = (void*)*indexPtr;
	VirtualProtect(indexPtr, sizeof(SIZE_T), PAGE_EXECUTE_READWRITE, &dwVirtualProtectBackup);
	*indexPtr = (SIZE_T)dest;
	VirtualProtect(indexPtr, sizeof(SIZE_T), dwVirtualProtectBackup, &dwVirtualProtectBackup);
	return origFunc;
}
 
const void* DetourFunc(BYTE* const src, const BYTE* dest, const DWORD length)
{
	BYTE* jump = new BYTE[length + 5];
	for (int i = 0; i < sizeof(detourBuffer) / sizeof(void*); ++i)
	{
		if (!detourBuffer[i])
		{
			detourBuffer[i] = jump;
			break;
		}
	}

	DWORD dwVirtualProtectBackup;
	VirtualProtect(src, length, PAGE_READWRITE, &dwVirtualProtectBackup);

	memcpy(jump, src, length);
	jump += length;

	jump[0] = 0xE9;
	*(DWORD*)(jump + 1) = (DWORD)(src + length - jump) - 5;

	src[0] = 0xE9;
	*(DWORD*)(src + 1) = (DWORD)(dest - src) - 5;

	VirtualProtect(src, length, dwVirtualProtectBackup, &dwVirtualProtectBackup);

	return jump - length;
}
 

DWORD __stdcall InitializeHook(LPVOID)
{
	HWND hWnd = GetForegroundWindow();
	IDXGISwapChain* pSwapChain;

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	swapChainDesc.BufferCount = 1;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = hWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = TRUE;//((GetWindowLong(hWnd, GWL_STYLE) & WS_POPUP) != 0) ? FALSE : TRUE;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, NULL, &featureLevel, 1
		, D3D11_SDK_VERSION, &swapChainDesc, &pSwapChain, &pDevice, NULL, &pContext)))
	{
		MessageBoxA(hWnd, "Failed to create directX device and swapchain!", "uBoos?", MB_ICONERROR);
		return NULL;
	}

	pSwapChainVtable = (DWORD_PTR*)pSwapChain;
	pSwapChainVtable = (DWORD_PTR*)pSwapChainVtable[0];

	pDeviceContextVTable = (DWORD_PTR*)pContext;
	pDeviceContextVTable = (DWORD_PTR*)pDeviceContextVTable[0];

#ifdef _WIN64
	phookD3D11Present = (D3D11PresentHook)DetourFunc64((BYTE*)pSwapChainVtable[8], (BYTE*)hookD3D11Present, 16);
	phookD3D11DrawIndexed = (D3D11DrawIndexedHook)DetourFunc64((BYTE*)pDeviceContextVTable[12], (BYTE*)hookD3D11DrawIndexed, 16);
	phookD3D11ClearRenderTargetView = (D3D11ClearRenderTargetViewHook)DetourFunc64((BYTE*)pDeviceContextVTable[50], (BYTE*)hookD3D11ClearRenderTargetView, 16);
#else
	phookD3D11Present = (D3D11PresentHook)DetourFunc((BYTE*)pSwapChainVtable[8], (BYTE*)hookD3D11Present, 5);
	phookD3D11DrawIndexed = (D3D11DrawIndexedHook)DetourFunc((BYTE*)pDeviceContextVTable[12], (BYTE*)hookD3D11DrawIndexed, 5);
	phookD3D11ClearRenderTargetView = (D3D11ClearRenderTargetViewHook)DetourFunc((BYTE*)pDeviceContextVTable[50], (BYTE*)hookD3D11ClearRenderTargetView, 5);
	phookD3D11CreateQuery = (D3D11CreateQueryHook)DetourFunc((BYTE*)pDeviceContextVTable[24], (BYTE*)hookD3D11CreateQuery, 5);
	phookD3D11PSSetShaderResources = (D3D11PSSetShaderResourcesHook)DetourFunc((BYTE*)pDeviceContextVTable[8], (BYTE*)hookD3D11PSSetShaderResources, 5);

	

	DWORD dwOld;
	VirtualProtect(phookD3D11Present, 2, PAGE_EXECUTE_READWRITE, &dwOld);
#endif

	pDevice->Release();
	pContext->Release();
	pSwapChain->Release();

	return NULL;
}
 
bool wallhack = true;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		memset(detourBuffer, 0, sizeof(detourBuffer) * sizeof(void*));
		CreateThread(NULL, 0, InitializeHook, NULL, 0, NULL);
		return TRUE;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		if (pFontWrapper)
		{
			pFontWrapper->Release();
		}

		for (int i = 0; i < sizeof(detourBuffer) / sizeof(void*); ++i)
		{
			if (detourBuffer[i])
			{
				VirtualFree(detourBuffer[i], 0, MEM_RELEASE);
 
			}
		}
		break;
	}
	return TRUE;
}
 