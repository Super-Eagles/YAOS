// FastNet must be included before any Windows headers to avoid winsock conflicts.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "FastNetLifecycle.h"

#include <FastNet/FastNet.h>
#include <QCoreApplication>
#include <QObject>

namespace yaos::platform {

void registerFastNetCleanup() {
    // Suppress FastNet file logging — it writes fastnet.log on every request
    // and causes unnecessary disk IO on the IO thread during normal operation.
    FastNet::setGlobalLogLevel(FastNet::LogLevel::ERROR_LVL);

    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, []() {
        FastNet::cleanup();
    });
}

} // namespace yaos::platform
