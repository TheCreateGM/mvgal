/**
 * @file d3d_wrapper.c
 * @brief Direct3D Interception Layer for Wine/Proton
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * Intercepts Direct3D 11/12 API calls via LD_PRELOAD when running
 * Windows applications through Wine or Steam Proton.
 *
 * This allows MVGAL to distribute Direct3D workloads across multiple GPUs
 * in a multi-vendor environment.
 *
 * Usage:
 *   # For Wine
 *   export MVGAL_D3D_ENABLED=1
 *   export LD_PRELOAD=/path/to/libmvgal_d3d.so
 *   wine your_application.exe
 *
 *   # For Proton
 *   export MVGAL_D3D_ENABLED=1
 *   export LD_PRELOAD=/path/to/libmvgal_d3d.so
 *   steam run your_game
 *
 * Environment Variables:
 *   MVGAL_D3D_ENABLED=1       - Enable Direct3D interception (default: 1)
 *   MVGAL_D3D_DEBUG=1         - Enable debug logging (default: 0)
 *   MVGAL_D3D_STRATEGY=round_robin - Workload distribution strategy
 *   MVGAL_D3D_GPUS="0,1"      - GPU indices to use
 */

// Note: D3D11/12 device methods use COM vtables, not standalone C functions.
// LD_PRELOAD cannot intercept vtable calls directly. The functions below document
// intended interception points for future COM vtable patching support.
// Suppress unused function warnings for these placeholder implementations.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <pthread.h>

// MVGAL headers
#include <mvgal/mvgal.h>
#include <mvgal/mvgal_scheduler.h>
#include <mvgal/mvgal_execution.h>

// Windows success/failure macros
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

/******************************************************************************
 * Windows Types for Linux
 ******************************************************************************/

#define S_OK  ((HRESULT)0x00000000)
#define E_FAIL ((HRESULT)0x80004005)
#define E_NOTIMPL ((HRESULT)0x80004001)

// WINAPI calling convention - empty on Linux
#define WINAPI
#define CALLBACK

// Additional Windows types
typedef void* HMODULE;
typedef void* HINSTANCE;

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef uint64_t UINT64;
typedef int32_t HRESULT;
typedef int32_t BOOL;
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned long SIZE_T;

// GUID/IID types
typedef struct _GUID {
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} GUID;

#define REFIID const GUID*
#define REFCLSID const GUID*

// Opaque COM interface pointers
typedef void* IUnknown;
typedef void* IDXGIObject;
typedef void* IDXGIFactory;
typedef void* IDXGIAdapter;
typedef void* ID3D11Device;
typedef void* ID3D12Device;
typedef void* ID3D11DeviceContext;
typedef void* ID3D11CommandList;
typedef void* ID3D12CommandQueue;
typedef void* ID3D11Resource;
typedef void* ID3D12Resource;
typedef void* ID3D11Buffer;
typedef void* ID3D12Heap;
typedef void* IDXGISwapChain;
typedef void* ID3D11Texture2D;
typedef void* ID3D11ShaderResourceView;
typedef void* ID3D11RenderTargetView;
typedef void* ID3D11DepthStencilView;
typedef void* ID3D12GraphicsCommandList;
typedef void* ID3D12CommandAllocator;
typedef void* ID3D12PipelineState;
typedef void* ID3D12Fence;
typedef void* ID3D12CommandList;
typedef int32_t INT;

// Additional D3D12 types (simplified)
typedef struct D3D12_CPU_DESCRIPTOR_HANDLE { void* ptr; } D3D12_CPU_DESCRIPTOR_HANDLE;
typedef struct D3D12_RECT { LONG left, top, right, bottom; } D3D12_RECT;

// D3D11 structures
typedef struct {
    UINT Width;
    UINT Height;
} DXGI_MODE_DESC;

// Feature level enum
typedef enum {
    D3D_FEATURE_LEVEL_9_1 = 0x9100,
    D3D_FEATURE_LEVEL_9_2 = 0x9200,
    D3D_FEATURE_LEVEL_9_3 = 0x9300,
    D3D_FEATURE_LEVEL_10_0 = 0xa000,
    D3D_FEATURE_LEVEL_10_1 = 0xa100,
    D3D_FEATURE_LEVEL_11_0 = 0xb000,
    D3D_FEATURE_LEVEL_11_1 = 0xb100,
    D3D_FEATURE_LEVEL_12_0 = 0xc000,
    D3D_FEATURE_LEVEL_12_1 = 0xc100,
} D3D_FEATURE_LEVEL;

// Swap chain description
typedef struct {
    DXGI_MODE_DESC BufferDesc;
    UINT SampleDescCount;
    UINT SampleDescQuality;
    UINT BufferUsage;
    UINT BufferCount;
    HRESULT OutputWindow;
    BOOL Windowed;
    UINT BufferFormat;  // DXGI_FORMAT as UINT for simplicity
    UINT SwapEffect;
    UINT Flags;
} DXGI_SWAP_CHAIN_DESC;

// D3D11 buffer description
typedef struct {
    UINT ByteWidth;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
} D3D11_BUFFER_DESC;

