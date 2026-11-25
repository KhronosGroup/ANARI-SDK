// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Typhoon's test runner (testMaterialXCppMain.cpp) registers every suite,
// including those that need OpenUSD, OpenImageIO or the rest of Typhoon's
// renderer, and the BSDF suite, which includes its integrator (see
// external/typhoon/PROVENANCE.md). They register no tests here,
// so the runner stays unmodified.

void Test_RegisterBsdfTests() {}
void Test_RegisterAdapterTests() {}
void Test_RegisterClosureClassificationTests() {}
void Test_RegisterTextureSystemTests() {}
