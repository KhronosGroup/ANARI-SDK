// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The `cts` conformance tool: a self-contained C++ command line that renders
// and scores the catalog against generated ground truth (ADR-0001). Python only
// ever reads the results tree it writes.

#include "cts/BuiltinTests.h"
#include "cts/Catalog.h"
#include "cts/Expansion.h"
#include "cts/HtmlReport.h"
#include "cts/RendererParams.h"
#include "cts/Report.h"
#include "cts/Runner.h"
#include "cts/Workdir.h"
// anari
#include "anari/anari_cpp.hpp"
#include "anari/frontend/anari_device_introspection.hpp"
// nlohmann json (vendored with the glTF loader)
#include "scenes/file/nlohmann/json.hpp"
// std
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace anari::cts;

namespace {

bool g_verbose = false;

void statusFunc(const void *userData,
    anari::Device,
    anari::Object,
    anari::DataType,
    anari::StatusSeverity severity,
    anari::StatusCode,
    const char *message)
{
  const bool verbose = userData ? *static_cast<const bool *>(userData) : false;
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR)
    std::cerr << "[ANARI][ERROR] " << message << "\n";
  else if (verbose && severity == ANARI_SEVERITY_WARNING)
    std::cerr << "[ANARI][WARN ] " << message << "\n";
}

struct Options
{
  std::string filter;
  std::string workdir = "cts_workdir";
  std::string device; // library name for run/query/check
  uint32_t width = 256;
  uint32_t height = 256;
  std::string renderer = "default";
  float ambientRadiance = 1.f;
  // Device-agnostic renderer parameter overrides (repeatable --renderer-param
  // NAME=VALUE), applied to every rendered Case's renderer.
  std::vector<RendererParam> rendererParams;
  bool useStdin = false;
  // query-device-info: restrict to an object type / subtype substring, list
  // subtypes only, or include detailed per-parameter info.
  std::string typeFilter;
  std::string subtypeFilter;
  bool skipParameters = false;
  bool info = false;
  // Progressive frame accumulation (color channel) and renderer denoising,
  // each gated on the device's feature set in the Runner. The accumulation
  // value is the user-facing default; <=1 disables it.
  uint32_t accumulationFrames = 16;
  bool denoise = false;
  // report: itemize every case, write an HTML file, and embed images in it.
  // The positional arg holds the workdir.
  bool includeAll = false;
  bool embed = false;
  std::string htmlOut;
  std::vector<std::string> positionals;
};

void printUsage()
{
  std::cout << R"(usage: cts <command> [options]

commands:
  list                       list tests, descriptions, and case counts
  generate [options]         render ground truth with the reference device
  run <device> [options]     render + score a candidate device against ground truth
  query-features <device>    print the device's supported extensions
  query-metadata [options]   print catalog metadata as JSON
  query-device-info <device> introspect a device: object subtypes + parameter metadata
  check-properties <device>  report which tests the device can run vs. will skip
  report <workdir>           summarize a run's results tree (text + optional HTML)

options:
  --filter <pattern>   select a slice of the catalog (substring or glob over <category>/<test>)
  --workdir <dir>      run root holding results/ ground_truth/ assets/ (default: cts_workdir)
  --device <lib>       reference device library for generate (default: helide)
  --width <n>          render width (default: 256)
  --height <n>         render height (default: 256)
  -r, --renderer <subtype>
                       renderer subtype (default: default)
  --ambientRadiance <value>
                       baseline renderer ambientRadiance (default: 1)
  -p, --renderer-param NAME=VALUE
                       set a custom renderer parameter on every rendered case
                       (repeatable; device-agnostic). VALUE is parsed into the
                       renderer's device-declared type, else inferred as bool/
                       int/float/float-vector/string. e.g. -p ambientSample=4
  --accumulation <n>   progressive color-channel frames when the device supports
                       ANARI_KHR_FRAME_ACCUMULATION (default: 16; <=1 disables)
  --no-accumulation    render each channel once (equivalent to --accumulation 1)
  --denoise            set the renderer "denoise" parameter (warns, but still
                       sets it, if the device lacks ANARI_KHR_RENDERER_DENOISE)
  --stdin              read newline-separated filter patterns from stdin (run)
  --verbose            print ANARI warnings

report options:
  --html <path>        also write an interactive HTML report
  --embed              inline images in the HTML (portable single file; embeds
                       only the initially-shown cases unless combined with --all)
  --all                itemize every case (default: failures only)

query-device-info options:
  --type <name>        restrict to object types whose name contains <name>
  --subtype <name>     restrict to subtypes whose name contains <name>
  --skip-parameters    list subtypes only, omit parameters
  --info               include detailed per-parameter info (default/min/max/values/...)
)";
}

