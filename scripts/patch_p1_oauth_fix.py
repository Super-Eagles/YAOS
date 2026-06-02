"""
Fix configChanged key usage in pollProviderOAuth and refreshProviderOAuth:
backend returns ProviderOAuthResult::toVariantMap() which uses 'changed', not 'configChanged'.
Also fix refreshProviderOAuth which doesn't update m_config properly.
"""
filepath = r'd:\GITHUB\YAOS\src\ui\StudioBridge.cpp'

with open(filepath, 'rb') as f:
    raw = f.read()

content = raw.decode('utf-8', errors='replace')

# Fix pollProviderOAuth: configChanged -> changed, and m_config update
old_poll_check = (
    '    const QVariantMap result = backend->pollProviderDeviceFlow(&m_config, normalized);\r\n'
    '    if (result.value(QStringLiteral("configChanged")).toBool()) {\r\n'
    '        m_configMap = m_config.toJson().toVariantMap();\r\n'
    '        emit configChanged();\r\n'
    '        refreshAll();\r\n'
    '    }\r\n'
)
new_poll_check = (
    '    const QVariantMap result = backend->pollProviderDeviceFlow(&m_config, normalized);\r\n'
    '    if (result.value(QStringLiteral("changed")).toBool()) {\r\n'
    '        m_configMap = m_config.toJson().toVariantMap();\r\n'
    '        emit configChanged();\r\n'
    '        refreshAll();\r\n'
    '    }\r\n'
)
if old_poll_check in content:
    content = content.replace(old_poll_check, new_poll_check, 1)
    print("Fix poll configChanged -> changed: applied")
else:
    print("Fix poll configChanged -> changed: NOT FOUND")

# Fix refreshProviderOAuth: configChanged -> changed, and add m_config sync
old_refresh_check = (
    '    const QVariantMap result = backend->refreshProviderOAuth(&m_config, normalized);\r\n'
    '    if (result.value(QStringLiteral("configChanged")).toBool()) {\r\n'
    '        m_configMap = m_config.toJson().toVariantMap();\r\n'
    '        emit configChanged();\r\n'
    '        refreshAll();\r\n'
    '    }\r\n'
)
new_refresh_check = (
    '    const QVariantMap result = backend->refreshProviderOAuth(&m_config, normalized);\r\n'
    '    if (result.value(QStringLiteral("changed")).toBool()) {\r\n'
    '        m_configMap = m_config.toJson().toVariantMap();\r\n'
    '        emit configChanged();\r\n'
    '        refreshAll();\r\n'
    '    }\r\n'
)
if old_refresh_check in content:
    content = content.replace(old_refresh_check, new_refresh_check, 1)
    print("Fix refresh configChanged -> changed: applied")
else:
    print("Fix refresh configChanged -> changed: NOT FOUND")

with open(filepath, 'wb') as f:
    f.write(content.encode('utf-8'))

print("Done.")
