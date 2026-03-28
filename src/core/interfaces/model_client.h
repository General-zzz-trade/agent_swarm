#ifndef CORE_INTERFACES_MODEL_CLIENT_H
#define CORE_INTERFACES_MODEL_CLIENT_H

#include <string>

class IModelClient {
public:
    virtual ~IModelClient() = default;

    virtual std::string generate(const std::string& prompt) const = 0;
    virtual const std::string& model() const = 0;
};

#endif
