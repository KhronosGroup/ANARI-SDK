// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/anari.h"
#include "DebugDevice.h"
#include "CodeSerializer.h"

#include "anari/type_utility.h"

namespace anari {
namespace debug_device {


template<int T>
struct anari_printer {
   using base_type = typename anari::ANARITypeProperties<T>::base_type;
   static const int components = anari::ANARITypeProperties<T>::components;
   void operator()(std::ostream &out, const void *mem) {
      const base_type *typed = (const base_type*)mem;
      out << typed[0];
      for(int i = 1;i<components;++i) {
         out << ", " << typed[i];
      }
   }
};

template<>
struct anari_printer<ANARI_DATA_TYPE> {
   void operator()(std::ostream &out, const void *mem) {
      const ANARIDataType *typed = (const ANARIDataType*)mem;
      out << anari::toString(*typed);
   }
};

template<>
struct anari_printer<ANARI_VOID_POINTER> {
   void operator()(std::ostream &out, const void *mem) {
      out << mem;
   }
};

template<>
struct anari_printer<ANARI_STRING> {
   void operator()(std::ostream &out, const void *mem) {
      const char *str = (const char*)mem;
      out << '"' << str << '"';
   }
};

static void printFromMemory(std::ostream &out, ANARIDataType t, const void *mem) {
   anari::anariTypeInvoke<void, anari_printer>(t, out, mem);
}

CodeSerializer::CodeSerializer(DebugDevice *dd) : dd(dd), locals(0) {
   std::string dir = dd->traceDir;
   if(!dir.empty()) {
      dir+='/';
   }

   dd->reportStatus(dd->this_device(),
      ANARI_DEVICE,
      ANARI_SEVERITY_INFO,
      ANARI_STATUS_UNKNOWN_ERROR,
      "tracing enabled");
   out.open(dir+"out.c");
   if(!out) {
      dd->reportStatus(dd->this_device(),
         ANARI_DEVICE,
         ANARI_SEVERITY_INFO,
         ANARI_STATUS_UNKNOWN_ERROR,
         "could not open %sout.c", dir.c_str());
   }
   data.open(dir+"data.bin", std::ios::binary);
   if(!data) {
      dd->reportStatus(dd->this_device(),
         ANARI_DEVICE,
         ANARI_SEVERITY_INFO,
         ANARI_STATUS_UNKNOWN_ERROR,
         "could not open %sdata.bin", dir.c_str());
   }


}

SerializerInterface* CodeSerializer::create(DebugDevice *dd) {
   return new CodeSerializer(dd);
}


void CodeSerializer::printObjectName(ANARIObject object) {
   if(object == dd->this_device()) {
      out << "device";
   }else if(auto objinfo = dd->getObjectInfo(object)) {
      out << anari::varnameOf(objinfo->getType()) << reinterpret_cast<uintptr_t>(object);
   } else {
      out << "unknown_handle";
   }
}

void CodeSerializer::insertStatus(ANARIObject source, ANARIDataType sourceType, ANARIStatusSeverity severity, ANARIStatusCode code, const char *status) {
   out << "//";
   if (severity == ANARI_SEVERITY_FATAL_ERROR) {
      out << "[FATAL] ";
   } else if (severity == ANARI_SEVERITY_ERROR) {
      out << "[ERROR] ";
   } else if (severity == ANARI_SEVERITY_WARNING) {
      out << "[WARN ] ";
   } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
      out << "[PERF ] ";
   } else if (severity == ANARI_SEVERITY_INFO) {
      out << "[INFO ] ";
   } else if (severity == ANARI_SEVERITY_DEBUG) {
      out << "[DEBUG] ";
   }
   out << status << '\n';
}

void CodeSerializer::anariNewArray1D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t byteStride1, ANARIArray1D result) {
   uint64_t local;
   uint64_t byte_offset = 0;
   uint64_t element_size = anari::sizeOf(dataType);
   uint64_t byte_size = element_size*numItems1;

   if(isObject(dataType)) {
      const ANARIObject *handles = (const ANARIObject*)appMemory;
      local = locals++;
      out << "const " << anari::typenameOf(dataType) << " " << anari::varnameOf(dataType) << "_local" << local << "[] = {";
      for(uint64_t i = 0;i<numItems1;++i) {
         out << anari::varnameOf(dataType) << reinterpret_cast<uintptr_t>(handles[i]);
         if(i+1<numItems1) {
            out << ", ";
         }
      }
      out << "};\n";
   } else {
      byte_offset = data.tellp();
      const char *charAppMemory = (const char*)appMemory;
      if(byteStride1 == 0 || byteStride1 == element_size) {
         data.write(charAppMemory, byte_size);
      } else {
         // destride the data
         for(uint64_t i = 0;i<numItems1;++i) {
            data.write(charAppMemory + i*byteStride1, element_size);
         }
      }
   }

   out << "ANARIArray1D " << anari::varnameOf(ANARI_ARRAY1D) << reinterpret_cast<uintptr_t>(result) << " = anariNewArray1D(device, ";

   if(isObject(dataType)) {
      out << anari::varnameOf(dataType) << "_local" << local << ", ";
   } else {
      out << "data(" << byte_offset << ", " << byte_size << "), ";
   }
   if(deleter) {
      out << "deleter, deleterData, ";
   } else {
      out << "NULL, NULL, ";
   }
   out << anari::toString(dataType) << ", ";
   out << numItems1 << ", 0);";
   if(byteStride1 != 0) {
      out << "// original stride: " << byteStride1;
   }
   out << "\n";
}

