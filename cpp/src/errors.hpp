#pragma once
#include <stdexcept>
#include <string>

constexpr int EXIT_OK            = 0;
constexpr int EXIT_BLOCK_FAILURE = 1;
constexpr int EXIT_ABORT_ALL     = 2;
constexpr int EXIT_STARTUP       = 3;
constexpr int EXIT_SIGINT        = 130;

struct CodegenError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ConfigError : CodegenError {
    using CodegenError::CodegenError;
};

struct ParseError : CodegenError {
    ParseError(const std::string& msg) : CodegenError(msg) {}
};
