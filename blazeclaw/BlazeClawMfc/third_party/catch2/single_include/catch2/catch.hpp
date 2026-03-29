// Minimal Catch2-like single-header stub for CI testing
#pragma once

#include <vector>
#include <functional>
#include <iostream>
#include <string>
#include <cstdlib>

namespace test_harness {
    inline std::vector<std::function<void()>>& registry() {
        static std::vector<std::function<void()>> r;
        return r;
    }
}

#define CONCAT_INTERNAL(a,b) a##b
#define CONCAT(a,b) CONCAT_INTERNAL(a,b)
#define UNIQUE_NAME(prefix) CONCAT(prefix,__LINE__)

#define TEST_CASE(name, tags) \
    static void UNIQUE_NAME(test_)(); \
    static int UNIQUE_NAME(_reg_) = (test_harness::registry().push_back(&UNIQUE_NAME(test_)), 0); \
    static void UNIQUE_NAME(test_)()

#define REQUIRE(cond) do { if (!(cond)) { std::cerr << "REQUIRE failed: " #cond << std::endl; std::exit(1); } } while(0)

// Provide a main runner when CATCH_CONFIG_MAIN is defined in test translation unit
#ifdef CATCH_CONFIG_MAIN
int main(int argc, char** argv) {
    const auto& regs = test_harness::registry();
    int passed = 0;
    int total = static_cast<int>(regs.size());
    for (size_t i = 0; i < regs.size(); ++i) {
        try {
            regs[i]();
            ++passed;
        } catch (const std::exception& ex) {
            std::cerr << "Test threw exception: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "Test threw unknown exception" << std::endl;
        }
    }
    std::cout << "Passed " << passed << "/" << total << " tests\n";
    return passed == total ? 0 : 1;
}
#endif

