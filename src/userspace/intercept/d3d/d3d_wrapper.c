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

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static int get_next_gpu(void) {
    pthread_mutex_lock(&wrapper_state.lock);
    int gpu = wrapper_state.current_gpu;
    wrapper_state.current_gpu = (wrapper_state.current_gpu + 1) % wrapper_state.gpu_count;
    pthread_mutex_unlock(&wrapper_state.lock);
    return gpu;
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

static void submit_d3d_workload(const char *step_name, mvgal_workload_type_t type) {
    if (!wrapper_state.context) return;
    
    mvgal_workload_submit_info_t info = {
        .type = type,
        .priority = 50,
        .gpu_mask = 0xFFFFFFFF,
        .user_data = NULL
    };
    
    mvgal_workload_t workload;
    mvgal_error_t err = mvgal_workload_submit(wrapper_state.context, &info, &workload);
    if (err != MVGAL_SUCCESS) {
        log_warn("Failed to submit D3D workload: %s", step_name);
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
    
    log_debug("D3D11CreateDevice intercepted");
    
    // Submit workload to MVGAL
    submit_d3d_workload("D3D11CreateDevice", MVGAL_WORKLOAD_D3D_CONTEXT);
    
    // Call real function
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

// D3D11CreateDeviceContextState
HRESULT WINAPI D3D11CreateDeviceContextState(
    ID3D11Device *pDevice,
    const D3D11_DEVICE_CONTEXT_STATE_DESC *pDesc,
    ID3D11DeviceContext **ppDeviceContextState
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11CreateDeviceContextState) {
            real_D3D11CreateDeviceContextState = get_real_function("D3D11CreateDeviceContextState");
        }
        if (real_D3D11CreateDeviceContextState) {
            return real_D3D11CreateDeviceContextState(pDevice, pDesc, ppDeviceContextState);
        }
        return E_FAIL;
    }

    log_debug("D3D11CreateDeviceContextState intercepted");

    submit_d3d_workload("CreateDeviceContextState", MVGAL_WORKLOAD_D3D_CONTEXT);

    if (!real_D3D11CreateDeviceContextState) {
        real_D3D11CreateDeviceContextState = get_real_function("D3D11CreateDeviceContextState");
    }
    if (real_D3D11CreateDeviceContextState) {
        return real_D3D11CreateDeviceContextState(pDevice, pDesc, ppDeviceContextState);
    }

    return E_FAIL;
}

// D3D11CreateBuffer
HRESULT WINAPI D3D11CreateBuffer(
    ID3D11Device *pDevice,
    const D3D11_BUFFER_DESC *pDesc,
    const void *pInitialData,
    ID3D11Buffer **ppBuffer
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D11CreateBuffer) {
            real_D3D11CreateBuffer = get_real_function("D3D11CreateBuffer");
        }
        if (real_D3D11CreateBuffer) {
            return real_D3D11CreateBuffer(pDevice, pDesc, pInitialData, ppBuffer);
        }
        return E_FAIL;
    }

    log_debug("D3D11CreateBuffer intercepted (size: %u)", pDesc ? pDesc->ByteWidth : 0);
    
    submit_d3d_workload("CreateBuffer", MVGAL_WORKLOAD_D3D_BUFFER);

    if (!real_D3D11CreateBuffer) {
        real_D3D11CreateBuffer = get_real_function("D3D11CreateBuffer");
    }
    if (real_D3D11CreateBuffer) {
        return real_D3D11CreateBuffer(pDevice, pDesc, pInitialData, ppBuffer);
    }

    return E_FAIL;
}

// CreateDXGIFactory1
HRESULT WINAPI CreateDXGIFactory1(
    REFIID riid,
    void **ppFactory
) {
    if (!wrapper_state.enabled) {
        if (!real_CreateDXGIFactory1) {
            real_CreateDXGIFactory1 = get_real_function("CreateDXGIFactory1");
        }
        if (real_CreateDXGIFactory1) {
            return real_CreateDXGIFactory1(riid, ppFactory);
        }
        return E_FAIL;
    }

    log_debug("CreateDXGIFactory1 intercepted");

    submit_d3d_workload("CreateDXGIFactory1", MVGAL_WORKLOAD_D3D_CONTEXT);

    if (!real_CreateDXGIFactory1) {
        real_CreateDXGIFactory1 = get_real_function("CreateDXGIFactory1");
    }
    if (real_CreateDXGIFactory1) {
        return real_CreateDXGIFactory1(riid, ppFactory);
    }

    return E_FAIL;
}