// D3D11 texture2D description
typedef struct {
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    UINT Format;
    UINT SampleDescCount;
    UINT SampleDescQuality;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D11_TEXTURE2D_DESC;

// D3D11 shader resource view description
typedef struct {
    UINT Format;
    UINT ViewDimension;
    UINT Flags;  // Simplified - would include union in real D3D11
} D3D11_SHADER_RESOURCE_VIEW_DESC;

// D3D11 render target view description
typedef struct {
    UINT Format;
    UINT ViewDimension;
    UINT Flags;  // Simplified
} D3D11_RENDER_TARGET_VIEW_DESC;

// D3D11 device context state description
typedef struct {
    UINT Flags;
} D3D11_DEVICE_CONTEXT_STATE_DESC;

// D3D12 command queue description
typedef struct {
    UINT Type;
    UINT Priority;
    UINT Flags;
    UINT NodeMask;
} D3D12_COMMAND_QUEUE_DESC;

// D3D12 committed resource description
typedef struct {
    UINT HeapType;
    UINT CPUPageProperty;
    UINT MemoryPoolPreference;
    UINT CreationNodeMask;
    UINT VisibleNodeMask;
} D3D12_HEAP_PROPERTIES;

typedef struct {
    UINT Dimension;
    UINT Alignment;
    UINT Width;
    UINT Height;
    UINT DepthOrArraySize;
    UINT MipLevels;
    UINT Format;
    UINT SampleCount;
    UINT SampleQuality;
    UINT Layout;
    UINT Flags;
} D3D12_RESOURCE_DESC;

typedef struct {
    D3D12_HEAP_PROPERTIES HeapProperties;
    D3D12_RESOURCE_DESC ResourceDesc;
    UINT ResourceState;
    void* ClearValue;  // D3D12_CLEAR_VALUE*
} D3D12_COMMITTED_RESOURCE_DESC;

// D3D12 descriptor heap description
typedef struct {
    UINT Type;
    UINT NumDescriptors;
    UINT Flags;
    UINT NodeMask;
} D3D12_DESCRIPTOR_HEAP_DESC;

// D3D12 clear value (simplified)
typedef struct {
    UINT Format;
    float Color[4];
    float Depth;
    UINT Stencil;
} D3D12_CLEAR_VALUE;

// Note: DXGI_SWAP_CHAIN_DESC is already defined above (line 122-133)
// The original definition is kept; this is a comment to avoid duplicate definition

/******************************************************************************
 * Function Pointer Types
 ******************************************************************************/

typedef HRESULT (WINAPI *D3D11CreateDevice_t)(
    IDXGIAdapter *pAdapter,
    D3D_FEATURE_LEVEL DriverTypes,
    HMODULE Software,
    UINT Flags,
    const DWORD *pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel,
    ID3D11DeviceContext **ppImmediateContext
);

typedef HRESULT (WINAPI *D3D11CreateDeviceContextState_t)(
    ID3D11Device *pDevice,
    const D3D11_DEVICE_CONTEXT_STATE_DESC *pDesc,
    ID3D11DeviceContext **ppDeviceContextState
);

typedef HRESULT (WINAPI *D3D11CreateBuffer_t)(
    ID3D11Device *pDevice,
    const D3D11_BUFFER_DESC *pDesc,
    const void *pInitialData,
    ID3D11Buffer **ppBuffer
);

typedef HRESULT (WINAPI *CreateDXGIFactory1_t)(
    REFIID riid,
    void **ppFactory
);

typedef HRESULT (WINAPI *DXGIFactory_EnumAdapters1_t)(
    IDXGIFactory *pFactory,
    UINT Adapter,
    IDXGIAdapter **ppAdapter
);

typedef HRESULT (WINAPI *D3D12CreateDevice_t)(
    IUnknown *pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void **ppDevice
);

typedef HRESULT (WINAPI *D3D12Device_CreateCommandQueue_t)(
    ID3D12Device *pDevice,
    const D3D12_COMMAND_QUEUE_DESC *pDesc,
    REFIID riid,
    void **ppCommandQueue
);

typedef HRESULT (WINAPI *D3D12Device_CreateGraphicsPipelineState_t)(
    ID3D12Device *pDevice,
    void *pDesc,
    REFIID riid,
    void **ppPipelineState
);

/******************************************************************************
 * Additional Function Pointer Types - D3D11 Device Methods
 ******************************************************************************/

typedef HRESULT (WINAPI *D3D11Device_CreateBuffer_t)(
    ID3D11Device *pDevice,
    const D3D11_BUFFER_DESC *pDesc,
    const void *pInitialData,
    ID3D11Buffer **ppBuffer
);

typedef HRESULT (WINAPI *D3D11Device_CreateTexture2D_t)(
    ID3D11Device *pDevice,
    const D3D11_TEXTURE2D_DESC *pDesc,
    const void *pInitialData,
    ID3D11Texture2D **ppTexture2D
);

typedef HRESULT (WINAPI *D3D11Device_CreateShaderResourceView_t)(
    ID3D11Device *pDevice,
    ID3D11Resource *pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,
    ID3D11ShaderResourceView **ppSRView
);

typedef HRESULT (WINAPI *D3D11Device_CreateRenderTargetView_t)(
    ID3D11Device *pDevice,
    ID3D11Resource *pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
    ID3D11RenderTargetView **ppRTView
);

/******************************************************************************
 * Additional Function Pointer Types - D3D11 Device Context Methods
 ******************************************************************************/

typedef HRESULT (WINAPI *D3D11DeviceContext_Draw_t)(
    ID3D11DeviceContext *pContext,
    UINT VertexCount,
    UINT StartVertexLocation
);

typedef HRESULT (WINAPI *D3D11DeviceContext_DrawIndexed_t)(
    ID3D11DeviceContext *pContext,
    UINT IndexCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation
);

typedef HRESULT (WINAPI *D3D11DeviceContext_OMSetRenderTargets_t)(
    ID3D11DeviceContext *pContext,
    UINT NumViews,
    ID3D11RenderTargetView *const *ppRenderTargetViews,
    ID3D11DepthStencilView *pDepthStencilView
);

typedef HRESULT (WINAPI *D3D11DeviceContext_ClearRenderTargetView_t)(
    ID3D11DeviceContext *pContext,
    ID3D11RenderTargetView *pRenderTargetView,
    const float ColorRGBA[4]
);

typedef HRESULT (WINAPI *D3D11DeviceContext_UpdateSubresource_t)(
    ID3D11DeviceContext *pContext,
    ID3D11Resource *pDstResource,
    UINT DstSubresource,
    const void *pDstBox,
    const void *pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch
);

typedef HRESULT (WINAPI *D3D11DeviceContext_QueryInterface_t)(
    ID3D11DeviceContext *pContext,
    REFIID riid,
    void **ppvObject
);

/******************************************************************************
 * Additional Function Pointer Types - D3D12 Device Methods
 ******************************************************************************/

typedef HRESULT (WINAPI *D3D12Device_CreateCommittedResource_t)(
    ID3D12Device *pDevice,
    const D3D12_HEAP_PROPERTIES *pHeapProperties,
    UINT HeapFlags,
    const D3D12_RESOURCE_DESC *pResourceDesc,
    UINT InitialResourceState,
    const D3D12_CLEAR_VALUE *pOptimizedClearValue,
    REFIID riid,
    void **ppvResource
);

typedef HRESULT (WINAPI *D3D12Device_CreateDescriptorHeap_t)(
    ID3D12Device *pDevice,
    const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc,
    REFIID riid,
    void **ppvHeap
);

/******************************************************************************
 * Additional Function Pointer Types - D3D12 Graphics Command List Methods
 ******************************************************************************/

typedef HRESULT (WINAPI *D3D12GraphicsCommandList_Reset_t)(
    ID3D12GraphicsCommandList *pCommandList,
    ID3D12CommandAllocator *pAllocator,
    ID3D12PipelineState *pInitialState
);

typedef HRESULT (WINAPI *D3D12GraphicsCommandList_Close_t)(
    ID3D12GraphicsCommandList *pCommandList
);

typedef HRESULT (WINAPI *D3D12GraphicsCommandList_DrawInstanced_t)(
    ID3D12GraphicsCommandList *pCommandList,
    UINT VertexCountPerInstance,
    UINT InstanceCount,
    UINT StartVertexLocation,
    UINT StartInstanceLocation
);

typedef HRESULT (WINAPI *D3D12GraphicsCommandList_DrawIndexedInstanced_t)(
    ID3D12GraphicsCommandList *pCommandList,
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation
);

typedef HRESULT (WINAPI *D3D12GraphicsCommandList_OMSetRenderTargets_t)(
    ID3D12GraphicsCommandList *pCommandList,
    UINT NumRenderTargetDescriptors,
    const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
    BOOL RTsSingleHandleToDescriptorRange,
    const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor
);

typedef HRESULT (WINAPI *D3D12GraphicsCommandList_ClearRenderTargetView_t)(
    ID3D12GraphicsCommandList *pCommandList,
    D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
    const float ColorRGBA[4],
    UINT NumRects,
    const D3D12_RECT *pRects
);

/******************************************************************************
 * Additional Function Pointer Types - D3D12 Command Queue Methods
 ******************************************************************************/

typedef HRESULT (WINAPI *D3D12CommandQueue_ExecuteCommandLists_t)(
    ID3D12CommandQueue *pCommandQueue,
    UINT NumCommandLists,
    ID3D12CommandList *const *ppCommandLists
);

typedef HRESULT (WINAPI *D3D12CommandQueue_Signal_t)(
    ID3D12CommandQueue *pCommandQueue,
    ID3D12Fence *pFence,
    UINT64 Value
);

/******************************************************************************
 * Additional Function Pointer Types - DXGI Methods
 ******************************************************************************/

typedef HRESULT (WINAPI *CreateDXGIFactory2_t)(
    UINT Flags,
    REFIID riid,
    void **ppFactory
);

typedef HRESULT (WINAPI *DXGIFactory_CreateSwapChain_t)(
    IDXGIFactory *pFactory,
    IUnknown *pDevice,
    DXGI_SWAP_CHAIN_DESC *pDesc,
    IDXGISwapChain **ppSwapChain
);

typedef HRESULT (WINAPI *DXGISwapChain_GetBuffer_t)(
    IDXGISwapChain *pSwapChain,
    UINT Buffer,
    REFIID riid,
    void **ppSurface
);

typedef HRESULT (WINAPI *DXGISwapChain_Present_t)(
    IDXGISwapChain *pSwapChain,
    UINT SyncInterval,
    UINT Flags
);

/******************************************************************************
 * Configuration and State
 ******************************************************************************/

#define MVGAL_D3D_VERSION "0.2.0"

// State structure
typedef struct {
    bool enabled;
    bool debug;
    int gpu_count;
    int current_gpu;
    char strategy[64];
    pthread_mutex_t lock;
    mvgal_context_t context;  // MVGAL context for workload submission
} d3d_wrapper_state_t;

static d3d_wrapper_state_t wrapper_state = {
    .enabled = true,
    .debug = false,
    .gpu_count = 0,
    .current_gpu = 0,
    .strategy = "round_robin",
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .context = NULL
};

// Real function pointers
static D3D11CreateDevice_t real_D3D11CreateDevice = NULL;
static D3D11CreateDeviceContextState_t real_D3D11CreateDeviceContextState = NULL;
static D3D11CreateBuffer_t real_D3D11CreateBuffer = NULL;
static CreateDXGIFactory1_t real_CreateDXGIFactory1 = NULL;
static DXGIFactory_EnumAdapters1_t real_DXGIFactory_EnumAdapters1 = NULL;
static D3D12CreateDevice_t real_D3D12CreateDevice = NULL;
static D3D12Device_CreateCommandQueue_t real_D3D12Device_CreateCommandQueue = NULL;
static D3D12Device_CreateGraphicsPipelineState_t real_D3D12Device_CreateGraphicsPipelineState = NULL;

// Additional static function pointers - D3D11 Device
static D3D11Device_CreateBuffer_t real_D3D11Device_CreateBuffer = NULL;
static D3D11Device_CreateTexture2D_t real_D3D11Device_CreateTexture2D = NULL;
static D3D11Device_CreateShaderResourceView_t real_D3D11Device_CreateShaderResourceView = NULL;
static D3D11Device_CreateRenderTargetView_t real_D3D11Device_CreateRenderTargetView = NULL;

// Additional static function pointers - D3D11 Device Context
static D3D11DeviceContext_Draw_t real_D3D11DeviceContext_Draw = NULL;
static D3D11DeviceContext_DrawIndexed_t real_D3D11DeviceContext_DrawIndexed = NULL;
static D3D11DeviceContext_OMSetRenderTargets_t real_D3D11DeviceContext_OMSetRenderTargets = NULL;
static D3D11DeviceContext_ClearRenderTargetView_t real_D3D11DeviceContext_ClearRenderTargetView = NULL;
static D3D11DeviceContext_UpdateSubresource_t real_D3D11DeviceContext_UpdateSubresource = NULL;
static D3D11DeviceContext_QueryInterface_t real_D3D11DeviceContext_QueryInterface = NULL;

// Additional static function pointers - D3D12 Device
static D3D12Device_CreateCommittedResource_t real_D3D12Device_CreateCommittedResource = NULL;
static D3D12Device_CreateDescriptorHeap_t real_D3D12Device_CreateDescriptorHeap = NULL;

// Additional static function pointers - D3D12 Graphics Command List
static D3D12GraphicsCommandList_Reset_t real_D3D12GraphicsCommandList_Reset = NULL;
static D3D12GraphicsCommandList_Close_t real_D3D12GraphicsCommandList_Close = NULL;
static D3D12GraphicsCommandList_DrawInstanced_t real_D3D12GraphicsCommandList_DrawInstanced = NULL;
static D3D12GraphicsCommandList_DrawIndexedInstanced_t real_D3D12GraphicsCommandList_DrawIndexedInstanced = NULL;
static D3D12GraphicsCommandList_OMSetRenderTargets_t real_D3D12GraphicsCommandList_OMSetRenderTargets = NULL;
static D3D12GraphicsCommandList_ClearRenderTargetView_t real_D3D12GraphicsCommandList_ClearRenderTargetView = NULL;

// Additional static function pointers - D3D12 Command Queue
static D3D12CommandQueue_ExecuteCommandLists_t real_D3D12CommandQueue_ExecuteCommandLists = NULL;
static D3D12CommandQueue_Signal_t real_D3D12CommandQueue_Signal = NULL;

// Additional static function pointers - DXGI
static CreateDXGIFactory2_t real_CreateDXGIFactory2 = NULL;
static DXGIFactory_CreateSwapChain_t real_DXGIFactory_CreateSwapChain = NULL;
static DXGISwapChain_GetBuffer_t real_DXGISwapChain_GetBuffer = NULL;
static DXGISwapChain_Present_t real_DXGISwapChain_Present = NULL;

/******************************************************************************
 * Device Tracking
 ******************************************************************************/

#define MAX_DEVICES 8
#define MAX_ADAPTERS 8

typedef struct {
    void *real_device;
    int mvgal_gpu_id;
    bool in_use;
} device_mapping_t;

static device_mapping_t device_map[MAX_DEVICES] = {0};
static int device_count = 0;

static int get_next_gpu(void) {
    pthread_mutex_lock(&wrapper_state.lock);
    int gpu = wrapper_state.current_gpu;
    wrapper_state.current_gpu = (wrapper_state.current_gpu + 1) % wrapper_state.gpu_count;
    pthread_mutex_unlock(&wrapper_state.lock);
    return gpu;
}

static int register_device(void *real_device, int mvgal_gpu_id) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!device_map[i].in_use) {
            device_map[i].real_device = real_device;
            device_map[i].mvgal_gpu_id = mvgal_gpu_id;
            device_map[i].in_use = true;
            device_count++;
            pthread_mutex_unlock(&wrapper_state.lock);
            return i;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    return -1;
}

