// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "MaterialXLibrary.h"
#include "MaterialXDefinitions.h"
// MaterialX
#include <MaterialXFormat/XmlIo.h>

namespace halcyon::pathtracer {

namespace mx = MaterialX;

MaterialX::ConstDocumentPtr materialXDataLibrary()
{
  // as MaterialX's loadLibrary() does with each file: read it into a
  // document of its own and import that
  static const mx::ConstDocumentPtr library = [] {
    mx::DocumentPtr doc = mx::createDocument();
    for (int i = 0; i < MATERIALX_DEFINITION_FILE_COUNT; i++) {
      const MaterialXDefinitionFile &file = MATERIALX_DEFINITION_FILES[i];
      mx::DocumentPtr part = mx::createDocument();
      mx::readFromXmlString(part, file.content);
      part->setSourceUri(file.path);
      doc->importLibrary(part);
    }
    return mx::ConstDocumentPtr(doc);
  }();
  return library;
}

} // namespace halcyon::pathtracer