void CodeSerializer::anariNewArray2D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t byteStride1, uint64_t byteStride2, ANARIArray2D result) {
   uint64_t byte_offset = data.tellp();
   uint64_t element_size = anari::sizeOf(dataType);
   uint64_t byte_size = element_size*numItems1*numItems2;

   const char *charAppMemory = (const char*)appMemory;

   bool compact =
      (byteStride1 == 0 || byteStride1 == element_size) &&
      (byteStride2 == 0 || byteStride2 == element_size*byteStride1);

   if(compact) {
      data.write(charAppMemory, byte_size);
   } else {
      // destride the data
      for(uint64_t i2 = 0;i2<numItems2;++i2) {
         for(uint64_t i1 = 0;i1<numItems1;++i1) {
            data.write(charAppMemory + i1*byteStride1 + i2*byteStride2, element_size);
         }
      }
   }

   out << "ANARIArray2D " << anari::varnameOf(ANARI_ARRAY2D) << reinterpret_cast<uintptr_t>(result) << " = anariNewArray2D(device, ";
   out << "data(" << byte_offset << ", " << byte_size << "), ";
   if(deleter) {
      out << "deleter, deleterData, ";
   } else {
      out << "NULL, NULL, ";
   }
   out << anari::toString(dataType) << ", ";
   out << numItems1 << ", ";
   out << numItems2 << ", 0, 0);";
   if(!compact) {
      out << "// original strides: " << byteStride1 << ' ' << byteStride2;
   }
   out << "\n";
}

