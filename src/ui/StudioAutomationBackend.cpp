#include "StudioBackend_p.h"

#include "../runtime/AutomationStore.h"
#include <QJsonObject>

namespace yaos::ui {

QVariantList RuntimeFacadeStudioBackend::automations(int limit) {
    return m_facade ? recordsToVariant(m_facade->automations(limit), automationToVariant) : QVariantList{};
}

QVariantList RuntimeFacadeStudioBackend::automationRuns(int limit,
                                                        const QString &automationId) {
    return m_facade ? recordsToVariant(m_facade->automationRuns(limit, automationId), automationRunToVariant)
                    : QVariantList{};
}

QString RuntimeFacadeStudioBackend::saveAutomation(const QVariantMap &recordMap,
                                                   QString *error) {
    if (!m_facade) {
        if (error) {
            *error = QStringLiteral("runtime facade is not initialized");
        }
        return QString();
    }
    return m_facade->saveAutomation(automationFromVariant(recordMap), error);
}

bool RuntimeFacadeStudioBackend::removeAutomation(const QString &id) {
    return m_facade && m_facade->removeAutomation(id);
}

QString RuntimeFacadeStudioBackend::runAutomation(const QString &id,
                                             QString *error,
                                             const QString &sessionKey) {
    if (!m_facade) {
        if (error) {
            *error = QStringLiteral("runtime facade is not initialized");
        }
        return QString();
    }
    return m_facade->runAutomation(id, error, sessionKey);
}

} // namespace yaos::ui
