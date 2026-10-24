// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureWriter.h"

#include "CaptureFormat.h"
#include "CaptureObject.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/array/Array2D.h"
#include "helium/array/Array3D.h"
#include "helium/array/ObjectArray.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace capture_device {

namespace {

constexpr const char *kHeader = "anari-capture 1";

bool isPointerLike(ANARIDataType type)
{
  switch (type) {
  case ANARI_VOID_POINTER:
  case ANARI_FUNCTION_POINTER:
  case ANARI_MEMORY_DELETER:
  case ANARI_STATUS_CALLBACK:
  case ANARI_FRAME_COMPLETION_CALLBACK:
    return true;
  default:
    return false;
  }
}

const helium::BaseObject *objectOf(const helium::AnariAny &value)
{
  return anari::isObject(value.type()) ? value.getObject<helium::BaseObject>()
                                       : nullptr;
}

class Writer
{
 public:
  explicit Writer(const WriterOptions &options) : m_options(options) {}

  Capture write(const helium::BaseObject &frame)
  {
    visit(&frame);

    std::string &out = m_capture.text;
    out += kHeader;
    out += "\n\n";
    writeDeviceBlock();
    for (auto *obj : m_order) {
      out += '\n';
      writeObject(obj);
    }
    return std::move(m_capture);
  }

 private:
  // Assigns IDs depth-first, parameters in name order, object arrays in
  // element order. m_order is the order the blocks are written in.
  void visit(const helium::BaseObject *obj)
  {
    if (m_ids.count(obj))
      return;
    m_ids[obj] = std::string(objectLabel(obj->type())) + "@"
        + std::to_string(m_next[obj->type()]++);
    m_order.push_back(obj);

    for (const auto *param : sortedParams(obj)) {
      if (auto *child = objectOf(param->second))
        visit(child);
    }
    if (auto *array = objectArrayOf(obj)) {
      for (auto **h = array->handlesBegin(); h != array->handlesEnd(); ++h) {
        if (*h)
          visit(*h);
      }
    }
  }

  static std::vector<const CommittedParams::List::value_type *> sortedParams(
      const helium::BaseObject *obj)
  {
    std::vector<const CommittedParams::List::value_type *> sorted;
    if (auto *cp = dynamic_cast<const CommittedParams *>(obj)) {
      for (const auto &p : cp->committedParams())
        sorted.push_back(&p);
    }
    std::sort(sorted.begin(), sorted.end(), [](auto *a, auto *b) {
      return a->first < b->first;
    });
    return sorted;
  }

  static const helium::ObjectArray *objectArrayOf(const helium::BaseObject *obj)
  {
    auto *array = dynamic_cast<const helium::Array *>(obj);
    return array && anari::isObject(array->elementType())
        ? static_cast<const helium::ObjectArray *>(array)
        : nullptr;
  }

  std::string idOf(const helium::BaseObject *obj) const
  {
    return m_ids.at(obj);
  }

  void writeDeviceBlock()
  {
    std::string &out = m_capture.text;
    out += "device {\n";
    out += "  extensions ANARI_STRING_LIST ";
    writeStringList(m_options.extensions, "  ");
    out += "}\n";
  }

  // '[' with one quoted string per line, or '[]' when empty
  void writeStringList(const std::vector<std::string> &list, const char *indent)
  {
    std::string &out = m_capture.text;
    if (list.empty()) {
      out += "[]\n";
      return;
    }
    out += "[\n";
    for (const auto &s : list)
      out += std::string(indent) + "  " + quoteString(s) + "\n";
    out += std::string(indent) + "]\n";
  }

  void writeObject(const helium::BaseObject *obj)
  {
    if (auto *array = dynamic_cast<const helium::Array *>(obj)) {
      writeArray(obj, array);
      return;
    }

    std::string &out = m_capture.text;
    out += idOf(obj);
    if (auto *cp = dynamic_cast<const CommittedParams *>(obj)) {
      if (!cp->subtype().empty())
        out += " " + quoteString(cp->subtype());
    }
    out += " {\n";
    for (const auto *param : sortedParams(obj))
      writeParam(param->first, param->second);
    out += "}\n";
  }

  void writeParam(const std::string &name, const helium::AnariAny &value)
  {
    std::string &out = m_capture.text;
    const ANARIDataType type = value.type();
    out += "  " + name + " " + anari::toString(type) + " ";

    if (anari::isObject(type)) {
      auto *child = objectOf(value);
      out += child ? idOf(child) : "null";
      out += "\n";
    } else if (type == ANARI_STRING) {
      out += quoteString(value.getString()) + "\n";
    } else if (type == ANARI_STRING_LIST) {
      writeStringList(value.getStringList(), "  ");
    } else if (type == ANARI_DATA_TYPE) {
      ANARIDataType t;
      std::memcpy(&t, value.data(), sizeof(t));
      out += std::string(anari::toString(t)) + "\n";
    } else if (isPointerLike(type)) {
      out += "<opaque>\n";
    } else {
      out += formatElement(elementLayout(type), (const uint8_t *)value.data())
          + "\n";
    }
  }