void CodeSerializer::anariNewArray3D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3, uint64_t byteStride1, uint64_t byteStride2, uint64_t byteStride3, ANARIArray3D result) {
   uint64_t byte_offset = data.tellp();
   uint64_t element_size = anari::sizeOf(dataType);
   uint64_t byte_size = element_size*numItems1*numItems2*numItems3;

   const char *charAppMemory = (const char*)appMemory;

   bool compact =
      (byteStride1 == 0 || byteStride1 == element_size) &&
      (byteStride2 == 0 || byteStride2 == element_size*byteStride1) &&
      (byteStride3 == 0 || byteStride2 == element_size*byteStride1*byteStride2);

   if(compact) {
      data.write(charAppMemory, byte_size);
   } else {
      // destride the data
      for(uint64_t i3 = 0;i3<numItems3;++i3) {
         for(uint64_t i2 = 0;i2<numItems2;++i2) {
            for(uint64_t i1 = 0;i1<numItems1;++i1) {
               data.write(charAppMemory + i1*byteStride1 + i2*byteStride2 + i3*byteStride3, element_size);
            }
         }
      }
   }

   out << "ANARIArray3D " << anari::varnameOf(ANARI_ARRAY3D) << reinterpret_cast<uintptr_t>(result) << " = anariNewArray3D(device, ";
   out << "data(" << byte_offset << ", " << byte_size << "), ";
   if(deleter) {
      out << "deleter, deleterData, ";
   } else {
      out << "NULL, NULL, ";
   }
   out << anari::toString(dataType) << ", ";
   out << numItems1 << ", ";
   out << numItems2 << ", ";
   out << numItems3 << ", 0, 0, 0);";
   if(!compact) {
      out << "// original strides: " << byteStride1 << ' ' << byteStride2 << ' ' << byteStride3;
   }
   out << "\n";
}

void CodeSerializer::anariMapArray(ANARIDevice device, ANARIArray array, void *result) {
   out << "void *ptr" << result << " = anariMapArray(device, ";
   printObjectName(array);
   out << ");\n";
}

void CodeSerializer::anariUnmapArray(ANARIDevice device, ANARIArray array) {
   if(auto info = dd->getDynamicObjectInfo<GenericArrayDebugObject>(array)) {
      uint64_t element_size = anari::sizeOf(info->arrayType);
      uint64_t byte_size = element_size*info->numItems1*info->numItems2*info->numItems3;

      if(isObject(info->arrayType)) {
         const ANARIObject *handles = (const ANARIObject*)info->mapping;
         ANARIDataType dataType = info->arrayType;
         uint64_t local = locals++;

         out << "const " << anari::typenameOf(dataType) << " " << anari::varnameOf(dataType) << "_local" << local << "[] = {";
         for(uint64_t i = 0;i<info->numItems1;++i) {
            out << anari::varnameOf(dataType) << reinterpret_cast<uintptr_t>(handles[i]);
            if(i+1<info->numItems1) {
               out << ", ";
            }
         }
         out << "};\n";
         out << "memcpy(ptr" << handles << ", " << anari::varnameOf(dataType) << "_local" << local << ", " << byte_size << ");\n";
      } else {
         uint64_t byte_offset = data.tellp();
         const char *charAppMemory = (const char*)info->mapping;

         bool compact =
            (info->byteStride1 == 0 || info->byteStride1 == element_size) &&
            (info->byteStride2 == 0 || info->byteStride2 == element_size*info->byteStride1) &&
            (info->byteStride3 == 0 || info->byteStride2 == element_size*info->byteStride1*info->byteStride2);

         if(compact) {
            data.write(charAppMemory, byte_size);
         } else {
            // destride the data
            for(uint64_t i3 = 0;i3<info->numItems3;++i3) {
               for(uint64_t i2 = 0;i2<info->numItems2;++i2) {
                  for(uint64_t i1 = 0;i1<info->numItems1;++i1) {
                     data.write(charAppMemory + i1*info->byteStride1 + i2*info->byteStride2 + i3*info->byteStride3, element_size);
                  }
               }
            }
         }

         out << "memcpy(ptr" << charAppMemory << ", data(" << byte_offset << ", " << byte_size << "), " << byte_size << ");\n";
      }
   }

   out << "anariUnmapArray(device, ";
   printObjectName(array);
   out << ");\n";
}