// Parse the options that follow a subcommand. A lone leading token that is not
// a flag is taken as the positional <device>.
Options parseOptions(int argc, char **argv, int start)
{
  Options o;
  // Parse a non-negative integer argument (width/height/accumulation).
  // std::stoul silently wraps a leading '-' to a huge value (so e.g.
  // --accumulation -1 would become ~4.3 billion frames -> an unbounded render
  // loop) and only throws above ULONG_MAX, so reject negatives, trailing
  // garbage, and anything that does not fit in uint32_t explicitly; keep the
  // caller's default on any error.
  auto parseDim = [](const std::string &s, uint32_t fallback) -> uint32_t {
    try {
      if (s.empty() || s[0] == '-')
        throw std::invalid_argument("not a non-negative integer");
      size_t pos = 0;
      const unsigned long v = std::stoul(s, &pos);
      if (pos != s.size() || v > std::numeric_limits<uint32_t>::max())
        throw std::out_of_range("does not fit in uint32_t");
      return static_cast<uint32_t>(v);
    } catch (const std::exception &) {
      std::cerr << "warning: invalid value '" << s << "', keeping default\n";
      return fallback;
    }
  };
  auto parseFloat = [](const std::string &s, float fallback) -> float {
    try {
      size_t pos = 0;
      const float v = std::stof(s, &pos);
      if (pos != s.size() || !std::isfinite(v))
        throw std::invalid_argument("not a finite float");
      return v;
    } catch (const std::exception &) {
      std::cerr << "warning: invalid value '" << s << "', keeping default\n";
      return fallback;
    }
  };
  for (int i = start; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      return (i + 1 < argc) ? argv[++i] : "";
    };
    if (a == "--filter")
      o.filter = next();
    else if (a == "--workdir")
      o.workdir = next();
    else if (a == "--device")
      o.device = next();
    else if (a == "--width")
      o.width = parseDim(next(), o.width);
    else if (a == "--height")
      o.height = parseDim(next(), o.height);
    else if (a == "--renderer" || a == "-r")
      o.renderer = next();
    else if (a == "--ambientRadiance")
      o.ambientRadiance = parseFloat(next(), o.ambientRadiance);
    else if (a == "--renderer-param" || a == "-p") {
      const std::string spec = next();
      if (auto p = parseRendererParam(spec))
        o.rendererParams.push_back(*p);
      else
        std::cerr << "warning: ignoring malformed --renderer-param '" << spec
                  << "' (expected NAME=VALUE)\n";
    } else if (a == "--accumulation")
      o.accumulationFrames = parseDim(next(), o.accumulationFrames);
    else if (a == "--no-accumulation")
      o.accumulationFrames = 1;
    else if (a == "--denoise")
      o.denoise = true;
    else if (a == "--stdin")
      o.useStdin = true;
    else if (a == "--type")
      o.typeFilter = next();
    else if (a == "--subtype")
      o.subtypeFilter = next();
    else if (a == "--skip-parameters")
      o.skipParameters = true;
    else if (a == "--info")
      o.info = true;
    else if (a == "--all")
      o.includeAll = true;
    else if (a == "--embed")
      o.embed = true;
    else if (a == "--html")
      o.htmlOut = next();
    else if (a == "--verbose")
      g_verbose = true;
    else if (!a.empty() && a[0] != '-') {
      o.positionals.push_back(a);
      if (o.device.empty())
        o.device = a; // first positional doubles as <device> for run/query
    } else
      std::cerr << "warning: ignoring unknown argument '" << a << "'\n";
  }
  return o;
}

std::set<std::string> deviceExtensions(
    anari::Library lib, const char *deviceType)
{
  std::set<std::string> out;
  const char **ext = anariGetDeviceExtensions(lib, deviceType);
  for (int i = 0; ext && ext[i]; ++i)
    out.insert(ext[i]);
  return out;
}

// --denoise sets the renderer's "denoise" parameter unconditionally; warn (but
// still proceed) when the device does not advertise ANARI_KHR_RENDERER_DENOISE,
// since the parameter is then ignored or device-defined.
void warnIfDenoiseUnsupported(const std::string &deviceName,
    const std::set<std::string> &features,
    bool denoiseRequested)
{
  if (denoiseRequested && !features.count("ANARI_KHR_RENDERER_DENOISE"))
    std::cerr << "warning: device '" << deviceName
              << "' does not report ANARI_KHR_RENDERER_DENOISE; setting the "
                 "renderer 'denoise' parameter anyway\n";
}

