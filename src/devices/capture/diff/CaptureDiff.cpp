// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/ext/capture/CaptureDiff.h"

#include "CaptureModel.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace anari {
namespace capture_diff {

using namespace detail;

// Difference and DiffResult //////////////////////////////////////////////////

std::string Difference::message() const
{
  if (!note.empty())
    return path + ": " + note;
  return path + ": expected " + expected + ", got " + actual;
}

std::string DiffResult::report() const
{
  if (outcome == Outcome::INVALID)
    return "error: " + error + "\n";
  if (outcome == Outcome::MATCH)
    return "the captures match\n";

  std::string out;
  for (const auto &d : differences)
    out += d.message() + "\n";
  const auto plural = [](size_t n) {
    return std::to_string(n) + (n == 1 ? " difference" : " differences");
  };
  if (totalDifferences > differences.size()) {
    out += "... and " + std::to_string(totalDifferences - differences.size())
        + (totalDifferences - differences.size() == 1 ? " more difference"
                                                      : " more differences")
        + " (" + plural(totalDifferences) + " in total)\n";
  } else {
    out += plural(totalDifferences) + "\n";
  }
  return out;
}

namespace {

// A capture that can't be compared: its input is bad, not different
struct InvalidCapture : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

// Floats ///////////////////////////////////////////////////////////////////

bool parseFloat(const std::string &token, double &value)
{
  if (token == "nan") {
    value = std::nan("");
    return true;
  }
  if (token == "inf" || token == "-inf") {
    value = token[0] == '-' ? -INFINITY : INFINITY;
    return true;
  }
  std::istringstream in(token);
  in.imbue(std::locale::classic());
  in >> value;
  return !in.fail();
}

template <typename Bits>
uint64_t orderedBits(Bits bits)
{
  const Bits sign = Bits(1) << (sizeof(Bits) * 8 - 1);
  return (bits & sign) ? uint64_t(Bits(~bits)) : uint64_t(bits | sign);
}

// How many representable values apart two floats are
uint64_t ulpDistance(double a, double b, bool isDouble)
{
  uint64_t ka, kb;
  if (isDouble) {
    uint64_t ba, bb;
    std::memcpy(&ba, &a, 8);
    std::memcpy(&bb, &b, 8);
    ka = orderedBits(ba);
    kb = orderedBits(bb);
  } else {
    const float fa = float(a), fb = float(b);
    uint32_t ba, bb;
    std::memcpy(&ba, &fa, 4);
    std::memcpy(&bb, &fb, 4);
    ka = orderedBits(ba);
    kb = orderedBits(bb);
  }
  return ka > kb ? ka - kb : kb - ka;
}

bool floatsMatch(
    double a, double b, bool isDouble, const FloatTolerance &tolerance)
{
  if (std::isnan(a) || std::isnan(b))
    return std::isnan(a) && std::isnan(b);
  if (a == b)
    return true;
  if (std::isinf(a) || std::isinf(b))
    return false;
  const double distance = std::fabs(a - b);
  if (distance <= tolerance.absolute)
    return true;
  if (distance <= tolerance.relative * std::max(std::fabs(a), std::fabs(b)))
    return true;
  return tolerance.ulps && ulpDistance(a, b, isDouble) <= tolerance.ulps;
}

// Array elements: from text or from a blob //////////////////////////////////

struct Scalar
{
  double f{0.0};
  int64_t i{0};
  uint64_t u{0};
};

class Elements
{
 public:
  Layout layout;
  std::string typeName;
  uint64_t count{0};

  // Elements listed in the text
  static Elements fromText(
      const std::string &typeName, const std::vector<std::string> &items)
  {
    Elements e;
    e.typeName = typeName;
    e.layout = layoutOf(typeName);
    e.count = items.size();
    e.texts = items;
    return e;
  }

