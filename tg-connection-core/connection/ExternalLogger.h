#pragma once

#include <string>

class ExternalLogger {
public:
    virtual ~ExternalLogger() = default;
    virtual void log(int level, const std::string& message) = 0;
};
