/* Refresh - XNA-inspired 3D Graphics Library with modern capabilities
 *
 * Copyright (c) 2020 Evan Hemsley
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Evan "cosmonaut" Hemsley <evan@moonside.games>
 *
 */

/* Function Pointer Signatures */
typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY1)(const GUID* riid, void** ppFactory);

 /* IIDs (from https://magnumdb.com) */

static const IID D3D_IID_IDXGIFactory1 = { 0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87} };
static const IID D3D_IID_IDXGIFactory6 = { 0xc1b6694f,0xff09,0x44a9,{0xb0,0x3c,0x77,0x90,0x0a,0x0a,0x1d,0x17} };
static const IID D3D_IID_IDXGIAdapter1 = { 0x29038f61,0x3839,0x4626,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05} };
static const IID D3D_IID_ID3D11Texture2D = { 0x6f15aaf2,0xd208,0x4e89,{0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c} };

/* IDXGIFactory6 (taken from dxgi1_6.h, cleaned up a bit) */
typedef enum
{
	DXGI_FEATURE_PRESENT_ALLOW_TEARING = 0
} DXGI_FEATURE;

typedef enum
{
	DXGI_GPU_PREFERENCE_UNSPECIFIED = 0,
	DXGI_GPU_PREFERENCE_MINIMUM_POWER = (DXGI_GPU_PREFERENCE_UNSPECIFIED + 1),
	DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE = (DXGI_GPU_PREFERENCE_MINIMUM_POWER + 1)
} DXGI_GPU_PREFERENCE;

typedef struct IDXGIFactory6 IDXGIFactory6;
typedef struct IDXGIFactory6Vtbl
{
	HRESULT(STDMETHODCALLTYPE* QueryInterface)(
		IDXGIFactory6* This,
		REFIID riid,
		void** ppvObject);

	ULONG(STDMETHODCALLTYPE* AddRef)(
		IDXGIFactory6* This);

	ULONG(STDMETHODCALLTYPE* Release)(
		IDXGIFactory6* This);

	HRESULT(STDMETHODCALLTYPE* SetPrivateData)(
		IDXGIFactory6* This,
		REFGUID Name,
		UINT DataSize,
		const void* pData);

	HRESULT(STDMETHODCALLTYPE* SetPrivateDataInterface)(
		IDXGIFactory6* This,
		REFGUID Name,
		const IUnknown* pUnknown);

	HRESULT(STDMETHODCALLTYPE* GetPrivateData)(
		IDXGIFactory6* This,
		REFGUID Name,
		UINT* pDataSize,
		void* pData);

	HRESULT(STDMETHODCALLTYPE* GetParent)(
		IDXGIFactory6* This,
		REFIID riid,
		void** ppParent);

	HRESULT(STDMETHODCALLTYPE* EnumAdapters)(
		IDXGIFactory6* This,
		UINT Adapter,
		IDXGIAdapter** ppAdapter);

	HRESULT(STDMETHODCALLTYPE* MakeWindowAssociation)(
		IDXGIFactory6* This,
		HWND WindowHandle,
		UINT Flags);

	HRESULT(STDMETHODCALLTYPE* GetWindowAssociation)(
		IDXGIFactory6* This,
		HWND* pWindowHandle);

	HRESULT(STDMETHODCALLTYPE* CreateSwapChain)(
		IDXGIFactory6* This,
		IUnknown* pDevice,
		DXGI_SWAP_CHAIN_DESC* pDesc,
		IDXGISwapChain** ppSwapChain);

	HRESULT(STDMETHODCALLTYPE* CreateSoftwareAdapter)(
		IDXGIFactory6* This,
		HMODULE Module,
		IDXGIAdapter** ppAdapter);

	HRESULT(STDMETHODCALLTYPE* EnumAdapters1)(
		IDXGIFactory6* This,
		UINT Adapter,
		IDXGIAdapter1** ppAdapter);

	BOOL(STDMETHODCALLTYPE* IsCurrent)(
		IDXGIFactory6* This);

	BOOL(STDMETHODCALLTYPE* IsWindowedStereoEnabled)(
		IDXGIFactory6* This);

	HRESULT(STDMETHODCALLTYPE* CreateSwapChainForHwnd)(
		IDXGIFactory6* This,
		IUnknown* pDevice,
		HWND hWnd,
		void* pDesc,
		void* pFullscreenDesc,
		void* pRestrictToOutput,
		void** ppSwapChain);

	HRESULT(STDMETHODCALLTYPE* CreateSwapChainForCoreWindow)(
		IDXGIFactory6* This,
		IUnknown* pDevice,
		IUnknown* pWindow,
		void* pDesc,
		void* pRestrictToOutput,
		void** ppSwapChain);

	HRESULT(STDMETHODCALLTYPE* GetSharedResourceAdapterLuid)(
		IDXGIFactory6* This,
		HANDLE hResource,
		LUID* pLuid);

	HRESULT(STDMETHODCALLTYPE* RegisterStereoStatusWindow)(
		IDXGIFactory6* This,
		HWND WindowHandle,
		UINT wMsg,
		DWORD* pdwCookie);

	HRESULT(STDMETHODCALLTYPE* RegisterStereoStatusEvent)(
		IDXGIFactory6* This,
		HANDLE hEvent,
		DWORD* pdwCookie);

	void (STDMETHODCALLTYPE* UnregisterStereoStatus)(
		IDXGIFactory6* This,
		DWORD dwCookie);

	HRESULT(STDMETHODCALLTYPE* RegisterOcclusionStatusWindow)(
		IDXGIFactory6* This,
		HWND WindowHandle,
		UINT wMsg,
		DWORD* pdwCookie);

	HRESULT(STDMETHODCALLTYPE* RegisterOcclusionStatusEvent)(
		IDXGIFactory6* This,
		HANDLE hEvent,
		DWORD* pdwCookie);

	void (STDMETHODCALLTYPE* UnregisterOcclusionStatus)(
		IDXGIFactory6* This,
		DWORD dwCookie);

	HRESULT(STDMETHODCALLTYPE* CreateSwapChainForComposition)(
		IDXGIFactory6* This,
		IUnknown* pDevice,
		void* pDesc,
		void* pRestrictToOutput,
		void** ppSwapChain);

	UINT(STDMETHODCALLTYPE* GetCreationFlags)(
		IDXGIFactory6* This);

	HRESULT(STDMETHODCALLTYPE* EnumAdapterByLuid)(
		IDXGIFactory6* This,
		LUID AdapterLuid,
		REFIID riid,
		void** ppvAdapter);

	HRESULT(STDMETHODCALLTYPE* EnumWarpAdapter)(
		IDXGIFactory6* This,
		REFIID riid,
		void** ppvAdapter);

	HRESULT(STDMETHODCALLTYPE* CheckFeatureSupport)(
		IDXGIFactory6* This,
		DXGI_FEATURE Feature,
		void* pFeatureSupportData,
		UINT FeatureSupportDataSize);

	HRESULT(STDMETHODCALLTYPE* EnumAdapterByGpuPreference)(
		IDXGIFactory6* This,
		UINT Adapter,
		DXGI_GPU_PREFERENCE GpuPreference,
		REFIID riid,
		void** ppvAdapter);
} IDXGIFactory6Vtbl;

struct IDXGIFactory6
{
	struct IDXGIFactory6Vtbl* lpVtbl;
};

#define IDXGIFactory6_EnumAdapterByGpuPreference(This,Adapter,GpuPreference,riid,ppvAdapter)	\
	( (This)->lpVtbl -> EnumAdapterByGpuPreference(This,Adapter,GpuPreference,riid,ppvAdapter) ) 