static void unregister_device(void *real_device) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (device_map[i].in_use && device_map[i].real_device == real_device) {
            device_map[i].in_use = false;
            device_map[i].real_device = NULL;
            device_count--;
            break;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
}

static int get_gpu_for_device(void *real_device) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (device_map[i].in_use && device_map[i].real_device == real_device) {
            int gpu_id = device_map[i].mvgal_gpu_id;
            pthread_mutex_unlock(&wrapper_state.lock);
            return gpu_id;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    return get_next_gpu(); // fallback to round-robin
}

static void log_debug(const char *format, ...) {
    if (!wrapper_state.debug) return;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[D3D DEBUG] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[D3D INFO] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[D3D WARN] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void* get_real_function(const char *name) {
    void *func = dlsym(RTLD_NEXT, name);
    if (!func) {
        log_warn("Could not find real function: %s", name);
    }
    return func;
}

/******************************************************************************
 * MVGAL Workload Submission
 ******************************************************************************/

static void submit_d3d_workload(const char *step_name, mvgal_workload_type_t type, void *device) {
    if (!wrapper_state.context) return;
    
    int gpu_id = device ? get_gpu_for_device(device) : get_next_gpu();
    
    mvgal_workload_submit_info_t info = {
        .type = type,
        .priority = 50,
        .gpu_mask = (1 << gpu_id),  // Target specific GPU
        .user_data = NULL
    };
    
    mvgal_workload_t workload;
    mvgal_error_t err = mvgal_workload_submit(wrapper_state.context, &info, &workload);
    if (err != MVGAL_SUCCESS) {
        log_warn("Failed to submit D3D workload: %s (GPU %d)", step_name, gpu_id);
    } else {
        log_debug("Submitted D3D workload: %s to GPU %d", step_name, gpu_id);
    }
}

/******************************************************************************
 * Initialization
 ******************************************************************************/

static void init_wrapper(void) {
    const char *enabled_str = getenv("MVGAL_D3D_ENABLED");
    if (enabled_str && atoi(enabled_str) == 0) {
        wrapper_state.enabled = false;
        log_info("D3D interception disabled via MVGAL_D3D_ENABLED=0");
        return;
    }

    const char *debug_str = getenv("MVGAL_D3D_DEBUG");
    if (debug_str && atoi(debug_str) == 1) {
        wrapper_state.debug = true;
    }

    const char *strategy = getenv("MVGAL_D3D_STRATEGY");
    if (strategy) {
        strncpy(wrapper_state.strategy, strategy, sizeof(wrapper_state.strategy) - 1);
    }

    // Initialize MVGAL
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        log_warn("Failed to initialize MVGAL: %d", err);
        return;
    }

    err = mvgal_context_create(&wrapper_state.context);
    if (err != MVGAL_SUCCESS) {
        log_warn("Failed to create MVGAL context: %d", err);
        wrapper_state.context = NULL;
        return;
    }

    // Set strategy if specified
    if (strcmp(wrapper_state.strategy, "round_robin") == 0) {
        mvgal_scheduler_set_strategy(wrapper_state.context, MVGAL_STRATEGY_ROUND_ROBIN);
    } else if (strcmp(wrapper_state.strategy, "hybrid") == 0) {
        mvgal_scheduler_set_strategy(wrapper_state.context, MVGAL_STRATEGY_HYBRID);
    }

    // Get GPU count
    wrapper_state.gpu_count = mvgal_gpu_get_count();
    if (wrapper_state.gpu_count <= 0) {
        wrapper_state.gpu_count = 1;
    }

    log_info("D3D wrapper initialized (strategy: %s, GPUs: %d)",
             wrapper_state.strategy, wrapper_state.gpu_count);
}

