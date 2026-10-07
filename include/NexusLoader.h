#pragma once

#include "Common.h"
#include "Singleton.h"

struct AddonAPI_t;
struct ID3D11Device;
struct IDXGISwapChain;

// Nexus-specific replacement for GW2Common's Direct3D11Loader (which hooked gw2al/d3d9_wrapper's
// COM-vtable events). Nexus already hands addons a live IDXGISwapChain* and calls registered render
// callbacks directly, so there's no COM hooking to do here at all - this class only has to bridge
// those callbacks into BaseCore's existing (loader-agnostic) Draw()/PostCreateSwapChain()/etc.
class NexusLoader : public Singleton<NexusLoader>
{
public:
    void Init(AddonAPI_t* api);
    void Shutdown();

    [[nodiscard]] AddonAPI_t* api() const { return api_; }

private:
    static void OnPreRender();
    static void OnWindowResized(void* eventArgs);

    void AcquireDeviceAndSwapChain();

    AddonAPI_t* api_ = nullptr;
    bool deviceAcquired_ = false;
    bool firstPreRenderSeen_ = false;
};
