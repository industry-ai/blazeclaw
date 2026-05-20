#pragma once

#include "Logger.h"

#include <fstream>
#include <mutex>
#include <string>

class ConsoleSink final : public ILogSink {
public:
    void Write(LogLevel level, const std::string& line) override;
};

class FileSink final : public ILogSink {
public:
    explicit FileSink(std::string path);
    void Write(LogLevel level, const std::string& line) override;

private:
    std::mutex mutex_;
    std::ofstream file_;
};