static void fini_wrapper(void) {
    if (wrapper_state.context) {
        mvgal_context_destroy(wrapper_state.context);
        wrapper_state.context = NULL;
        mvgal_shutdown();
        log_info("D3D wrapper shutdown");
    }
}

// Constructor/destructor attributes
__attribute__((constructor)) static void d3d_constructor(void) {
    init_wrapper();
}

__attribute__((destructor)) static void d3d_destructor(void) {
    fini_wrapper();
}

/******************************************************************************
 * D3D11 Function Intercepts
 ******************************************************************************/

// D3D11CreateDevice
HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter *pAdapter,
    D3D_FEATURE_LEVEL DriverTypes,
    void *Software,
    UINT Flags,
    const DWORD *pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel,
    ID3D11DeviceContext **ppImmediateContext
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11CreateDevice) {
            real_D3D11CreateDevice = get_real_function("D3D11CreateDevice");
        }
        if (real_D3D11CreateDevice) {
            return real_D3D11CreateDevice(pAdapter, DriverTypes, Software, 
                                         Flags, pFeatureLevels, FeatureLevels, 
                                         SDKVersion, ppDevice, pFeatureLevel, 
                                         ppImmediateContext);
        }
    return E_FAIL;
}

/******************************************************************************
 * Additional D3D11 Device Method Intercepts
 ******************************************************************************/

// D3D11Device_CreateTexture2D
HRESULT WINAPI D3D11Device_CreateTexture2D(
    ID3D11Device *pDevice,
    const D3D11_TEXTURE2D_DESC *pDesc,
    const void *pInitialData,
    ID3D11Texture2D **ppTexture2D
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11Device_CreateTexture2D) {
            real_D3D11Device_CreateTexture2D = get_real_function("D3D11Device_CreateTexture2D");
        }
        if (real_D3D11Device_CreateTexture2D) {
            return real_D3D11Device_CreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
        }
        return E_FAIL;
    }

    log_debug("D3D11Device_CreateTexture2D intercepted (%ux%u)",
              pDesc ? pDesc->Width : 0, pDesc ? pDesc->Height : 0);

    submit_d3d_workload("CreateTexture2D", MVGAL_WORKLOAD_D3D_TEXTURE, pDevice);

    if (!real_D3D11Device_CreateTexture2D) {
        real_D3D11Device_CreateTexture2D = get_real_function("D3D11Device_CreateTexture2D");
    }
    if (real_D3D11Device_CreateTexture2D) {
        return real_D3D11Device_CreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
    }

    return E_FAIL;
}

// D3D11Device_CreateShaderResourceView
HRESULT WINAPI D3D11Device_CreateShaderResourceView(
    ID3D11Device *pDevice,
    ID3D11Resource *pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,
    ID3D11ShaderResourceView **ppSRView
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11Device_CreateShaderResourceView) {
            real_D3D11Device_CreateShaderResourceView = get_real_function("D3D11Device_CreateShaderResourceView");
        }
        if (real_D3D11Device_CreateShaderResourceView) {
            return real_D3D11Device_CreateShaderResourceView(pDevice, pResource, pDesc, ppSRView);
        }
        return E_FAIL;
    }

    log_debug("D3D11Device_CreateShaderResourceView intercepted");

    submit_d3d_workload("CreateShaderResourceView", MVGAL_WORKLOAD_D3D_TEXTURE, pDevice);

    if (!real_D3D11Device_CreateShaderResourceView) {
        real_D3D11Device_CreateShaderResourceView = get_real_function("D3D11Device_CreateShaderResourceView");
    }
    if (real_D3D11Device_CreateShaderResourceView) {
        return real_D3D11Device_CreateShaderResourceView(pDevice, pResource, pDesc, ppSRView);
    }

    return E_FAIL;
}

