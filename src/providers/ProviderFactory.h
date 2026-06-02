#ifndef YAOS_PROVIDERS_PROVIDERFACTORY_H
#define YAOS_PROVIDERS_PROVIDERFACTORY_H

#include <memory>

#include "../config/Config.h"
#include "LLMProvider.h"

namespace yaos::providers {

class ProviderFactory {
public:
    static std::unique_ptr<LLMProvider> create(const config::Config &config, QString *selectedProvider = nullptr);
};

} // namespace yaos::providers

#endif // YAOS_PROVIDERS_PROVIDERFACTORY_H
