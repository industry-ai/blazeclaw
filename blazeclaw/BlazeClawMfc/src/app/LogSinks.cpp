#include "pch.h"
#include "LogSinks.h"

#include <iostream>

void ConsoleSink::Write(LogLevel level, const std::string& line) {
    (void)level;
    // Keep behavior: Error/Warn -> stderr, others -> stdout
    if (level == LogLevel::Error || level == LogLevel::Warn) {
        std::cerr << line << std::endl;
    } else {
        std::cout << line << std::endl;
    }
}

FileSink::FileSink(std::string path)
    : file_(std::move(path), std::ios::out | std::ios::app) {}

void FileSink::Write(LogLevel level, const std::string& line) {
    (void)level;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open()) return;
    file_ << line << "\n";
    file_.flush();
}