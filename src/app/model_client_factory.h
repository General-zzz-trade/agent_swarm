#ifndef APP_MODEL_CLIENT_FACTORY_H
#define APP_MODEL_CLIENT_FACTORY_H

#include <memory>
#include <string>

#include "../core/interfaces/http_transport.h"
#include "../core/interfaces/model_client.h"
#include "app_config.h"

std::unique_ptr<IModelClient> create_model_client(
    const AppConfig& config,
    const std::string& model_override,
    std::shared_ptr<IHttpTransport> transport);

#endif
