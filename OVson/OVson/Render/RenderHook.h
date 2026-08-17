#pragma once
#include <functional>

namespace RenderHook {
bool install();
void uninstall();
void poll();

// True after uninstall() decided it could not safely remove one of our
// hooks because another injected module chained onto it. When this is set,
// code in this module is still reachable from that other module, so the
// DLL must NOT be unloaded -- doing so leaves them jumping into unmapped
// memory. Leaking the module until the game exits is the cheap outcome.
bool mustStayLoaded();

void enqueueTask(std::function<void()> task);
float getDelta();
} // namespace RenderHook
