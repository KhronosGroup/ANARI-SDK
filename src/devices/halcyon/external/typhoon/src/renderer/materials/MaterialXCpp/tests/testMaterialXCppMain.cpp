//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include <renderer/materials/MaterialXCpp/mathTypes.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace mxcpp;

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------

static int _totalTests  = 0;
static int _failedTests = 0;

struct _TestEntry {
    std::string name;
    std::function<bool()> fn;
};

static std::vector<_TestEntry>& _Tests() {
    static std::vector<_TestEntry> tests;
    return tests;
}

void
Test_Register(const char* name, std::function<bool()> fn)
{
    _Tests().push_back({name, std::move(fn)});
}

bool
Test_IsClose(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

bool
Test_IsClose(const Vec3f& a, const Vec3f& b, float eps)
{
    return std::fabs(a[0]-b[0]) <= eps &&
           std::fabs(a[1]-b[1]) <= eps &&
           std::fabs(a[2]-b[2]) <= eps;
}

// Declared in individual test files.
void Test_RegisterBsdfTests();
void Test_RegisterNodeTests();
void Test_RegisterMaterialTests();
void Test_RegisterGraphTests();
void Test_RegisterAdapterTests();
void Test_RegisterClosureClassificationTests();
void Test_RegisterTextureSystemTests();

int
main(int argc, char** argv)
{
    Test_RegisterBsdfTests();
    Test_RegisterNodeTests();
    Test_RegisterMaterialTests();
    Test_RegisterGraphTests();
    Test_RegisterAdapterTests();
    Test_RegisterClosureClassificationTests();
    Test_RegisterTextureSystemTests();

    const char* filter = (argc > 1) ? argv[1] : nullptr;

    for (const auto& entry : _Tests()) {
        if (filter && entry.name.find(filter) == std::string::npos) {
            continue;
        }
        _totalTests++;
        printf("  [RUN ] %s\n", entry.name.c_str());
        bool passed = false;
        try {
            passed = entry.fn();
        } catch (const std::exception& e) {
            printf("  [EXCEPTION] %s: %s\n", entry.name.c_str(), e.what());
        }
        if (passed) {
            printf("  [PASS] %s\n", entry.name.c_str());
        } else {
            printf("  [FAIL] %s\n", entry.name.c_str());
            _failedTests++;
        }
    }

    printf("\n%d/%d tests passed.\n", _totalTests - _failedTests, _totalTests);
    if (_failedTests > 0) {
        printf("%d test(s) FAILED.\n", _failedTests);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