// Load a device library and create its default device, or null on failure.
anari::Device loadDevice(const std::string &name, anari::Library &lib)
{
  lib = anari::loadLibrary(name.c_str(), statusFunc, &g_verbose);
  if (!lib) {
    std::cerr << "error: failed to load ANARI library '" << name << "'\n";
    return nullptr;
  }
  auto d = anari::newDevice(lib, "default");
  if (!d) {
    std::cerr << "error: failed to create device from '" << name << "'\n";
    anari::unloadLibrary(lib);
    lib = nullptr;
    return nullptr;
  }
  anari::commitParameters(d, d);
  return d;
}

Catalog buildCatalog()
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  return catalog;
}

// --- commands ----------------------------------------------------------------

int cmdList(const Options &o)
{
  auto catalog = buildCatalog();
  const Filter filter{o.filter};
  int totalCases = 0;
  std::string currentCategory;
  for (const TestDef *t : catalog.filter(filter)) {
    if (t->category != currentCategory) {
      currentCategory = t->category;
      std::cout << currentCategory << "\n";
    }
    const int n = static_cast<int>(expand(*t).size());
    totalCases += n;
    std::cout << "  " << t->name << "  (" << n << " case"
              << (n == 1 ? "" : "s");
    if (t->simplified)
      std::cout << ", simplified";
    std::cout << ")\n";
    std::cout << "    " << t->description << "\n";
  }
  std::cout << "\n"
            << catalog.filter(filter).size() << " test(s), " << totalCases
            << " case(s)\n";
  return 0;
}

int cmdQueryMetadata(const Options &o)
{
  auto catalog = buildCatalog();
  nlohmann::json out = nlohmann::json::array();
  for (const TestDef *t : catalog.filter(Filter{o.filter})) {
    nlohmann::json jt;
    jt["id"] = t->id();
    jt["category"] = t->category;
    jt["name"] = t->name;
    jt["description"] = t->description;
    jt["simplified"] = t->simplified;
    jt["caseCount"] = expand(*t).size();
    jt["requiredFeatures"] = t->requiredFeatures;
    jt["thresholds"] = t->thresholds;
    jt["axes"] = nlohmann::json::array();
    for (const auto &ax : t->axes) {
      nlohmann::json ja;
      ja["name"] = ax.name;
      ja["kind"] = ax.kind == AxisKind::Permutation ? "permutation" : "variant";
      ja["values"] = nlohmann::json::array();
      for (const auto &v : ax.values)
        ja["values"].push_back(anyToString(v));
      jt["axes"].push_back(std::move(ja));
    }
    jt["channels"] = nlohmann::json::array();
    for (Channel ch : t->channels)
      jt["channels"].push_back(channelName(ch));
    out.push_back(std::move(jt));
  }
  std::cout << out.dump(2) << "\n";
  return 0;
}

int cmdQueryFeatures(const Options &o)
{
  if (o.device.empty()) {
    std::cerr << "error: query-features requires a <device>\n";
    return 2;
  }
  anari::Library lib = nullptr;
  auto d = loadDevice(o.device, lib);
  if (!d)
    return 2;
  for (const auto &e : deviceExtensions(lib, "default"))
    std::cout << e << "\n";
  anari::release(d, d);
  anari::unloadLibrary(lib);
  return 0;
}

int cmdQueryDeviceInfo(const Options &o)
{
  if (o.device.empty()) {
    std::cerr << "error: query-device-info requires a <device>\n";
    return 2;
  }
  anari::Library lib = nullptr;
  auto d = loadDevice(o.device, lib);
  if (!d)
    return 2;
  anari::introspection::QueryOptions options;
  options.typeFilter = o.typeFilter;
  options.subtypeFilter = o.subtypeFilter;
  options.includeParameters = !o.skipParameters;
  options.includeParameterInfo = o.info;
  std::cout << anari::introspection::formatDeviceInfo(
      anari::introspection::queryDeviceInfo(d, options));
  anari::release(d, d);
  anari::unloadLibrary(lib);
  return 0;
}

int cmdCheckProperties(const Options &o)
{
  if (o.device.empty()) {
    std::cerr << "error: check-properties requires a <device>\n";
    return 2;
  }
  anari::Library lib = nullptr;
  auto d = loadDevice(o.device, lib);
  if (!d)
    return 2;
  const auto features = deviceExtensions(lib, "default");

  auto catalog = buildCatalog();
  int runnable = 0, skipped = 0;
  for (const TestDef *t : catalog.filter(Filter{o.filter})) {
    const bool ok = isSupported(*t, features);
    runnable += ok;
    skipped += !ok;
    std::cout << (ok ? "[run ] " : "[skip] ") << t->id();
    if (!ok) {
      std::cout << "  missing:";
      for (const auto &f : t->requiredFeatures)
        if (!features.count(f))
          std::cout << " " << f;
    }
    std::cout << "\n";
  }
  std::cout << "\n"
            << runnable << " runnable, " << skipped << " skipped on '"
            << o.device << "'\n";
  anari::release(d, d);
  anari::unloadLibrary(lib);
  return 0;
}

