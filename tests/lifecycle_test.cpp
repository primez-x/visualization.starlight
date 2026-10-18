#include <kodi/c-api/addon_base.h>
#include <kodi/c-api/addon-instance/visualization.h>
#include <kodi/versions.h>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <thread>

// Failure cases: fresh paused instance, repeated paused queries, rejected audio
// format, stopped instance, and restart after Stop or a failed Start.
namespace
{
int failures = 0;

void Check(bool condition, const char* message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void GetProperties(KODI_HANDLE, KODI_ADDON_VISUALIZATION_PROPS* props)
{
  *props = {};
  props->width = props->height = 720;
  props->pixelRatio = 1;
}

void Log(KODI_ADDON_BACKEND_HDL, int, const char* message)
{
  std::cerr << "Kodi: " << message << '\n';
}

void WaitForFrame()
{
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
}
}

int main(int argc, char** argv)
{
  if (argc != 2)
    return EXIT_FAILURE;
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library)
  {
    std::cerr << dlerror() << '\n';
    return EXIT_FAILURE;
  }
  auto create = reinterpret_cast<ADDON_STATUS (*)(KODI_HANDLE)>(dlsym(library, "ADDON_Create"));
  if (!create)
    return EXIT_FAILURE;

  AddonToKodiFuncTable_Addon host{};
  host.addon_log_msg = Log;
  KodiToAddonFuncTable_Addon addon{};
  AddonGlobalInterface global{};
  global.toKodi = &host;
  global.toAddon = &addon;
  Check(create(&global) == ADDON_STATUS_OK, "create production add-on");
  if (failures)
    return EXIT_FAILURE;

  for (int cycle = 0; cycle < 22; ++cycle)
  {
    KODI_ADDON_INSTANCE_INFO info{};
    info.type = ADDON_INSTANCE_VISUALIZATION;
    info.version = "4.0.0";
    KODI_ADDON_INSTANCE_FUNC functions{};
    KodiToAddonFuncTable_Visualization callbacks{};
    AddonToKodiFuncTable_Visualization properties{};
    properties.get_properties = GetProperties;
    AddonInstance_Visualization visualization{};
    visualization.toAddon = &callbacks;
    visualization.toKodi = &properties;
    KODI_ADDON_INSTANCE_STRUCT instance{};
    instance.info = &info;
    instance.functions = &functions;
    instance.visualization = &visualization;
    Check(addon.create_instance(global.addonBase, &instance) == ADDON_STATUS_OK,
          "create production visualization instance");
    if (!instance.hdl)
      return EXIT_FAILURE;

    Check(!callbacks.is_dirty(instance.hdl), "fresh paused instance requests no redraw");
    for (int query = 0; query < 4; ++query)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      Check(!callbacks.is_dirty(instance.hdl), "repeated paused queries request no redraw");
    }
    Check(!callbacks.start(instance.hdl, 0, 48000, 32, "invalid"), "reject invalid channels");
    WaitForFrame();
    Check(!callbacks.is_dirty(instance.hdl), "failed Start requests no redraw");

    const auto beforeStart = std::chrono::steady_clock::now();
    Check(callbacks.start(instance.hdl, 2, 48000, 32, "valid"), "accept valid Start");
    const bool earlyDirty = callbacks.is_dirty(instance.hdl);
    if (std::chrono::steady_clock::now() - beforeStart < std::chrono::milliseconds(33))
      Check(!earlyDirty, "Start preserves the frame interval");
    WaitForFrame();
    Check(callbacks.is_dirty(instance.hdl), "valid Start requests a timed redraw");

    callbacks.stop(instance.hdl);
    for (int query = 0; query < 2; ++query)
    {
      WaitForFrame();
      Check(!callbacks.is_dirty(instance.hdl), "Stop prevents further redraw requests");
    }
    Check(callbacks.start(instance.hdl, 2, 48000, 32, "restart"), "restart after Stop succeeds");
    WaitForFrame();
    Check(callbacks.is_dirty(instance.hdl), "restart resumes timed redraw requests");
    Check(!callbacks.start(instance.hdl, 2, 0, 32, "invalid"), "reject invalid sample rate");
    WaitForFrame();
    Check(!callbacks.is_dirty(instance.hdl), "failed restart prevents redraw requests");
    Check(callbacks.start(instance.hdl, 2, 48000, 32, "recovery"), "restart after failure succeeds");
    WaitForFrame();
    Check(callbacks.is_dirty(instance.hdl), "recovery resumes timed redraw requests");
    callbacks.stop(instance.hdl);
    addon.destroy_instance(global.addonBase, &instance);
  }
  addon.destroy(global.addonBase);
  dlclose(library);
  if (failures)
    return EXIT_FAILURE;
  std::cout << "PASS: production Kodi lifecycle across 22 paused instances\n";
}
