#ifndef YAOS_RUNTIME_LOCALRUNTIMECLIENT_H
#define YAOS_RUNTIME_LOCALRUNTIMECLIENT_H

#include <memory>

#include "../distributed/Contracts.h"
#include "RuntimeCore.h"

namespace yaos::runtime {

class LocalRuntimeClient : public distributed::IRuntimeClient {
public:
    LocalRuntimeClient();
    explicit LocalRuntimeClient(std::unique_ptr<RuntimeCore> runtime);
    ~LocalRuntimeClient() override = default;

    QJsonObject invoke(const QString &method, const QJsonObject &payload) override;

private:
    static QJsonObject success(const QJsonObject &payload = QJsonObject());
    static QJsonObject failure(const QString &message);

private:
    std::unique_ptr<RuntimeCore> _runtime;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_LOCALRUNTIMECLIENT_H
