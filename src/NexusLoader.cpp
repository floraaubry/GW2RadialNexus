#include "NexusLoader.h"

#include <BaseCore.h>
#include <Log.h>
#include <Nexus.h>
#include <d3d11_4.h>
#include <dxgi.h>

const char* GetAddonName();
const wchar_t* GetAddonNameW();

void NexusLoader::OnPreRender() {
    auto& self = i();

    if(!self.firstPreRenderSeen_) {
        self.firstPreRenderSeen_ = true;
        LogInfo("NexusLoader: first OnPreRender() call");
    }

    // Only do the *heavy* acquisition (ShaderManager/ImGui reinit) once. Comparing raw
    // IDXGISwapChain* pointers across frames to detect "did the swapchain change" turned out to
    // be unreliable - COM doesn't guarantee a stable pointer for a given object across separate
    // QueryInterface calls, only IUnknown identity does, and Nexus may well hand out a freshly
    // queried interface pointer for the same underlying swapchain on some internal cycle. Treating
    // that as "swapchain changed" re-triggered full reinit every frame from that point on - the
    // busy-loop/huge-lag-after-a-few-seconds regression. The per-frame backbuffer refresh below
    // is the only part that actually needs to run every frame.
    if(self.api_ && !self.deviceAcquired_)
        self.AcquireDeviceAndSwapChain();

    if(self.deviceAcquired_) {
        // Modern (flip-model) swap chains rotate through multiple physical buffers, so "buffer
        // index 0" fetched once at acquisition time stops being the buffer actually on screen
        // after a few Present() calls - draws keep succeeding, just onto a buffer nobody sees.
        // Re-fetch it every frame instead of relying on the one grabbed in PostCreateSwapChain.
        GetBaseCore().PreResizeSwapChain();
        GetBaseCore().PostResizeSwapChain(GetBaseCore().screenWidth(), GetBaseCore().screenHeight());

        GetBaseCore().Draw();
    }
}

void NexusLoader::OnWindowResized(void* eventArgs) {
    auto& self = i();
    if(!self.api_)
        return;

    auto* nexusLink = static_cast<NexusLinkData_t*>(self.api_->DataLink_Get(DL_NEXUS_LINK));
    if(!nexusLink)
        return;

    GetBaseCore().PreResizeSwapChain();
    GetBaseCore().PostResizeSwapChain(nexusLink->Width, nexusLink->Height);
}

void NexusLoader::AcquireDeviceAndSwapChain() {
    if(!api_ || !api_->SwapChain)
        return;

    auto* swc = static_cast<IDXGISwapChain*>(api_->SwapChain);

    ComPtr<ID3D11Device> dev;
    if(FAILED(swc->GetDevice(__uuidof(ID3D11Device), &dev))) {
        LogWarn("NexusLoader: SwapChain present but GetDevice() failed, will retry next frame.");
        return;
    }

    DXGI_SWAP_CHAIN_DESC desc;
    if(FAILED(swc->GetDesc(&desc))) {
        LogWarn("NexusLoader: SwapChain present but GetDesc() failed, will retry next frame.");
        return;
    }

    LogInfo("NexusLoader: acquired device/swapchain (hwnd: {})", LogPtr | desc.OutputWindow);

    deviceAcquired_ = true;
    GetBaseCore().PostCreateSwapChain(desc.OutputWindow, dev.Get(), swc);
}

void NexusLoader::Init(AddonAPI_t* api) {
    api_ = api;

    LogInfo("NexusLoader: Init() called");

    api_->GUI_Register(RT_PreRender, &NexusLoader::OnPreRender);
    api_->Events_Subscribe(EV_WINDOW_RESIZED, &NexusLoader::OnWindowResized);
}

void NexusLoader::Shutdown() {
    if(!api_)
        return;

    api_->GUI_Deregister(&NexusLoader::OnPreRender);
    api_->Events_Unsubscribe(EV_WINDOW_RESIZED, &NexusLoader::OnWindowResized);

    api_ = nullptr;
    deviceAcquired_ = false;
}
