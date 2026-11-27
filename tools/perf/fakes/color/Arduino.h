#pragma once
#include <string>
struct TestSerial {
    std::string output;
    int printf(const char *, ...) __attribute__((format(printf, 2, 3)));
};
extern TestSerial Serial;
