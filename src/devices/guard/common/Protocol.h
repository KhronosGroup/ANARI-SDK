// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace anari_guard {

// Bumped by hand whenever any message changes.
constexpr uint32_t PROTOCOL_VERSION = 6;

struct MessageHeader
{
  uint32_t size; // payload bytes following the header
  uint16_t opcode;
  uint16_t descriptors; // 0 or 1; a message with one is sent on its own
};
static_assert(sizeof(MessageHeader) == 8, "header is 8 bytes on the wire");

// Object ids: 0 is NULL, 1 is the device, the adapter hands out the rest.
using ObjectId = uint64_t;
constexpr ObjectId NULL_ID = 0;
constexpr ObjectId DEVICE_ID = 1;
// Never an object. As an element of an object array in UNMAP_ARRAY: this
// element is what it was when the adapter last sent it, so leave it alone.
// It is how an element goes on naming an object whose handle the application
// has released, and whose id both sides have therefore forgotten.
constexpr ObjectId UNCHANGED_ID = ~ObjectId(0);

// The two constants that common/Tuning.h lets the environment override: the
// array ceiling, the size from which an array's contents always cross in a
// region, and how often the host asks the wrapped device whether an
// outstanding render is over.
constexpr uint64_t DEFAULT_ARRAY_CEILING = 1024 * 1024;
constexpr uint64_t DEFAULT_PROBE_PERIOD_US = 1000;

// The size from which a mapped frame channel crosses in a region, which the
// host keeps for the next mapping of that channel. Nothing overrides it.
constexpr uint64_t FRAME_REGION_THRESHOLD = 16 * 1024;

// Where a message's bulk data is.
enum class Storage : uint8_t
{
  NONE = 0, // there is none
  INLINE, // the bytes follow
  // uint64 offset and uint64 length within a region follow. Which region:
  // the one whose descriptor the message carries, or failing that the one
  // the receiver already holds for the same object.
  REGION
};

// Wire strings are a uint32 length and bytes; this length means a null pointer.
constexpr uint32_t NULL_STRING = 0xffffffffu;

enum class Op : uint16_t
{
  // Session
  HELLO = 1, // host -> adapter: uint32 version
  CREATE_DEVICE, // library, subtype -> ok flag, failure text
  QUERY_EXTENSIONS, // library, subtype -> ok flag, failure text, string list
  RELEASE_DEVICE, // -> empty reply

  // One per DeviceImpl call that needs a message
  NEW_ARRAY1D,
  NEW_ARRAY2D,
  NEW_ARRAY3D,
  NEW_GEOMETRY,
  NEW_MATERIAL,
  NEW_SAMPLER,
  NEW_SURFACE,
  NEW_SPATIAL_FIELD,
  NEW_VOLUME,
  NEW_LIGHT,
  NEW_GROUP,
  NEW_INSTANCE,
  NEW_WORLD,
  NEW_CAMERA,
  NEW_RENDERER,
  NEW_FRAME,
  NEW_OBJECT,
  SET_PARAMETER,
  UNSET_PARAMETER,
  UNSET_ALL_PARAMETERS,
  COMMIT_PARAMETERS,
  RELEASE,
  RETAIN,
  UNMAP_ARRAY, // the array's contents changed
  GET_PROPERTY, // -> reply
  GET_OBJECT_SUBTYPES, // -> reply
  GET_OBJECT_INFO, // -> reply
  GET_PARAMETER_INFO, // -> reply
  FRAME_BUFFER_MAP, // -> reply
  RENDER_FRAME,
  DISCARD_FRAME,

  // Event channel, host -> adapter
  STATUS_EVENT = 100,
  // A render is over, however it ended: frame id, then as a uint64 which of
  // that frame's renders, counted from 1 by both sides.
  FRAME_COMPLETED_EVENT,
  // The wrapped device exists: sent once, before the reply to CREATE_DEVICE.
  // Every status event ahead of it is from before there was a device.
  DEVICE_CREATED_EVENT,
  // The wrapped device called the frame's completion callback: frame id.
  FRAME_CALLED_BACK_EVENT
};

// How a reply carries a value the API returns by pointer.
enum class ValueKind : uint8_t
{
  NONE = 0, // NULL
  BYTES, // uint32 size, bytes
  STRING, // string
  STRING_LIST, // uint32 count, strings
  DATA_TYPE_LIST, // uint32 count, int32s
  PARAMETER_LIST // uint32 count, (string, int32)s
};

} // namespace anari_guard
