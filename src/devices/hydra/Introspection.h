// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari.h>
// std
#include <cstdint>
#include <vector>

namespace anari_hydra {

struct HydraGlobalState;

// Per-delegate views over the generated ANARI query tables (SPEC §4.8).
//
// `anari_library_hydra_queries.cpp` is generated at build time by the
// ANARI-SDK query generator from the SDK's own KHR schema files plus
// `introspection/*.json` (the device root and the bridge-only
// parameters). The tables are therefore the vanilla KHR wording for every
// extension the device implements on any delegate; the functions here apply
// the one narrowing the device makes at query time — extension existence:
// an entry whose `sourceExtension` the bound delegate does not advertise is
// not reflected. Where the device honours less than a claimed extension's
// schema states, README.md documents it rather than the tables.
//
// Returned pointers stay valid for the device lifetime: names point into the
// generated static tables and the filtered lists live in HydraGlobalState.

// `extensions` is the bound delegate's NUL-terminated extension view
// (HydraGlobalState::extensionView); it must outlive the state's caches.

// The filtered, NUL-terminated parameter list for (type, subtype), or nullptr
// when the tables know no such object. `subtype` must be nullptr for
// anonymous types and a table-known name otherwise (the device gates that).
const ANARIParameter *reflectedParameterList(HydraGlobalState &state,
    const std::vector<const char *> &extensions,
    ANARIDataType type,
    const char *subtype);

// Whether (name, parameterType) is an entry of the filtered list.
bool reflectsParameter(
    const ANARIParameter *list, const char *name, ANARIDataType parameterType);

// The frame's channel list narrowed to the channels whose `channel.*`
// parameter survives the filter above (SPEC §11.4).
const char **reflectedFrameChannels(
    HydraGlobalState &state, const std::vector<const char *> &extensions);

// The generated table's answer for `sourceExtension` re-indexed into the
// device's own extension list (ANARI_INT32 form); nullptr when the extension
// is not advertised. `extensionName` is the generated string (no ANARI_
// prefix).
const int32_t *sourceExtensionIndex(HydraGlobalState &state,
    const std::vector<const char *> &extensions,
    const char *extensionName);

} // namespace anari_hydra
