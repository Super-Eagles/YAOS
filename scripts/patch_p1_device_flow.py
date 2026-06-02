"""
Patch beginProviderOAuthWithConfig: replace the device flow branch
(providers::startDeviceFlow direct call) with backend->startProviderDeviceFlow.
"""
filepath = r'd:\GITHUB\YAOS\src\ui\StudioBridge.cpp'

with open(filepath, 'rb') as f:
    raw = f.read()

content = raw.decode('utf-8', errors='replace')

# The device flow branch at the end of beginProviderOAuthWithConfig
# It's after the browser branch return, so it's the fallback path.
old = (
    '    providers::ProviderOAuthResult result = providers::startDeviceFlow(normalized, *provider);\r\n'
    '    if (result.changed) {\r\n'
    '        *provider = result.config;\r\n'
    '        if (!persistConfig(false)) {\r\n'
    '            result.ok = false;\r\n'
    '            result.error = QStringLiteral("failed to persist OAuth state");\r\n'
    '        }\r\n'
    '    }\r\n'
    '    if (!result.ok) {\r\n'
    '        emit toastRequested(QStringLiteral("OAuth failed"),\r\n'
    '                            result.error,\r\n'
    '                            QStringLiteral("warning"));\r\n'
    '    } else {\r\n'
    '        emit toastRequested(QStringLiteral("Device login ready"),\r\n'
    '                            QStringLiteral("Use the verification code shown in the provider card."),\r\n'
    '                            QStringLiteral("neutral"));\r\n'
    '    }\r\n'
    '    return result.toVariantMap();\r\n'
    '}'
)

new = (
    '    // P1.1: device flow delegated to backend\r\n'
    '    std::shared_ptr<IStudioBackend> backend;\r\n'
    '    {\r\n'
    '        QMutexLocker locker(&m_backendMutex);\r\n'
    '        backend = m_backend;\r\n'
    '    }\r\n'
    '    if (!backend) {\r\n'
    '        emit toastRequested(QStringLiteral("OAuth failed"),\r\n'
    '                            QStringLiteral("runtime backend is not initialized"),\r\n'
    '                            QStringLiteral("warning"));\r\n'
    '        return QVariantMap{{QStringLiteral("ok"), false},\r\n'
    '                           {QStringLiteral("error"), QStringLiteral("runtime backend is not initialized")}};\r\n'
    '    }\r\n'
    '    // Pass nextConfig so the backend can persist and update it in place.\r\n'
    '    const QVariantMap result = backend->startProviderDeviceFlow(&nextConfig, normalized);\r\n'
    '    if (result.value(QStringLiteral("changed")).toBool()) {\r\n'
    '        m_config = nextConfig;\r\n'
    '        m_configMap = m_config.toJson().toVariantMap();\r\n'
    '        emit configChanged();\r\n'
    '        refreshAll();\r\n'
    '    }\r\n'
    '    if (!result.value(QStringLiteral("ok")).toBool()) {\r\n'
    '        emit toastRequested(QStringLiteral("OAuth failed"),\r\n'
    '                            result.value(QStringLiteral("error")).toString(),\r\n'
    '                            QStringLiteral("warning"));\r\n'
    '    } else {\r\n'
    '        emit toastRequested(QStringLiteral("Device login ready"),\r\n'
    '                            QStringLiteral("Use the verification code shown in the provider card."),\r\n'
    '                            QStringLiteral("neutral"));\r\n'
    '    }\r\n'
    '    return result;\r\n'
    '}'
)

if old in content:
    content = content.replace(old, new, 1)
    print("Patch device flow in beginProviderOAuthWithConfig: applied")
else:
    print("Patch device flow in beginProviderOAuthWithConfig: NOT FOUND")

with open(filepath, 'wb') as f:
    f.write(content.encode('utf-8'))

print("Done.")