// D3D11Device_CreateRenderTargetView
HRESULT WINAPI D3D11Device_CreateRenderTargetView(
    ID3D11Device *pDevice,
    ID3D11Resource *pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
    ID3D11RenderTargetView **ppRTView
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11Device_CreateRenderTargetView) {
            real_D3D11Device_CreateRenderTargetView = get_real_function("D3D11Device_CreateRenderTargetView");
        }
        if (real_D3D11Device_CreateRenderTargetView) {
            return real_D3D11Device_CreateRenderTargetView(pDevice, pResource, pDesc, ppRTView);
        }
        return E_FAIL;
    }

    log_debug("D3D11Device_CreateRenderTargetView intercepted");

    submit_d3d_workload("CreateRenderTargetView", MVGAL_WORKLOAD_D3D_TEXTURE, pDevice);

    if (!real_D3D11Device_CreateRenderTargetView) {
        real_D3D11Device_CreateRenderTargetView = get_real_function("D3D11Device_CreateRenderTargetView");
    }
    if (real_D3D11Device_CreateRenderTargetView) {
        return real_D3D11Device_CreateRenderTargetView(pDevice, pResource, pDesc, ppRTView);
    }

    return E_FAIL;
}

/******************************************************************************
 * D3D11 Device Context Method Intercepts
 ******************************************************************************/

// D3D11DeviceContext_Draw
HRESULT WINAPI D3D11DeviceContext_Draw(
    ID3D11DeviceContext *pContext,
    UINT VertexCount,
    UINT StartVertexLocation
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11DeviceContext_Draw) {
            real_D3D11DeviceContext_Draw = get_real_function("D3D11DeviceContext_Draw");
        }
        if (real_D3D11DeviceContext_Draw) {
            return real_D3D11DeviceContext_Draw(pContext, VertexCount, StartVertexLocation);
        }
        return E_FAIL;
    }

    log_debug("D3D11DeviceContext_Draw intercepted (%u vertices)", VertexCount);

    submit_d3d_workload("Draw", MVGAL_WORKLOAD_D3D_CONTEXT, pContext);

    if (!real_D3D11DeviceContext_Draw) {
        real_D3D11DeviceContext_Draw = get_real_function("D3D11DeviceContext_Draw");
    }
    if (real_D3D11DeviceContext_Draw) {
        return real_D3D11DeviceContext_Draw(pContext, VertexCount, StartVertexLocation);
    }

    return E_FAIL;
}

// D3D11DeviceContext_DrawIndexed
HRESULT WINAPI D3D11DeviceContext_DrawIndexed(
    ID3D11DeviceContext *pContext,
    UINT IndexCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11DeviceContext_DrawIndexed) {
            real_D3D11DeviceContext_DrawIndexed = get_real_function("D3D11DeviceContext_DrawIndexed");
        }
        if (real_D3D11DeviceContext_DrawIndexed) {
            return real_D3D11DeviceContext_DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
        }
        return E_FAIL;
    }

    log_debug("D3D11DeviceContext_DrawIndexed intercepted (%u indices)", IndexCount);

    submit_d3d_workload("DrawIndexed", MVGAL_WORKLOAD_D3D_CONTEXT, pContext);

    if (!real_D3D11DeviceContext_DrawIndexed) {
        real_D3D11DeviceContext_DrawIndexed = get_real_function("D3D11DeviceContext_DrawIndexed");
    }
    if (real_D3D11DeviceContext_DrawIndexed) {
        return real_D3D11DeviceContext_DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
    }

    return E_FAIL;
}

// D3D11DeviceContext_OMSetRenderTargets
HRESULT WINAPI D3D11DeviceContext_OMSetRenderTargets(
    ID3D11DeviceContext *pContext,
    UINT NumViews,
    ID3D11RenderTargetView *const *ppRenderTargetViews,
    ID3D11DepthStencilView *pDepthStencilView
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11DeviceContext_OMSetRenderTargets) {
            real_D3D11DeviceContext_OMSetRenderTargets = get_real_function("D3D11DeviceContext_OMSetRenderTargets");
        }
        if (real_D3D11DeviceContext_OMSetRenderTargets) {
            return real_D3D11DeviceContext_OMSetRenderTargets(pContext, NumViews, ppRenderTargetViews, pDepthStencilView);
        }
        return E_FAIL;
    }

    log_debug("D3D11DeviceContext_OMSetRenderTargets intercepted (%u views)", NumViews);

    submit_d3d_workload("OMSetRenderTargets", MVGAL_WORKLOAD_D3D_TEXTURE, pContext);

    if (!real_D3D11DeviceContext_OMSetRenderTargets) {
        real_D3D11DeviceContext_OMSetRenderTargets = get_real_function("D3D11DeviceContext_OMSetRenderTargets");
    }
    if (real_D3D11DeviceContext_OMSetRenderTargets) {
        return real_D3D11DeviceContext_OMSetRenderTargets(pContext, NumViews, ppRenderTargetViews, pDepthStencilView);
    }

    return E_FAIL;
}

// D3D11DeviceContext_ClearRenderTargetView
HRESULT WINAPI D3D11DeviceContext_ClearRenderTargetView(
    ID3D11DeviceContext *pContext,
    ID3D11RenderTargetView *pRenderTargetView,
    const float ColorRGBA[4]
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11DeviceContext_ClearRenderTargetView) {
            real_D3D11DeviceContext_ClearRenderTargetView = get_real_function("D3D11DeviceContext_ClearRenderTargetView");
        }
        if (real_D3D11DeviceContext_ClearRenderTargetView) {
            return real_D3D11DeviceContext_ClearRenderTargetView(pContext, pRenderTargetView, ColorRGBA);
        }
        return E_FAIL;
    }

    log_debug("D3D11DeviceContext_ClearRenderTargetView intercepted");

    submit_d3d_workload("ClearRenderTargetView", MVGAL_WORKLOAD_D3D_CONTEXT, pContext);

    if (!real_D3D11DeviceContext_ClearRenderTargetView) {
        real_D3D11DeviceContext_ClearRenderTargetView = get_real_function("D3D11DeviceContext_ClearRenderTargetView");
    }
    if (real_D3D11DeviceContext_ClearRenderTargetView) {
        return real_D3D11DeviceContext_ClearRenderTargetView(pContext, pRenderTargetView, ColorRGBA);
    }

    return E_FAIL;
}

// D3D11DeviceContext_UpdateSubresource
HRESULT WINAPI D3D11DeviceContext_UpdateSubresource(
    ID3D11DeviceContext *pContext,
    ID3D11Resource *pDstResource,
    UINT DstSubresource,
    const void *pDstBox,
    const void *pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11DeviceContext_UpdateSubresource) {
            real_D3D11DeviceContext_UpdateSubresource = get_real_function("D3D11DeviceContext_UpdateSubresource");
        }
        if (real_D3D11DeviceContext_UpdateSubresource) {
            return real_D3D11DeviceContext_UpdateSubresource(pContext, pDstResource, DstSubresource,
                                                           pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
        }
        return E_FAIL;
    }

    log_debug("D3D11DeviceContext_UpdateSubresource intercepted (subresource: %u)", DstSubresource);

    submit_d3d_workload("UpdateSubresource", MVGAL_WORKLOAD_D3D_CONTEXT, pContext);

    if (!real_D3D11DeviceContext_UpdateSubresource) {
        real_D3D11DeviceContext_UpdateSubresource = get_real_function("D3D11DeviceContext_UpdateSubresource");
    }
    if (real_D3D11DeviceContext_UpdateSubresource) {
        return real_D3D11DeviceContext_UpdateSubresource(pContext, pDstResource, DstSubresource,
                                                        pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
    }

    return E_FAIL;
}

