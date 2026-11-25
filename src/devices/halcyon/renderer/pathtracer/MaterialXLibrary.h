// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// MaterialX's standard data library (the nodedefs and their node graph
// implementations), read once per process from the definitions the build
// embedded (MaterialXDefinitions.h), so no search path or environment
// variable is involved. Documents reference it read-only
// (Document::setDataLibrary()). Only the halcyon_materialx_documents library
// includes MaterialX headers; the device's own sources don't, so no MaterialX
// symbol is instantiated in them.

#include <MaterialXCore/Document.h>

namespace halcyon::pathtracer {

// The library; thread-safe. Throws MaterialX's exceptions only if an embedded
// file doesn't parse, which the materialx_library ctest rules out.
MaterialX::ConstDocumentPtr materialXDataLibrary();

} // namespace halcyon::pathtracer