// DXGIFactory_EnumAdapters1
HRESULT WINAPI DXGIFactory_EnumAdapters1(
    IDXGIFactory *pFactory,
    UINT Adapter,
    IDXGIAdapter **ppAdapter
) {
    if (!wrapper_state.enabled) {
        if (!real_DXGIFactory_EnumAdapters1) {
            real_DXGIFactory_EnumAdapters1 = get_real_function("EnumAdapters1");
        }
        if (real_DXGIFactory_EnumAdapters1) {
            return real_DXGIFactory_EnumAdapters1(pFactory, Adapter, ppAdapter);
        }
        return E_FAIL;
    }

    log_debug("EnumAdapters1 intercepted (adapter: %u)", Adapter);

    submit_d3d_workload("EnumAdapters1", MVGAL_WORKLOAD_D3D_CONTEXT);

    if (!real_DXGIFactory_EnumAdapters1) {
        real_DXGIFactory_EnumAdapters1 = get_real_function("EnumAdapters1");
    }
    if (real_DXGIFactory_EnumAdapters1) {
        return real_DXGIFactory_EnumAdapters1(pFactory, Adapter, ppAdapter);
    }

    return E_FAIL;
}

/******************************************************************************
 * D3D12 Function Intercepts
 ******************************************************************************/

// D3D12CreateDevice
HRESULT WINAPI D3D12CreateDevice(
    IUnknown *pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void **ppDevice
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12CreateDevice) {
            real_D3D12CreateDevice = get_real_function("D3D12CreateDevice");
        }
        if (real_D3D12CreateDevice) {
            return real_D3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);
        }
        return E_FAIL;
    }

    log_debug("D3D12CreateDevice intercepted");

    submit_d3d_workload("D3D12CreateDevice", MVGAL_WORKLOAD_D3D_CONTEXT);

    if (!real_D3D12CreateDevice) {
        real_D3D12CreateDevice = get_real_function("D3D12CreateDevice");
    }
    if (real_D3D12CreateDevice) {
        return real_D3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);
    }

    return E_FAIL;
}

// D3D12Device_CreateCommandQueue
HRESULT WINAPI D3D12Device_CreateCommandQueue(
    ID3D12Device *pDevice,
    const D3D12_COMMAND_QUEUE_DESC *pDesc,
    REFIID riid,
    void **ppCommandQueue
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12Device_CreateCommandQueue) {
            real_D3D12Device_CreateCommandQueue = get_real_function("D3D12Device_CreateCommandQueue");
        }
        if (real_D3D12Device_CreateCommandQueue) {
            return real_D3D12Device_CreateCommandQueue(pDevice, pDesc, riid, ppCommandQueue);
        }
        return E_FAIL;
    }

    log_debug("D3D12Device_CreateCommandQueue intercepted");

    submit_d3d_workload("CreateCommandQueue", MVGAL_WORKLOAD_D3D_QUEUE);

    if (!real_D3D12Device_CreateCommandQueue) {
        real_D3D12Device_CreateCommandQueue = get_real_function("D3D12Device_CreateCommandQueue");
    }
    if (real_D3D12Device_CreateCommandQueue) {
        return real_D3D12Device_CreateCommandQueue(pDevice, pDesc, riid, ppCommandQueue);
    }

    return E_FAIL;
}

// D3D12Device_CreateGraphicsPipelineState
HRESULT WINAPI D3D12Device_CreateGraphicsPipelineState(
    ID3D12Device *pDevice,
    void *pDesc,
    REFIID riid,
    void **ppPipelineState
) {
    if (!wrapper_state.enabled) {
        if (!real_D3D12Device_CreateGraphicsPipelineState) {
            real_D3D12Device_CreateGraphicsPipelineState = get_real_function("D3D12Device_CreateGraphicsPipelineState");
        }
        if (real_D3D12Device_CreateGraphicsPipelineState) {
            return real_D3D12Device_CreateGraphicsPipelineState(pDevice, pDesc, riid, ppPipelineState);
        }
        return E_FAIL;
    }

    log_debug("D3D12Device_CreateGraphicsPipelineState intercepted");

    submit_d3d_workload("CreateGraphicsPipelineState", MVGAL_WORKLOAD_D3D_PIPELINE);

    if (!real_D3D12Device_CreateGraphicsPipelineState) {
        real_D3D12Device_CreateGraphicsPipelineState = get_real_function("D3D12Device_CreateGraphicsPipelineState");
    }
    if (real_D3D12Device_CreateGraphicsPipelineState) {
        return real_D3D12Device_CreateGraphicsPipelineState(pDevice, pDesc, riid, ppPipelineState);
    }

    return E_FAIL;
}
