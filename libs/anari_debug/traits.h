#pragma once

#include "DebugDevice.h"
#include "anari/anari.h"

template<typename T>
struct validation_trait {
  static void validate(DebugDevice *dd, const T &val) { (void)dd; (void)val; }
};

template<>
struct validation_trait<ANARIObject> {
  static void validate(DebugDevice *dd, const ANARIObject &val) {
    if(ObjectData *data = dd->getObject(val)) {
      if(data->getRefcount() <= 0) {
        dd->reportStatus("use of released handle %p (%s)", val, data->getName().c_str());
      }
    } else {
      dd->reportStatus("unknown handle %p %p", val, dd);
    }
  }
};
