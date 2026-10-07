#include <Core.h>
#include <Main.h>
#include <NexusLoader.h>
#include <Nexus.h>
#include <Version.h>

const char* GetAddonName() {
    return ADDON_NAME;
}

const wchar_t* GetAddonNameW() {
    return TEXT(ADDON_NAME);
}

const char* GetAddonVersionString() {
    return GIT_VER_STR;
}

u64 GetAddonVersion() {
    static std::array<u64, 4> ver = { GIT_VER };
    return (ver[0] << 16 * 3) + (ver[1] << 16 * 2) + (ver[2] << 16 * 1) + (ver[3] << 16 * 0);
}

BaseCore& GetBaseCore() {
    return GW2Radial::Core::i();
}

HMODULE g_dllModule = nullptr;
std::ofstream g_logStream;

void AddonLoad(AddonAPI_t* aApi) {
    // BaseCore/Core do all of their own device/window/ImGui setup lazily once NexusLoader hands
    // them a real device+swapchain (see NexusLoader::OnPreRender) - this just wires that up.
    GW2Radial::Core::Init(g_dllModule);
    NexusLoader::i().Init(aApi);
}

void AddonUnload() {
    // Nexus can hot-unload this addon while the game keeps running (unlike gw2al, which only ever
    // unloaded at process exit), so we have to actually tear down cleanly here rather than relying
    // on process death to clean up for us.
    NexusLoader::i().Shutdown();
    BaseCore::Shutdown();
}

AddonDefinition_t g_addonDef = {
    // TODO: register this addon on raidcore.gg and replace with the assigned signature; a negative
    // placeholder is fine for local/unlisted use but two unrelated unlisted addons could collide.
    -0x47325244, // "GW2RD" ~ish, placeholder
    NEXUS_API_VERSION,
    "GW2Radial",
    { 0, 0, 0, 9999 }, // TODO: wire up to the same GIT_VER generation GW2Radial's build already uses
    "Flora Aubry (original: Friendly0Fire)",
    "Radial menu overlay to select mount, novelty and more on the fly",
    AddonLoad,
    AddonUnload,
    AF_None,
    UP_None, // TODO: set to UP_GitHub + UpdateLink once this port has its own repo/releases
    nullptr,
};

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    return &g_addonDef;
}

BOOL WINAPI DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
    switch(fdwReason) {
        case DLL_PROCESS_ATTACH:
            g_dllModule = hModule;
            g_logStream = std::ofstream("gw2radial.log");
            break;
        case DLL_PROCESS_DETACH:
            break;
    }

    return TRUE;
}