// D3D11DeviceContext_QueryInterface
HRESULT WINAPI D3D11DeviceContext_QueryInterface(
    ID3D11DeviceContext *pContext,
    REFIID riid,
    void **ppvObject
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11DeviceContext_QueryInterface) {
            real_D3D11DeviceContext_QueryInterface = get_real_function("D3D11DeviceContext_QueryInterface");
        }
        if (real_D3D11DeviceContext_QueryInterface) {
            return real_D3D11DeviceContext_QueryInterface(pContext, riid, ppvObject);
        }
        return E_FAIL;
    }

    log_debug("D3D11DeviceContext_QueryInterface intercepted");

    submit_d3d_workload("QueryInterface", MVGAL_WORKLOAD_D3D_CONTEXT, pContext);

    if (!real_D3D11DeviceContext_QueryInterface) {
        real_D3D11DeviceContext_QueryInterface = get_real_function("D3D11DeviceContext_QueryInterface");
    }
    if (real_D3D11DeviceContext_QueryInterface) {
        return real_D3D11DeviceContext_QueryInterface(pContext, riid, ppvObject);
    }

    return E_FAIL;
}

/******************************************************************************
 * Additional D3D12 Device Method Intercepts
 ******************************************************************************/

// D3D12Device_CreateCommittedResource
HRESULT WINAPI D3D12Device_CreateCommittedResource(
    ID3D12Device *pDevice,
    const D3D12_HEAP_PROPERTIES *pHeapProperties,
    UINT HeapFlags,
    const D3D12_RESOURCE_DESC *pResourceDesc,
    UINT InitialResourceState,
    const D3D12_CLEAR_VALUE *pOptimizedClearValue,
    REFIID riid,
    void **ppvResource
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12Device_CreateCommittedResource) {
            real_D3D12Device_CreateCommittedResource = get_real_function("D3D12Device_CreateCommittedResource");
        }
        if (real_D3D12Device_CreateCommittedResource) {
            return real_D3D12Device_CreateCommittedResource(pDevice, pHeapProperties, HeapFlags,
                                                           pResourceDesc, InitialResourceState,
                                                           pOptimizedClearValue, riid, ppvResource);
        }
        return E_FAIL;
    }

    log_debug("D3D12Device_CreateCommittedResource intercepted (%ux%u)",
              pResourceDesc ? pResourceDesc->Width : 0, pResourceDesc ? pResourceDesc->Height : 0);

    submit_d3d_workload("CreateCommittedResource", MVGAL_WORKLOAD_D3D_TEXTURE, pDevice);

    if (!real_D3D12Device_CreateCommittedResource) {
        real_D3D12Device_CreateCommittedResource = get_real_function("D3D12Device_CreateCommittedResource");
    }
    if (real_D3D12Device_CreateCommittedResource) {
        HRESULT hr = real_D3D12Device_CreateCommittedResource(pDevice, pHeapProperties, HeapFlags,
                                                              pResourceDesc, InitialResourceState,
                                                              pOptimizedClearValue, riid, ppvResource);
        if (SUCCEEDED(hr) && ppvResource && *ppvResource) {
            int gpu_id = get_next_gpu();
            int idx = register_device(*ppvResource, gpu_id);
            if (idx >= 0) {
                log_debug("Registered D3D12 resource %p to MVGAL GPU %d", *ppvResource, gpu_id);
            }
        }
        return hr;
    }

    return E_FAIL;
}

// D3D12Device_CreateDescriptorHeap
HRESULT WINAPI D3D12Device_CreateDescriptorHeap(
    ID3D12Device *pDevice,
    const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc,
    REFIID riid,
    void **ppvHeap
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12Device_CreateDescriptorHeap) {
            real_D3D12Device_CreateDescriptorHeap = get_real_function("D3D12Device_CreateDescriptorHeap");
        }
        if (real_D3D12Device_CreateDescriptorHeap) {
            return real_D3D12Device_CreateDescriptorHeap(pDevice, pDescriptorHeapDesc, riid, ppvHeap);
        }
        return E_FAIL;
    }

    log_debug("D3D12Device_CreateDescriptorHeap intercepted (%u descriptors)",
              pDescriptorHeapDesc ? pDescriptorHeapDesc->NumDescriptors : 0);

    submit_d3d_workload("CreateDescriptorHeap", MVGAL_WORKLOAD_D3D_BUFFER, pDevice);

    if (!real_D3D12Device_CreateDescriptorHeap) {
        real_D3D12Device_CreateDescriptorHeap = get_real_function("D3D12Device_CreateDescriptorHeap");
    }
    if (real_D3D12Device_CreateDescriptorHeap) {
        return real_D3D12Device_CreateDescriptorHeap(pDevice, pDescriptorHeapDesc, riid, ppvHeap);
    }

    return E_FAIL;
}

/******************************************************************************
 * D3D12 Graphics Command List Method Intercepts
 ******************************************************************************/

// D3D12GraphicsCommandList_Reset
HRESULT WINAPI D3D12GraphicsCommandList_Reset(
    ID3D12GraphicsCommandList *pCommandList,
    ID3D12CommandAllocator *pAllocator,
    ID3D12PipelineState *pInitialState
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12GraphicsCommandList_Reset) {
            real_D3D12GraphicsCommandList_Reset = get_real_function("D3D12GraphicsCommandList_Reset");
        }
        if (real_D3D12GraphicsCommandList_Reset) {
            return real_D3D12GraphicsCommandList_Reset(pCommandList, pAllocator, pInitialState);
        }
        return E_FAIL;
    }

    log_debug("D3D12GraphicsCommandList_Reset intercepted");

    submit_d3d_workload("Reset", MVGAL_WORKLOAD_D3D_CONTEXT, pCommandList);

    if (!real_D3D12GraphicsCommandList_Reset) {
        real_D3D12GraphicsCommandList_Reset = get_real_function("D3D12GraphicsCommandList_Reset");
    }
    if (real_D3D12GraphicsCommandList_Reset) {
        return real_D3D12GraphicsCommandList_Reset(pCommandList, pAllocator, pInitialState);
    }

    return E_FAIL;
}

// D3D12GraphicsCommandList_Close
HRESULT WINAPI D3D12GraphicsCommandList_Close(
    ID3D12GraphicsCommandList *pCommandList
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12GraphicsCommandList_Close) {
            real_D3D12GraphicsCommandList_Close = get_real_function("D3D12GraphicsCommandList_Close");
        }
        if (real_D3D12GraphicsCommandList_Close) {
            return real_D3D12GraphicsCommandList_Close(pCommandList);
        }
        return E_FAIL;
    }

    log_debug("D3D12GraphicsCommandList_Close intercepted");

    submit_d3d_workload("Close", MVGAL_WORKLOAD_D3D_CONTEXT, pCommandList);

    if (!real_D3D12GraphicsCommandList_Close) {
        real_D3D12GraphicsCommandList_Close = get_real_function("D3D12GraphicsCommandList_Close");
    }
    if (real_D3D12GraphicsCommandList_Close) {
        return real_D3D12GraphicsCommandList_Close(pCommandList);
    }

    return E_FAIL;
}