  void writeArray(const helium::BaseObject *obj, const helium::Array *array)
  {
    std::string &out = m_capture.text;
    const ANARIDataType elementType = array->elementType();

    std::vector<uint64_t> dims;
    const uint8_t *bytes = nullptr;
    if (auto *objects = objectArrayOf(obj)) {
      dims = {objects->size()};
    } else if (auto *a1 = dynamic_cast<const helium::Array1D *>(array)) {
      dims = {a1->size()};
      bytes = (const uint8_t *)a1->begin();
    } else if (auto *a2 = dynamic_cast<const helium::Array2D *>(array)) {
      auto s = a2->size();
      dims = {s.x, s.y};
      bytes = (const uint8_t *)a2->data();
    } else if (auto *a3 = dynamic_cast<const helium::Array3D *>(array)) {
      auto s = a3->size();
      dims = {s.x, s.y, s.z};
      bytes = (const uint8_t *)a3->data();
    }

    out += idOf(obj) + " {\n";
    out += std::string("  elementType ") + anari::toString(elementType) + "\n";
    out += "  dimensions (";
    for (size_t i = 0; i < dims.size(); ++i)
      out += (i ? ", " : "") + std::to_string(dims[i]);
    out += ")\n";

    uint64_t count = 1;
    for (auto d : dims)
      count *= d;

    if (auto *objects = objectArrayOf(obj)) {
      out += "  data [\n";
      for (auto **h = objects->handlesBegin(); h != objects->handlesEnd(); ++h)
        out += "    " + (*h ? idOf(*h) : std::string("null")) + "\n";
      out += "  ]\n";
    } else if (count <= m_options.inlineArrayLimit) {
      const auto layout = elementLayout(elementType);
      const size_t elementBytes = anari::sizeOf(elementType);
      out += "  data [\n";
      for (uint64_t i = 0; i < count; ++i)
        out += "    " + formatElement(layout, bytes + i * elementBytes) + "\n";
      out += "  ]\n";
    } else {
      writeSummaryAndBlob(elementType, dims, count, bytes);
    }
    out += "}\n";
  }

  // The element count, the smallest and largest value of each component, and
  // the name of the blob holding the bytes
  void writeSummaryAndBlob(ANARIDataType elementType,
      const std::vector<uint64_t> &dims,
      uint64_t count,
      const uint8_t *bytes)
  {
    std::string &out = m_capture.text;
    const auto layout = elementLayout(elementType);
    const size_t elementBytes = anari::sizeOf(elementType);

    out += "  count " + std::to_string(count) + "\n";
    if (layout.kind != ScalarKind::OPAQUE && layout.kind != ScalarKind::BOOL
        && count > 0) {
      std::vector<Scalar> lo, hi;
      for (size_t c = 0; c < layout.components; ++c) {
        lo.push_back(readScalar(layout, bytes, c));
        hi.push_back(lo.back());
      }
      for (uint64_t i = 1; i < count; ++i) {
        for (size_t c = 0; c < layout.components; ++c) {
          const auto v = readScalar(layout, bytes + i * elementBytes, c);
          if (less(v, lo[c]))
            lo[c] = v;
          if (less(hi[c], v))
            hi[c] = v;
        }
      }
      out += "  min " + formatTuple(lo) + "\n";
      out += "  max " + formatTuple(hi) + "\n";
    }

    const size_t numBytes = count * elementBytes;
    const auto name = contentHash(elementType, dims, bytes, numBytes);
    m_capture.blobs[name].assign(bytes, bytes + numBytes);
    out += "  blob " + name + "\n";
  }

  // Orders scalars of one kind; a NaN is never the smallest or the largest
  // unless every value is one
  static bool less(const Scalar &a, const Scalar &b)
  {
    switch (a.kind) {
    case ScalarKind::FLOAT:
      return std::isnan(b.f) ? !std::isnan(a.f) : a.f < b.f;
    case ScalarKind::INT:
      return a.i < b.i;
    default:
      return a.u < b.u;
    }
  }

  const WriterOptions &m_options;
  Capture m_capture;
  std::vector<const helium::BaseObject *> m_order;
  std::unordered_map<const helium::BaseObject *, std::string> m_ids;
  std::map<ANARIDataType, int> m_next;
};

} // namespace

Capture writeCapture(const helium::BaseObject &frame, const WriterOptions &o)
{
  return Writer(o).write(frame);
}

} // namespace capture_device