  // Elements in a blob's bytes
  static Elements fromBytes(
      const std::string &typeName, uint64_t count, std::vector<uint8_t> bytes)
  {
    Elements e;
    e.typeName = typeName;
    e.layout = layoutOf(typeName);
    e.count = count;
    e.bytes = std::move(bytes);
    e.isBlob = true;
    return e;
  }

  bool numeric() const
  {
    return layout.kind != Layout::OTHER && layout.components > 0;
  }

  // The components of element 'i' as they are, or false if the text of the
  // element does not read as numbers
  bool components(uint64_t i, std::vector<Scalar> &out) const
  {
    out.assign(layout.components, Scalar());
    if (isBlob) {
      const uint8_t *p = bytes.data() + i * layout.elementBytes();
      for (size_t c = 0; c < layout.components; ++c)
        out[c] = decode(p + c * layout.componentBytes);
      return true;
    }
    const auto tokens = splitTuple(texts[i]);
    if (tokens.size() != layout.components)
      return false;
    for (size_t c = 0; c < tokens.size(); ++c) {
      if (!parse(tokens[c], out[c]))
        return false;
    }
    return true;
  }

  std::string text(uint64_t i) const
  {
    if (!isBlob)
      return texts[i];
    std::vector<Scalar> parts;
    components(i, parts);
    std::string out = parts.size() > 1 ? "(" : "";
    for (size_t c = 0; c < parts.size(); ++c)
      out += (c ? ", " : "") + format(parts[c]);
    return out + (parts.size() > 1 ? ")" : "");
  }

 private:
  Scalar decode(const uint8_t *p) const
  {
    Scalar s;
    const size_t n = layout.componentBytes;
    switch (layout.kind) {
    case Layout::FLOAT:
      if (n == 4) {
        float v;
        std::memcpy(&v, p, 4);
        s.f = v;
      } else if (n == 8) {
        std::memcpy(&s.f, p, 8);
      } else {
        uint16_t h;
        std::memcpy(&h, p, 2);
        s.f = halfToDouble(h);
      }
      break;
    case Layout::INT: {
      int64_t v = 0;
      std::memcpy(&v, p, n); // little-endian hosts
      if (n < 8 && (v & (int64_t(1) << (n * 8 - 1))))
        v -= int64_t(1) << (n * 8);
      s.i = v;
      break;
    }
    default: {
      uint64_t v = 0;
      std::memcpy(&v, p, n);
      s.u = v;
    }
    }
    return s;
  }

  static double halfToDouble(uint16_t h)
  {
    const int exponent = (h >> 10) & 0x1f;
    const int mantissa = h & 0x3ff;
    double v;
    if (exponent == 0)
      v = std::ldexp(double(mantissa), -24);
    else if (exponent == 31)
      v = mantissa ? std::nan("") : INFINITY;
    else
      v = std::ldexp(double(mantissa | 0x400), exponent - 25);
    return (h & 0x8000) ? -v : v;
  }

  bool parse(const std::string &token, Scalar &s) const
  {
    switch (layout.kind) {
    case Layout::FLOAT:
      return parseFloat(token, s.f);
    case Layout::INT:
      s.i = std::strtoll(token.c_str(), nullptr, 10);
      return !token.empty();
    case Layout::BOOL:
      s.u = token == "true";
      return token == "true" || token == "false";
    default:
      s.u = std::strtoull(token.c_str(), nullptr, 10);
      return !token.empty();
    }
  }

  std::string format(const Scalar &s) const
  {
    switch (layout.kind) {
    case Layout::FLOAT:
      return formatFloat(s.f, layout.componentBytes == 8);
    case Layout::INT:
      return std::to_string(s.i);
    case Layout::BOOL:
      return s.u ? "true" : "false";
    default:
      return std::to_string(s.u);
    }
  }