// D3D12GraphicsCommandList_DrawInstanced
HRESULT WINAPI D3D12GraphicsCommandList_DrawInstanced(
    ID3D12GraphicsCommandList *pCommandList,
    UINT VertexCountPerInstance,
    UINT InstanceCount,
    UINT StartVertexLocation,
    UINT StartInstanceLocation
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12GraphicsCommandList_DrawInstanced) {
            real_D3D12GraphicsCommandList_DrawInstanced = get_real_function("D3D12GraphicsCommandList_DrawInstanced");
        }
        if (real_D3D12GraphicsCommandList_DrawInstanced) {
            return real_D3D12GraphicsCommandList_DrawInstanced(pCommandList, VertexCountPerInstance,
                                                              InstanceCount, StartVertexLocation,
                                                              StartInstanceLocation);
        }
        return E_FAIL;
    }

    log_debug("D3D12GraphicsCommandList_DrawInstanced intercepted (%u vertices, %u instances)",
              VertexCountPerInstance, InstanceCount);

    submit_d3d_workload("DrawInstanced", MVGAL_WORKLOAD_D3D_QUEUE, pCommandList);

    if (!real_D3D12GraphicsCommandList_DrawInstanced) {
        real_D3D12GraphicsCommandList_DrawInstanced = get_real_function("D3D12GraphicsCommandList_DrawInstanced");
    }
    if (real_D3D12GraphicsCommandList_DrawInstanced) {
        return real_D3D12GraphicsCommandList_DrawInstanced(pCommandList, VertexCountPerInstance,
                                                           InstanceCount, StartVertexLocation,
                                                           StartInstanceLocation);
    }

    return E_FAIL;
}

// D3D12GraphicsCommandList_DrawIndexedInstanced
HRESULT WINAPI D3D12GraphicsCommandList_DrawIndexedInstanced(
    ID3D12GraphicsCommandList *pCommandList,
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12GraphicsCommandList_DrawIndexedInstanced) {
            real_D3D12GraphicsCommandList_DrawIndexedInstanced = get_real_function("D3D12GraphicsCommandList_DrawIndexedInstanced");
        }
        if (real_D3D12GraphicsCommandList_DrawIndexedInstanced) {
            return real_D3D12GraphicsCommandList_DrawIndexedInstanced(pCommandList, IndexCountPerInstance,
                                                                    InstanceCount, StartIndexLocation,
                                                                    BaseVertexLocation, StartInstanceLocation);
        }
        return E_FAIL;
    }

    log_debug("D3D12GraphicsCommandList_DrawIndexedInstanced intercepted (%u indices, %u instances)",
              IndexCountPerInstance, InstanceCount);

    submit_d3d_workload("DrawIndexedInstanced", MVGAL_WORKLOAD_D3D_QUEUE, pCommandList);

    if (!real_D3D12GraphicsCommandList_DrawIndexedInstanced) {
        real_D3D12GraphicsCommandList_DrawIndexedInstanced = get_real_function("D3D12GraphicsCommandList_DrawIndexedInstanced");
    }
    if (real_D3D12GraphicsCommandList_DrawIndexedInstanced) {
        return real_D3D12GraphicsCommandList_DrawIndexedInstanced(pCommandList, IndexCountPerInstance,
                                                                 InstanceCount, StartIndexLocation,
                                                                 BaseVertexLocation, StartInstanceLocation);
    }

    return E_FAIL;
}

// D3D12GraphicsCommandList_OMSetRenderTargets
HRESULT WINAPI D3D12GraphicsCommandList_OMSetRenderTargets(
    ID3D12GraphicsCommandList *pCommandList,
    UINT NumRenderTargetDescriptors,
    const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
    BOOL RTsSingleHandleToDescriptorRange,
    const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12GraphicsCommandList_OMSetRenderTargets) {
            real_D3D12GraphicsCommandList_OMSetRenderTargets = get_real_function("D3D12GraphicsCommandList_OMSetRenderTargets");
        }
        if (real_D3D12GraphicsCommandList_OMSetRenderTargets) {
            return real_D3D12GraphicsCommandList_OMSetRenderTargets(pCommandList, NumRenderTargetDescriptors,
                                                                  pRenderTargetDescriptors,
                                                                  RTsSingleHandleToDescriptorRange,
                                                                  pDepthStencilDescriptor);
        }
        return E_FAIL;
    }

    log_debug("D3D12GraphicsCommandList_OMSetRenderTargets intercepted (%u targets)", NumRenderTargetDescriptors);

    submit_d3d_workload("OMSetRenderTargets", MVGAL_WORKLOAD_D3D_TEXTURE, pCommandList);

    if (!real_D3D12GraphicsCommandList_OMSetRenderTargets) {
        real_D3D12GraphicsCommandList_OMSetRenderTargets = get_real_function("D3D12GraphicsCommandList_OMSetRenderTargets");
    }
    if (real_D3D12GraphicsCommandList_OMSetRenderTargets) {
        return real_D3D12GraphicsCommandList_OMSetRenderTargets(pCommandList, NumRenderTargetDescriptors,
                                                               pRenderTargetDescriptors,
                                                               RTsSingleHandleToDescriptorRange,
                                                               pDepthStencilDescriptor);
    }

    return E_FAIL;
}

// D3D12GraphicsCommandList_ClearRenderTargetView
HRESULT WINAPI D3D12GraphicsCommandList_ClearRenderTargetView(
    ID3D12GraphicsCommandList *pCommandList,
    D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
    const float ColorRGBA[4],
    UINT NumRects,
    const D3D12_RECT *pRects
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12GraphicsCommandList_ClearRenderTargetView) {
            real_D3D12GraphicsCommandList_ClearRenderTargetView = get_real_function("D3D12GraphicsCommandList_ClearRenderTargetView");
        }
        if (real_D3D12GraphicsCommandList_ClearRenderTargetView) {
            return real_D3D12GraphicsCommandList_ClearRenderTargetView(pCommandList, RenderTargetView,
                                                                     ColorRGBA, NumRects, pRects);
        }
        return E_FAIL;
    }

    log_debug("D3D12GraphicsCommandList_ClearRenderTargetView intercepted");

    submit_d3d_workload("ClearRenderTargetView", MVGAL_WORKLOAD_D3D_CONTEXT, pCommandList);

    if (!real_D3D12GraphicsCommandList_ClearRenderTargetView) {
        real_D3D12GraphicsCommandList_ClearRenderTargetView = get_real_function("D3D12GraphicsCommandList_ClearRenderTargetView");
    }
    if (real_D3D12GraphicsCommandList_ClearRenderTargetView) {
        return real_D3D12GraphicsCommandList_ClearRenderTargetView(pCommandList, RenderTargetView,
                                                                  ColorRGBA, NumRects, pRects);
    }

    return E_FAIL;
}

/******************************************************************************
 * D3D12 Command Queue Method Intercepts
 ******************************************************************************/

