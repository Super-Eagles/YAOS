#include "RuntimeFacade.h"

#include "../daemon/DaemonRuntimeClient.h"
#include "RemoteRuntimeClient.h"
#include "RuntimeClientFacade.h"
#include "RuntimeCore.h"
#include "RuntimeServiceSupport.h"

namespace yaos::runtime {

RuntimeFacadeSelection createRuntimeFacade(const config::Config &config) {
    RuntimeFacadeSelection selection;
    selection.requestedMode = config.normalizedRuntimeMode();
    selection.activeMode = "embedded";

    if (selection.requestedMode == "daemon") {
        auto client = std::make_unique<::yaos::daemon::DaemonRuntimeClient>(config);
        QString error;
        if (client->ensureReady(&error)) {
            selection.activeMode = "daemon";
            selection.facade = std::make_unique<RuntimeClientFacade>(std::move(client));
            return selection;
        }

        selection.fallbackReason = error.isEmpty()
            ? QStringLiteral("Runtime mode 'daemon' could not connect to the local sidecar. YAOS is using the embedded runtime.")
            : error;
    }
    if (selection.requestedMode == "remote") {
        const RuntimeServiceAvailability availability = ensureRuntimeService(config, true);
        if (availability.reachable) {
            selection.activeMode = "remote";
            selection.facade = std::make_unique<RuntimeClientFacade>(
                std::make_unique<RemoteRuntimeClient>(config));
            return selection;
        }

        selection.fallbackReason = availability.error.isEmpty()
            ? QStringLiteral("Runtime mode 'remote' could not connect to the configured runtime endpoint. YAOS is using the embedded runtime.")
            : availability.error;
    }

    selection.facade = std::make_unique<RuntimeCore>();
    return selection;
}

} // namespace yaos::runtime
