#pragma once

namespace yaos::platform {

// Register FastNet::cleanup() to be called on QCoreApplication::aboutToQuit.
// Call this once after QCoreApplication is created.
void registerFastNetCleanup();

} // namespace yaos::platform