int cmdGenerate(const Options &o)
{
  const std::string deviceName = o.device.empty() ? "helide" : o.device;
  anari::Library lib = nullptr;
  auto d = loadDevice(deviceName, lib);
  if (!d)
    return 2;
  const auto features = deviceExtensions(lib, "default");
  warnIfDenoiseUnsupported(deviceName, features, o.denoise);

  auto catalog = buildCatalog();
  RunOptions ro;
  ro.width = o.width;
  ro.height = o.height;
  ro.ambientRadiance = o.ambientRadiance;
  ro.accumulationFrames = o.accumulationFrames;
  ro.denoise = o.denoise;
  ro.rendererParams = o.rendererParams;
  ro.device = {deviceName, "default", o.renderer};
  Runner runner(d, Workdir(o.workdir), ro);

  auto s = runner.generate(catalog, Filter{o.filter}, features);
  std::cout << "generate (" << deviceName << "): " << s.passed << " generated, "
            << s.skipped << " skipped, " << s.failed << " failed (of "
            << s.total << ")\n";

  anari::release(d, d);
  anari::unloadLibrary(lib);
  return s.failed > 0 ? 1 : 0;
}

int cmdRun(const Options &o)
{
  if (o.device.empty()) {
    std::cerr << "error: run requires a <device>\n";
    return 2;
  }
  anari::Library lib = nullptr;
  auto d = loadDevice(o.device, lib);
  if (!d)
    return 2;
  const auto features = deviceExtensions(lib, "default");
  warnIfDenoiseUnsupported(o.device, features, o.denoise);

  auto catalog = buildCatalog();
  RunOptions ro;
  ro.width = o.width;
  ro.height = o.height;
  ro.ambientRadiance = o.ambientRadiance;
  ro.accumulationFrames = o.accumulationFrames;
  ro.denoise = o.denoise;
  ro.rendererParams = o.rendererParams;
  ro.device = {o.device, "default", o.renderer};
  Runner runner(d, Workdir(o.workdir), ro);

  RunSummary s;
  if (o.useStdin) {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.empty())
        continue;
      auto part = runner.run(catalog, Filter{line}, features);
      s.total += part.total;
      s.passed += part.passed;
      s.failed += part.failed;
      s.skipped += part.skipped;
    }
  } else {
    s = runner.run(catalog, Filter{o.filter}, features);
  }

  std::cout << "run (" << o.device << "): " << s.passed << " passed, "
            << s.failed << " failed, " << s.skipped << " skipped (of "
            << s.total << ")\n";

  anari::release(d, d);
  anari::unloadLibrary(lib);
  return s.failed > 0 ? 1 : 0;
}

// Reporting reads the results tree only; it never loads a device (ADR-0008).
int cmdReport(const Options &o)
{
  const std::string workdir =
      o.positionals.empty() ? o.workdir : o.positionals.front();
  auto results = loadResults(workdir, std::cerr);
  writeTextSummary(std::cout, workdir, results, o.includeAll);

  if (!o.htmlOut.empty()) {
    HtmlOptions html;
    html.includeAll = o.includeAll;
    html.embed = o.embed;
    html.htmlPath = o.htmlOut;
    const std::string doc = generateHtml(workdir, results, html);
    std::ofstream out(o.htmlOut, std::ios::binary);
    if (!out || !(out << doc)) {
      std::cerr << "error: failed to write HTML report '" << o.htmlOut << "'\n";
      return 2;
    }
    std::cout << "wrote " << o.htmlOut << "\n";
  }

  return summarize(results).failed > 0 ? 1 : 0;
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 2) {
    printUsage();
    return 2;
  }

  const std::string command = argv[1];
  if (command == "-h" || command == "--help" || command == "help") {
    printUsage();
    return 0;
  }

  const Options o = parseOptions(argc, argv, 2);

  if (command == "list")
    return cmdList(o);
  if (command == "generate")
    return cmdGenerate(o);
  if (command == "run")
    return cmdRun(o);
  if (command == "query-features")
    return cmdQueryFeatures(o);
  if (command == "query-metadata")
    return cmdQueryMetadata(o);
  if (command == "query-device-info")
    return cmdQueryDeviceInfo(o);
  if (command == "check-properties")
    return cmdCheckProperties(o);
  if (command == "report")
    return cmdReport(o);

  std::cerr << "error: unknown command '" << command << "'\n\n";
  printUsage();
  return 2;
}