  std::vector<std::string> texts;
  std::vector<uint8_t> bytes;
  bool isBlob{false};
};

// The path of an array parameter against a path from the options ///////////

struct Segment
{
  std::string name;
  bool hasIndex{false}; // an index other than '*'
  long index{0};
};

std::vector<Segment> segments(const std::string &path)
{
  std::vector<Segment> out;
  size_t at = 0;
  while (at <= path.size()) {
    auto end = path.find('.', at);
    if (end == std::string::npos)
      end = path.size();
    Segment s;
    s.name = path.substr(at, end - at);
    const auto bracket = s.name.find('[');
    if (bracket != std::string::npos) {
      const std::string inside =
          s.name.substr(bracket + 1, s.name.size() - bracket - 2);
      if (inside != "*") {
        s.hasIndex = true;
        s.index = std::atol(inside.c_str());
      }
      s.name.erase(bracket);
    }
    out.push_back(s);
    at = end + 1;
  }
  return out;
}

bool pathMatches(const std::string &pattern, const std::string &path)
{
  const auto p = segments(pattern);
  const auto s = segments(path);
  if (p.size() != s.size())
    return false;
  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i].name != s[i].name)
      return false;
    if (p[i].hasIndex && (!s[i].hasIndex || p[i].index != s[i].index))
      return false;
  }
  return true;
}

std::string join(const std::string &path, const std::string &name)
{
  return path.empty() ? name : path + "." + name;
}

std::string describeEntry(const Entry &e)
{
  if (e.isList) {
    std::string out = "[";
    for (size_t i = 0; i < e.items.size(); ++i)
      out += (i ? ", " : "") + e.items[i];
    return e.type + " " + out + "]";
  }
  return e.type + " " + e.value;
}

// The comparison ///////////////////////////////////////////////////////////

class Differ
{
 public:
  Differ(const Capture &expected,
      const Model &expectedModel,
      const Capture &actual,
      const Model &actualModel,
      const DiffOptions &options,
      DiffResult &result)
      : m_capture{&expected, &actual},
        m_model{&expectedModel, &actualModel},
        m_options(options),
        m_result(result)
  {}

  void run()
  {
    compareDevice();
    compareRef("", m_model[0]->frameId, m_model[1]->frameId);
    compareWarnings();
  }

 private:
  // Reporting //

  void add(const std::string &path,
      const std::string &expected,
      const std::string &actual)
  {
    if (m_probing) {
      ++m_probeDifferences;
      return;
    }
    ++m_result.totalDifferences;
    if (m_result.differences.size() < m_options.maxDifferences)
      m_result.differences.push_back({path, expected, actual, ""});
  }

  // A difference said in words, which counts as one
  void addNote(const std::string &path, const std::string &note)
  {
    if (m_probing) {
      ++m_probeDifferences;
      return;
    }
    ++m_result.totalDifferences;
    if (m_result.differences.size() < m_options.maxDifferences)
      m_result.differences.push_back({path, "", "", note});
  }

  // A line that says more of a difference already counted
  void addSummary(const std::string &path, const std::string &note)
  {
    if (!m_probing && m_result.differences.size() < m_options.maxDifferences)
      m_result.differences.push_back({path, "", "", note});
  }

  // Device and warnings //

  static std::vector<std::string> itemsOf(const Block &b, const char *name)
  {
    const Entry *e = b.find(name);
    return e ? e->items : std::vector<std::string>{};
  }

  void compareDevice()
  {
    auto a = itemsOf(m_model[0]->device, "extensions");
    auto b = itemsOf(m_model[1]->device, "extensions");
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    std::vector<std::string> onlyA, onlyB;
    std::set_difference(
        a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(onlyA));
    std::set_difference(
        b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(onlyB));
    for (const auto &e : onlyA)
      add("device.extensions", e, "<absent>");
    for (const auto &e : onlyB)
      add("device.extensions", "<absent>", e);
  }

