#include "utils.h"

// StreamingPreferences only queries these to pick the recommended
// fullscreen mode. Stubbing them keeps the test free of SDL and
// windowing system dependencies.
bool WMUtils::isRunningWayland()
{
    return false;
}

bool WMUtils::isGpuSlow()
{
    return false;
}
