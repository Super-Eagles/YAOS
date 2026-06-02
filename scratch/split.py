import os

source_file = "d:/GITHUB/YAOS/src/ui/StudioBackend.cpp"
with open(source_file, "r", encoding="utf-8") as f:
    content = f.read()

# We will match functions by their signature
import re

def extract_function(func_name, text):
    # This is a bit tricky with regex for C++. It's better to just find the function by name and find the matching closing brace.
    idx = text.find(func_name)
    if idx == -1: return "", text
    
    # Find the beginning of the line
    start_idx = text.rfind("\n", 0, idx)
    if start_idx == -1: start_idx = 0
    else: start_idx += 1

    # In case there are template declarations or annotations right above
    tmp = text.rfind("\n", 0, start_idx-1)
    if tmp != -1 and "template" in text[tmp:start_idx]:
        start_idx = tmp + 1

    # Find the opening brace
    brace_idx = text.find("{", idx)
    if brace_idx == -1: return "", text
    
    # Find matching closing brace
    open_braces = 0
    end_idx = -1
    for i in range(brace_idx, len(text)):
        if text[i] == '{':
            open_braces += 1
        elif text[i] == '}':
            open_braces -= 1
            if open_braces == 0:
                end_idx = i
                break
                
    if end_idx == -1: return "", text
    
    # Include trailing newline
    if end_idx + 1 < len(text) and text[end_idx+1] == '\n':
        end_idx += 1
        
    extracted = text[start_idx:end_idx+1]
    remainder = text[:start_idx] + text[end_idx+1:]
    return extracted, remainder

def write_backend_file(filename, functions, includes):
    global content
    out = '#include "StudioBackend_p.h"\n\n'
    for inc in includes:
        out += f'#include {inc}\n'
    out += '\nnamespace yaos::ui {\n\n'
    
    for f in functions:
        ext, content = extract_function(f, content)
        if ext:
            # If it's a static/anonymous namespace helper, we might have extracted it from the anonymous namespace
            # We don't want to wrap it in another anonymous namespace, just output it.
            out += ext + "\n"
        else:
            print(f"Warning: {f} not found!")
            
    out += '} // namespace yaos::ui\n'
    with open(f"d:/GITHUB/YAOS/src/ui/{filename}", "w", encoding="utf-8") as f:
        f.write(out)

# 1. StudioBackendDto.cpp
dto_funcs = [
    "stringListToVariant(",
    "statusToVariant(",
    "taskToVariant(",
    "nodeToVariant(",
    "eventToVariant(",
    "approvalToVariant(",
    "notificationToVariant(",
    "resourceToVariant(",
    "automationToVariant(",
    "automationRunToVariant(",
    "pluginToVariant(",
    "skillToVariant(",
    "extensionCatalogToVariant(",
    "summaryToVariant(",
    "recordsToVariant", # template function
    "automationFromVariant(",
    "chatTurnToStudioResult("
]
write_backend_file("StudioBackendDto.cpp", dto_funcs, [
    '"../distributed/Contracts.h"',
    '"../runtime/RuntimeFacade.h"',
    '<QJsonArray>',
    '<QJsonObject>'
])

# 2. StudioProviderBackend.cpp
prov_funcs = [
    "providerConfigById(config::Config",
    "providerConfigById(const config::Config",
    "localModelForProvider(",
    "routedModelForProvider(",
    "preferredModelForProvider(",
    "fallbackModelCatalogForProvider(",
    "defaultApiBaseForProvider(",
    "resolvedApiBaseForProvider(",
    "providerModelError(",
    "appendProviderWarning(",
    "RuntimeFacadeStudioBackend::fetchProviderModels(",
    "RemoteStudioBackend::fetchProviderModels("
]
write_backend_file("StudioProviderBackend.cpp", prov_funcs, [
    '"../providers/ProviderRegistry.h"'
])

# 3. StudioOAuthBackend.cpp
oauth_funcs = [
    "preserveOAuthRuntimeFields(",
    "preserveOAuthDefaults(",
    "preserveLiveOAuthProviderState(",
    "preserveLiveOAuthState(",
    "copyResolvedOAuthRuntimeState(",
    "RuntimeFacadeStudioBackend::providerAuthStatus(",
    "RuntimeFacadeStudioBackend::startProviderDeviceFlow(",
    "RuntimeFacadeStudioBackend::pollProviderDeviceFlow(",
    "RuntimeFacadeStudioBackend::refreshProviderOAuth(",
    "RuntimeFacadeStudioBackend::logoutProviderOAuth(",
    "RuntimeFacadeStudioBackend::startProviderBrowserOAuth(",
    "RuntimeFacadeStudioBackend::completeProviderBrowserOAuth(",
    "RemoteStudioBackend::providerAuthStatus(",
    "RemoteStudioBackend::startProviderDeviceFlow(",
    "RemoteStudioBackend::pollProviderDeviceFlow(",
    "RemoteStudioBackend::refreshProviderOAuth(",
    "RemoteStudioBackend::logoutProviderOAuth(",
    "RemoteStudioBackend::startProviderBrowserOAuth(",
    "RemoteStudioBackend::completeProviderBrowserOAuth("
]
write_backend_file("StudioOAuthBackend.cpp", oauth_funcs, [
    '"../providers/ProviderOAuth.h"'
])

# 4. StudioControlBackend.cpp
ctrl_funcs = [
    "controlPlaneEndpoint(",
    "RuntimeFacadeStudioBackend::pushDelegationTemplatesToControl(",
    "RuntimeFacadeStudioBackend::pullDelegationTemplatesFromControl(",
    "RemoteStudioBackend::pushDelegationTemplatesToControl(",
    "RemoteStudioBackend::pullDelegationTemplatesFromControl("
]
write_backend_file("StudioControlBackend.cpp", ctrl_funcs, [
    '"../config/DelegationTemplateExchange.h"',
    '"../distributed/RemoteControlClient.h"',
    '<QJsonDocument>',
    '<QJsonArray>'
])

# 5. StudioAutomationBackend.cpp
auto_funcs = [
    "RuntimeFacadeStudioBackend::automations(",
    "RuntimeFacadeStudioBackend::automationRuns(",
    "RuntimeFacadeStudioBackend::automation(",
    "RuntimeFacadeStudioBackend::saveAutomation(",
    "RuntimeFacadeStudioBackend::removeAutomation(",
    "RuntimeFacadeStudioBackend::runAutomation("
]
write_backend_file("StudioAutomationBackend.cpp", auto_funcs, [
    '"../runtime/AutomationStore.h"',
    '<QJsonObject>'
])

# 6. RemoteStudioBackend.cpp (the rest of RemoteStudioBackend)
rem_funcs = [
    "RemoteStudioBackend::RemoteStudioBackend(",
    "RemoteStudioBackend::status(",
    "RemoteStudioBackend::invokeStudioMap(",
    "RemoteStudioBackend::invokeProviderOAuth("
]
write_backend_file("RemoteStudioBackend.cpp", rem_funcs, [
    '<QJsonObject>',
    '<QJsonDocument>'
])

# Remove empty namespace blocks and generic error helpers to output to remaining
content = content.replace("namespace {\n\n", "namespace {\n")
content = content.replace("namespace {\n} // namespace\n", "")

ext, content = extract_function("operationError(", content)
with open("d:/GITHUB/YAOS/src/ui/StudioBackend.cpp", "w", encoding="utf-8") as f:
    f.write('#include "StudioBackend_p.h"\n\n' + ext + '\n' + content)

print("Done splitting.")