  void compareWarnings()
  {
    const auto &a = m_model[0]->warnings.entries;
    const auto &b = m_model[1]->warnings.entries;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
      const std::string path = "warnings[" + std::to_string(i) + "]";
      const std::string ea = i < a.size() ? a[i].value : "<absent>";
      const std::string eb = i < b.size() ? b[i].value : "<absent>";
      if (ea != eb)
        add(path, ea, eb);
    }
  }

  // Objects //

  const Block &block(int side, const std::string &id) const
  {
    auto it = m_model[side]->blocks.find(id);
    if (it == m_model[side]->blocks.end()) {
      throw InvalidCapture(std::string(side ? "the actual" : "the expected")
          + " capture refers to '" + id + "', which it does not define");
    }
    return it->second;
  }

  void pair(const std::string &a, const std::string &b, const std::string &path)
  {
    m_aToB[a] = b;
    m_bToA[b] = a;
    if (!m_firstPath.count(a))
      m_firstPath[a] = path;
    if (!m_firstPathB.count(b))
      m_firstPathB[b] = path;
    m_journal.push_back({a, b});
  }

  void rollback(size_t mark)
  {
    while (m_journal.size() > mark) {
      m_aToB.erase(m_journal.back().first);
      m_bToA.erase(m_journal.back().second);
      m_journal.pop_back();
    }
  }

  std::string describe(int side, const std::string &id) const
  {
    const Block &b = block(side, id);
    const bool vowel = !b.label.empty() && std::strchr("aeiou", b.label[0]);
    return std::string(vowel ? "an " : "a ") + b.label
        + (b.hasSubtype ? " " + b.subtype : "");
  }

  // A reference to an object in each capture, at 'path'
  void compareRef(
      const std::string &path, const std::string &a, const std::string &b)
  {
    if (a == "null" || b == "null") {
      if (a != b) {
        add(path,
            a == "null" ? "null" : describe(0, a),
            b == "null" ? "null" : describe(1, b));
      }
      return;
    }

    auto pairedA = m_aToB.find(a);
    if (pairedA != m_aToB.end()) {
      if (pairedA->second != b) {
        addNote(path,
            "expected the object also at " + displayPath(m_firstPath[a])
                + ", got another object");
      }
      return;
    }
    if (m_bToA.count(b)) {
      addNote(path,
          "expected another object, got the object also at "
              + displayPath(m_firstPathB[b]));
      return;
    }

    pair(a, b, path);
    compareObject(path, block(0, a), block(1, b));
  }

  static std::string displayPath(const std::string &path)
  {
    return path.empty() ? "the frame" : path;
  }

  void compareObject(const std::string &path, const Block &a, const Block &b)
  {
    if (a.isArray()) {
      compareArray(path, a, b);
      return;
    }

    if (a.hasSubtype != b.hasSubtype || a.subtype != b.subtype) {
      add(join(path, "@subtype"),
          a.hasSubtype ? a.subtype : "<none>",
          b.hasSubtype ? b.subtype : "<none>");
    }

    std::set<std::string> names;
    for (const auto &e : a.entries)
      names.insert(e.name);
    for (const auto &e : b.entries)
      names.insert(e.name);
    for (const auto &name : names)
      compareParam(join(path, name), a.find(name), b.find(name));
  }

  void compareParam(const std::string &path, const Entry *a, const Entry *b)
  {
    if (!a || !b) {
      add(path,
          a ? describeEntry(*a) : "<absent>",
          b ? describeEntry(*b) : "<absent>");
      return;
    }
    if (a->type != b->type) {
      add(path, "type " + a->type, b->type);
      return;
    }

    if (isObjectType(a->type)) {
      compareRef(path, a->value, b->value);
    } else if (a->isList || b->isList) {
      if (a->items != b->items)
        add(path, describeEntry(*a), describeEntry(*b));
    } else if (a->type.rfind("ANARI_FLOAT", 0) == 0) {
      if (!tuplesMatch(a->type, a->value, b->value))
        add(path, a->value, b->value);
    } else if (a->value != b->value) {
      add(path, a->value, b->value);
    }
  }

  // Whether two values of a float type, as scalars or tuples, match
  bool tuplesMatch(
      const std::string &type, const std::string &a, const std::string &b) const
  {
    const auto ta = splitTuple(a);
    const auto tb = splitTuple(b);
    if (ta.size() != tb.size())
      return false;
    const bool isDouble = layoutOf(type).componentBytes == 8;
    for (size_t i = 0; i < ta.size(); ++i) {
      double fa, fb;
      if (!parseFloat(ta[i], fa) || !parseFloat(tb[i], fb))
        return ta[i] == tb[i];
      if (!floatsMatch(fa, fb, isDouble, m_options.tolerance))
        return false;
    }
    return true;
  }

  // Arrays //

  bool isUnordered(const std::string &path) const
  {
    return std::any_of(m_options.unorderedPaths.begin(),
        m_options.unorderedPaths.end(),
        [&](const std::string &pattern) { return pathMatches(pattern, path); });
  }

  static std::string fieldOf(const Block &b, const char *name)
  {
    const Entry *e = b.find(name);
    return e ? e->value : "";
  }

  static uint64_t countOf(const std::string &dimensions)
  {
    uint64_t n = 1;
    for (const auto &d : splitTuple(dimensions))
      n *= std::strtoull(d.c_str(), nullptr, 10);
    return n;
  }

  void compareArray(const std::string &path, const Block &a, const Block &b)
  {
    const std::string typeA = fieldOf(a, "elementType");
    const std::string typeB = fieldOf(b, "elementType");
    if (typeA != typeB) {
      add(join(path, "@elementType"), typeA, typeB);
      return;
    }
    const std::string dimsA = fieldOf(a, "dimensions");
    const std::string dimsB = fieldOf(b, "dimensions");
    if (dimsA != dimsB) {
      add(join(path, "@dimensions"), dimsA, dimsB);
      return;
    }

    if (isObjectType(typeA)) {
      const Entry *da = a.find("data");
      const Entry *db = b.find("data");
      const std::vector<std::string> empty;
      const auto &ia = da ? da->items : empty;
      const auto &ib = db ? db->items : empty;
      if (isUnordered(path)) {
        compareUnordered(path, ia, ib);
      } else {
        for (size_t i = 0; i < std::min(ia.size(), ib.size()); ++i)
          compareRef(path + "[" + std::to_string(i) + "]", ia[i], ib[i]);
      }
      return;
    }

    // Blobs are named by their content: the same name is the same elements
    const Entry *blobA = a.find("blob");
    const Entry *blobB = b.find("blob");
    if (blobA && blobB && blobA->value == blobB->value)
      return;

    const uint64_t count = countOf(dimsA);
    compareElements(
        path, elementsOf(0, a, typeA, count), elementsOf(1, b, typeA, count));
  }

  Elements elementsOf(int side,
      const Block &b,
      const std::string &typeName,
      uint64_t count) const
  {
    if (const Entry *blob = b.find("blob")) {
      const Layout layout = layoutOf(typeName);
      std::vector<uint8_t> bytes = loadBlob(side, blob->value);
      if (bytes.size() != count * layout.elementBytes()) {
        throw InvalidCapture("the blob '" + blob->value + "' has "
            + std::to_string(bytes.size()) + " bytes, not the "
            + std::to_string(count * layout.elementBytes())
            + " its array needs");
      }
      return Elements::fromBytes(typeName, count, std::move(bytes));
    }
    const Entry *data = b.find("data");
    return Elements::fromText(
        typeName, data ? data->items : std::vector<std::string>{});
  }

  std::vector<uint8_t> loadBlob(int side, const std::string &name) const
  {
    const Capture &c = *m_capture[side];
    auto inMemory = c.blobs.find(name);
    if (inMemory != c.blobs.end())
      return inMemory->second;
    if (!c.blobDirectory.empty()) {
      const auto path =
          std::filesystem::path(c.blobDirectory) / (name + ".bin");
      std::ifstream in(path, std::ios::binary);
      if (in) {
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
      }
    }
    throw InvalidCapture(std::string("the blob '") + name + "' of the "
        + (side ? "actual" : "expected") + " capture is missing");
  }

  void compareElements(
      const std::string &path, const Elements &a, const Elements &b)
  {
    uint64_t differing = 0;
    uint64_t listed = 0;
    std::vector<Scalar> pa, pb;
    const uint64_t count = std::min(a.count, b.count);
    const bool isFloat = a.layout.kind == Layout::FLOAT;
    const bool isDouble = isFloat && a.layout.componentBytes == 8;

    for (uint64_t i = 0; i < count; ++i) {
      bool same;
      if (!a.numeric() || !a.components(i, pa) || !b.components(i, pb)) {
        same = a.text(i) == b.text(i);
      } else {
        same = true;
        for (size_t c = 0; c < pa.size() && same; ++c) {
          switch (a.layout.kind) {
          case Layout::FLOAT:
            same = floatsMatch(pa[c].f, pb[c].f, isDouble, m_options.tolerance);
            break;
          case Layout::INT:
            same = pa[c].i == pb[c].i;
            break;
          default:
            same = pa[c].u == pb[c].u;
          }
        }
      }
      if (same)
        continue;
      ++differing;
      if (listed < m_options.maxIndicesPerArray) {
        ++listed;
        add(path + "[" + std::to_string(i) + "]", a.text(i), b.text(i));
      } else if (m_probing) {
        ++m_probeDifferences;
      } else {
        ++m_result.totalDifferences;
      }
    }
    if (differing > listed) {
      addSummary(
          path, std::to_string(differing - listed) + " more elements differ");
    }
  }

  // Unordered arrays of objects //

  uint64_t structuralHash(int side, const std::string &id)
  {
    if (id == "null")
      return 0x9e3779b97f4a7c15ull;
    auto &memo = m_hashes[side];
    auto found = memo.find(id);
    if (found != memo.end())
      return found->second;
    memo[id] = 0; // an object that refers to itself hashes as nothing

    const Block &b = block(side, id);
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](const std::string &s) {
      for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
      }
      h ^= 0xff;
      h *= 0x100000001b3ull;
    };
    auto mixHash = [&](uint64_t v) { mix(std::to_string(v)); };

    mix(b.label);
    mix(b.hasSubtype ? b.subtype : "");
    std::vector<const Entry *> sorted;
    for (const auto &e : b.entries)
      sorted.push_back(&e);
    std::sort(sorted.begin(), sorted.end(), [](auto *x, auto *y) {
      return x->name < y->name;
    });
    for (const Entry *e : sorted) {
      mix(e->name);
      mix(e->type);
      if (isObjectType(e->type)) {
        mixHash(structuralHash(side, e->value));
      } else if (e->isList) {
        const bool objects =
            b.isArray() && isObjectType(fieldOf(b, "elementType"));
        for (const auto &item : e->items) {
          if (objects)
            mixHash(structuralHash(side, item));
          else
            mix(item);
        }
      } else {
        mix(e->value);
      }
    }
    memo[id] = h;
    return h;
  }

  // Whether the objects have no differences, leaving nothing paired
  bool equalObjects(
      const std::string &path, const std::string &a, const std::string &b)
  {
    const size_t mark = m_journal.size();
    const bool wasProbing = m_probing;
    const size_t before = m_probeDifferences;
    m_probing = true;
    compareRef(path, a, b);
    const bool equal = m_probeDifferences == before;
    m_probing = wasProbing;
    rollback(mark);
    return equal;
  }

  void compareUnordered(const std::string &path,
      const std::vector<std::string> &a,
      const std::vector<std::string> &b)
  {
    const auto indexed = [&](size_t i) {
      return path + "[" + std::to_string(i) + "]";
    };
    std::vector<bool> usedA(a.size(), false), usedB(b.size(), false);

    // First by structural hash
    std::unordered_multimap<uint64_t, size_t> byHash;
    for (size_t j = 0; j < b.size(); ++j)
      byHash.emplace(structuralHash(1, b[j]), j);
    for (size_t i = 0; i < a.size(); ++i) {
      const auto range = byHash.equal_range(structuralHash(0, a[i]));
      for (auto it = range.first; it != range.second; ++it) {
        if (usedB[it->second])
          continue;
        usedA[i] = usedB[it->second] = true;
        compareRef(indexed(i), a[i], b[it->second]);
        break;
      }
    }

    // Then the rest by comparing them, which lets floats within the tolerance
    // pair
    for (size_t i = 0; i < a.size(); ++i) {
      if (usedA[i])
        continue;
      for (size_t j = 0; j < b.size(); ++j) {
        if (usedB[j] || !equalObjects(indexed(i), a[i], b[j]))
          continue;
        usedA[i] = usedB[j] = true;
        compareRef(indexed(i), a[i], b[j]);
        break;
      }
    }

    for (size_t i = 0; i < a.size(); ++i) {
      if (!usedA[i])
        addNote(
            indexed(i), "no matching element, expected " + describe(0, a[i]));
    }
    for (size_t j = 0; j < b.size(); ++j) {
      if (!usedB[j])
        addNote(indexed(j), "unexpected element, " + describe(1, b[j]));
    }
  }

  const Capture *m_capture[2];
  const Model *m_model[2];
  const DiffOptions &m_options;
  DiffResult &m_result;

  // Which object of the other capture each one is paired with, and where each
  // was first reached
  std::unordered_map<std::string, std::string> m_aToB, m_bToA;
  std::unordered_map<std::string, std::string> m_firstPath, m_firstPathB;
  std::vector<std::pair<std::string, std::string>> m_journal;
  std::unordered_map<std::string, uint64_t> m_hashes[2];

  // While probing, differences are counted and not reported
  bool m_probing{false};
  size_t m_probeDifferences{0};
};

} // namespace