// D3D12CommandQueue_ExecuteCommandLists
HRESULT WINAPI D3D12CommandQueue_ExecuteCommandLists(
    ID3D12CommandQueue *pCommandQueue,
    UINT NumCommandLists,
    ID3D12CommandList *const *ppCommandLists
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12CommandQueue_ExecuteCommandLists) {
            real_D3D12CommandQueue_ExecuteCommandLists = get_real_function("D3D12CommandQueue_ExecuteCommandLists");
        }
        if (real_D3D12CommandQueue_ExecuteCommandLists) {
            return real_D3D12CommandQueue_ExecuteCommandLists(pCommandQueue, NumCommandLists, ppCommandLists);
        }
        return E_FAIL;
    }

    log_debug("D3D12CommandQueue_ExecuteCommandLists intercepted (%u lists)", NumCommandLists);

    submit_d3d_workload("ExecuteCommandLists", MVGAL_WORKLOAD_D3D_QUEUE, pCommandQueue);

    if (!real_D3D12CommandQueue_ExecuteCommandLists) {
        real_D3D12CommandQueue_ExecuteCommandLists = get_real_function("D3D12CommandQueue_ExecuteCommandLists");
    }
    if (real_D3D12CommandQueue_ExecuteCommandLists) {
        return real_D3D12CommandQueue_ExecuteCommandLists(pCommandQueue, NumCommandLists, ppCommandLists);
    }

    return E_FAIL;
}

// D3D12CommandQueue_Signal
HRESULT WINAPI D3D12CommandQueue_Signal(
    ID3D12CommandQueue *pCommandQueue,
    ID3D12Fence *pFence,
    UINT64 Value
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12CommandQueue_Signal) {
            real_D3D12CommandQueue_Signal = get_real_function("D3D12CommandQueue_Signal");
        }
        if (real_D3D12CommandQueue_Signal) {
            return real_D3D12CommandQueue_Signal(pCommandQueue, pFence, Value);
        }
        return E_FAIL;
    }

    log_debug("D3D12CommandQueue_Signal intercepted (value: %lu)", Value);

    submit_d3d_workload("Signal", MVGAL_WORKLOAD_D3D_QUEUE, pCommandQueue);

    if (!real_D3D12CommandQueue_Signal) {
        real_D3D12CommandQueue_Signal = get_real_function("D3D12CommandQueue_Signal");
    }
    if (real_D3D12CommandQueue_Signal) {
        return real_D3D12CommandQueue_Signal(pCommandQueue, pFence, Value);
    }

    return E_FAIL;
}

/******************************************************************************
 * DXGI Method Intercepts
 ******************************************************************************/

// CreateDXGIFactory2
HRESULT WINAPI CreateDXGIFactory2(
    UINT Flags,
    REFIID riid,
    void **ppFactory
) {
    if (!wrapper_state.enabled) {
        if (!real_CreateDXGIFactory2) {
            real_CreateDXGIFactory2 = get_real_function("CreateDXGIFactory2");
        }
        if (real_CreateDXGIFactory2) {
            return real_CreateDXGIFactory2(Flags, riid, ppFactory);
        }
        return E_FAIL;
    }

    log_debug("CreateDXGIFactory2 intercepted");

    submit_d3d_workload("CreateDXGIFactory2", MVGAL_WORKLOAD_D3D_CONTEXT, NULL);

    if (!real_CreateDXGIFactory2) {
        real_CreateDXGIFactory2 = get_real_function("CreateDXGIFactory2");
    }
    if (real_CreateDXGIFactory2) {
        return real_CreateDXGIFactory2(Flags, riid, ppFactory);
    }

    return E_FAIL;
}

// DXGIFactory_CreateSwapChain
HRESULT WINAPI DXGIFactory_CreateSwapChain(
    IDXGIFactory *pFactory,
    IUnknown *pDevice,
    DXGI_SWAP_CHAIN_DESC *pDesc,
    IDXGISwapChain **ppSwapChain
) {
    if (!wrapper_state.enabled) {
        if (!real_DXGIFactory_CreateSwapChain) {
            real_DXGIFactory_CreateSwapChain = get_real_function("DXGIFactory_CreateSwapChain");
        }
        if (real_DXGIFactory_CreateSwapChain) {
            return real_DXGIFactory_CreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
        }
        return E_FAIL;
    }

    log_debug("DXGIFactory_CreateSwapChain intercepted (%ux%u)",
              pDesc ? pDesc->BufferDesc.Width : 0, pDesc ? pDesc->BufferDesc.Height : 0);

    submit_d3d_workload("CreateSwapChain", MVGAL_WORKLOAD_D3D_TEXTURE, pDevice);

    if (!real_DXGIFactory_CreateSwapChain) {
        real_DXGIFactory_CreateSwapChain = get_real_function("DXGIFactory_CreateSwapChain");
    }
    if (real_DXGIFactory_CreateSwapChain) {
        HRESULT hr = real_DXGIFactory_CreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
        if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
            int gpu_id = get_next_gpu();
            int idx = register_device(*ppSwapChain, gpu_id);
            if (idx >= 0) {
                log_debug("Registered swap chain %p to MVGAL GPU %d", *ppSwapChain, gpu_id);
            }
        }
        return hr;
    }

    return E_FAIL;
}

// DXGISwapChain_GetBuffer
HRESULT WINAPI DXGISwapChain_GetBuffer(
    IDXGISwapChain *pSwapChain,
    UINT Buffer,
    REFIID riid,
    void **ppSurface
) {
    if (!wrapper_state.enabled) {
        if (!real_DXGISwapChain_GetBuffer) {
            real_DXGISwapChain_GetBuffer = get_real_function("DXGISwapChain_GetBuffer");
        }
        if (real_DXGISwapChain_GetBuffer) {
            return real_DXGISwapChain_GetBuffer(pSwapChain, Buffer, riid, ppSurface);
        }
        return E_FAIL;
    }

    log_debug("DXGISwapChain_GetBuffer intercepted (buffer: %u)", Buffer);

    submit_d3d_workload("GetBuffer", MVGAL_WORKLOAD_D3D_TEXTURE, pSwapChain);

    if (!real_DXGISwapChain_GetBuffer) {
        real_DXGISwapChain_GetBuffer = get_real_function("DXGISwapChain_GetBuffer");
    }
    if (real_DXGISwapChain_GetBuffer) {
        return real_DXGISwapChain_GetBuffer(pSwapChain, Buffer, riid, ppSurface);
    }

    return E_FAIL;
}

// DXGISwapChain_Present
HRESULT WINAPI DXGISwapChain_Present(
    IDXGISwapChain *pSwapChain,
    UINT SyncInterval,
    UINT Flags
) {
    if (!wrapper_state.enabled) {
        if (!real_DXGISwapChain_Present) {
            real_DXGISwapChain_Present = get_real_function("DXGISwapChain_Present");
        }
        if (real_DXGISwapChain_Present) {
            return real_DXGISwapChain_Present(pSwapChain, SyncInterval, Flags);
        }
        return E_FAIL;
    }

    log_debug("DXGISwapChain_Present intercepted (sync: %u)", SyncInterval);

    submit_d3d_workload("Present", MVGAL_WORKLOAD_D3D_CONTEXT, pSwapChain);

    if (!real_DXGISwapChain_Present) {
        real_DXGISwapChain_Present = get_real_function("DXGISwapChain_Present");
    }
    if (real_DXGISwapChain_Present) {
        return real_DXGISwapChain_Present(pSwapChain, SyncInterval, Flags);
    }

     return E_FAIL;
}

}

#pragma GCC diagnostic pop