void CodeSerializer::anariNewLight(ANARIDevice device, const char* type, ANARILight result) {
   out << "ANARILight " << anari::varnameOf(ANARI_LIGHT) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewLight(device, \"" << type << "\");\n";
}

void CodeSerializer::anariNewCamera(ANARIDevice device, const char* type, ANARICamera result) {
   out << "ANARICamera " << anari::varnameOf(ANARI_CAMERA) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewCamera(device, \"" << type << "\");\n";
}

void CodeSerializer::anariNewGeometry(ANARIDevice device, const char* type, ANARIGeometry result) {
   out << "ANARIGeometry " << anari::varnameOf(ANARI_GEOMETRY) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewGeometry(device, \"" << type << "\");\n";
}

void CodeSerializer::anariNewSpatialField(ANARIDevice device, const char* type, ANARISpatialField result) {
   out << "ANARISpatialField " << anari::varnameOf(ANARI_SPATIAL_FIELD) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewSpatialField(device, \"" << type << "\");\n";
}

void CodeSerializer::anariNewVolume(ANARIDevice device, const char* type, ANARIVolume result) {
   out << "ANARIVolume " << anari::varnameOf(ANARI_VOLUME) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewVolume(device, \"" << type << "\");\n";
}

void CodeSerializer::anariNewSurface(ANARIDevice device, ANARISurface result) {
   out << "ANARISurface " << anari::varnameOf(ANARI_SURFACE) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewSurface(device);\n";
}

void CodeSerializer::anariNewMaterial(ANARIDevice device, const char* type, ANARIMaterial result) {
   out << "ANARIMaterial " << anari::varnameOf(ANARI_MATERIAL) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewMaterial(device, \"" << type << "\");\n";
}

void CodeSerializer::anariNewSampler(ANARIDevice device, const char* type, ANARISampler result) {
   out << "ANARISampler " << anari::varnameOf(ANARI_SAMPLER) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewSampler(device, \"" << type << "\");\n";
}

void CodeSerializer::anariNewGroup(ANARIDevice device, ANARIGroup result) {
   out << "ANARIGroup " << anari::varnameOf(ANARI_GROUP) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewGroup(device);\n";
}

void CodeSerializer::anariNewInstance(ANARIDevice device, ANARIInstance result) {
   out << "ANARIInstance " << anari::varnameOf(ANARI_INSTANCE) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewInstance(device);\n";
}

void CodeSerializer::anariNewWorld(ANARIDevice device, ANARIWorld result) {
   out << "ANARIWorld " << anari::varnameOf(ANARI_WORLD) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewWorld(device);\n";
}

void CodeSerializer::anariNewObject(ANARIDevice device, const char* objectType, const char* type, ANARIObject result) {
   out << "ANARIObject " << anari::varnameOf(ANARI_OBJECT) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewObject(device, \"" << objectType << "\", \"" << type << "\");\n";
}

void CodeSerializer::anariSetParameter(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, const void *mem) {
   uint64_t local;
   bool isArray = false;
   if(!isObject(dataType) && !(dataType == ANARI_STRING)) {
      local = locals++;
      out << anari::typenameOf(dataType) << ' ' << name << local;
      if(anari::componentsOf(dataType) > 1) {
         out << "[] = {";
         isArray = true;
      } else {
         out << " = ";
      }

      printFromMemory(out, dataType, mem);

      if(isArray) {
         out << "}";
      }
      out << ";\n";
   }

   out << "anariSetParameter(device, ";
   printObjectName(object);
   out << ", \"" << name << "\", ";
   out << anari::toString(dataType) << ", ";
   if(isObject(dataType)) {
      const ANARIObject *handle = (const ANARIObject*)mem;
      if(auto paraminfo = dd->getObjectInfo(*handle)) {
         out << '&' << anari::varnameOf(paraminfo->getType()) << reinterpret_cast<uintptr_t>(*handle);
      } else {
         out << "&unknown_handle";
      }
   } else if(dataType == ANARI_STRING) {
      printFromMemory(out, dataType, mem);
   } else {
      if(!isArray) {
         out << '&';
      }
      out << name << local;
   }
   out << ");\n";
}

void CodeSerializer::anariUnsetParameter(ANARIDevice device, ANARIObject object, const char* name) {
   out << "anariUnsetParameter(device, ";
   printObjectName(object);
   out << ", \"" << name << "\");\n";
}

void CodeSerializer::anariCommitParameters(ANARIDevice device, ANARIObject object) {
   out << "anariCommitParameters(device, ";
   printObjectName(object);
   out << ");\n";
}

void CodeSerializer::anariRelease(ANARIDevice device, ANARIObject object) {
   out << "anariRelease(device, ";
   printObjectName(object);
   out << ");\n";
}

void CodeSerializer::anariRetain(ANARIDevice device, ANARIObject object) {
   out << "anariRetain(device, ";
   printObjectName(object);
   out << ");\n";
}

void CodeSerializer::anariGetProperty(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType type, void* mem, uint64_t size, ANARIWaitMask mask, int result) {
   uint64_t local = locals++;
   out << anari::typenameOf(type) << ' ' << name << local << '[' << anari::componentsOf(type) << "];\n";
   out << "int " << name << local << "_retrieved" << " = anariGetProperty(device, ";
   printObjectName(object);
   out << ", \"" <<  name << "\", " << anari::toString(type) << ", ";
   out << name << local << ", " << size << ", ";
   out << (mask==ANARI_WAIT ? "ANARI_WAIT" : "ANARI_NO_WAIT");
   out << ");";
   if(result) {
      out << "\n// returned ";
      printFromMemory(out, type, mem);
      out << "\n";
   } else {
      out << "\n// no value returned\n";
   }
}

void CodeSerializer::anariNewFrame(ANARIDevice device, ANARIFrame result) {
   out << "ANARIFrame " << anari::varnameOf(ANARI_FRAME) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewFrame(device);\n";
}

void CodeSerializer::anariMapFrame(ANARIDevice device, ANARIFrame object, const char* channel, const void *mapped) {
   uint64_t local = locals++;
   out << "const void *mapped_" << channel << local << " = anariMapFrame(device, ";
   printObjectName(object);
   out << ", \"" << channel << "\");\n";

   if(auto info = dd->getDynamicObjectInfo<DebugObject<ANARI_FRAME>>(object)) {
      ANARIDataType mappingType = ANARI_UNKNOWN;
      if(std::strncmp(channel, "color", 5)==0) {
         mappingType = info->colorType;
      } else if(std::strncmp(channel, "depth", 5)==0) {
         mappingType = info->depthType;
      }
      out << "image(\"" << channel << "\", mapped_" << channel << local << ", ";
      out << info->size[0] << ", " << info->size[1] << ", " << anari::toString(mappingType) << ");\n";
   }
}

void CodeSerializer::anariUnmapFrame(ANARIDevice device, ANARIFrame object, const char* channel) {
   out << "anariUnmapFrame(device, ";
   printObjectName(object);
   out << ", \"" << channel << "\");\n";

}

void CodeSerializer::anariNewRenderer(ANARIDevice device, const char* type, ANARIRenderer result) {
   out << "ANARIRenderer " << anari::varnameOf(ANARI_RENDERER) << reinterpret_cast<uintptr_t>(result);
   out << " = anariNewRenderer(device, \"" << type << "\");\n";
}

void CodeSerializer::anariRenderFrame(ANARIDevice device, ANARIFrame object) {
   out << "anariRenderFrame(device, ";
   printObjectName(object);
   out << ");\n";
}

void CodeSerializer::anariFrameReady(ANARIDevice device, ANARIFrame object, ANARIWaitMask mask, int result) {
   out << "anariFrameReady(device, ";
   printObjectName(object);
   out << ", " << (mask==ANARI_WAIT ? "ANARI_WAIT" : "ANARI_NO_WAIT");
   out << ");\n";
}

void CodeSerializer::anariDiscardFrame(ANARIDevice device, ANARIFrame object) {
   out << "anariDiscardFrame(device, ";
   printObjectName(object);
   out << ");\n";
}

void CodeSerializer::anariReleaseDevice(ANARIDevice device) {

}



}
}