// Entry points /////////////////////////////////////////////////////////////

DiffResult diffCaptures(
    const Capture &expected, const Capture &actual, const DiffOptions &options)
{
  DiffResult result;

  Model expectedModel, actualModel;
  std::string error;
  if (!parseCapture(expected.text, expectedModel, error)) {
    result.outcome = Outcome::INVALID;
    result.error = "the expected capture: " + error;
    return result;
  }
  if (!parseCapture(actual.text, actualModel, error)) {
    result.outcome = Outcome::INVALID;
    result.error = "the actual capture: " + error;
    return result;
  }

  try {
    Differ(expected, expectedModel, actual, actualModel, options, result).run();
  } catch (const InvalidCapture &e) {
    result = DiffResult();
    result.outcome = Outcome::INVALID;
    result.error = e.what();
    return result;
  }

  if (result.totalDifferences)
    result.outcome = Outcome::DIFFER;
  return result;
}

bool readCaptureFile(
    const std::string &path, Capture &capture, std::string &error)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot read '" + path + "'";
    return false;
  }
  capture.text.assign(
      std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  capture.blobs.clear();
  capture.blobDirectory =
      (std::filesystem::path(path).parent_path() / "blobs").string();
  return true;
}

DiffResult diffCaptureFiles(const std::string &expectedPath,
    const std::string &actualPath,
    const DiffOptions &options)
{
  Capture expected, actual;
  DiffResult result;
  std::string error;
  if (!readCaptureFile(expectedPath, expected, error)
      || !readCaptureFile(actualPath, actual, error)) {
    result.outcome = Outcome::INVALID;
    result.error = error;
    return result;
  }
  return diffCaptures(expected, actual, options);
}

} // namespace capture_diff
} // namespace anari
