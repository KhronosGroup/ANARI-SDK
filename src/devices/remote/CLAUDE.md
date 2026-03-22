# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is the **remote ANARI device** — a client-server system for distributed rendering over TCP. An ANARI application runs on the client side (loaded as `ANARI_LIBRARY=remote`) and forwards all rendering work to a server running an actual backend device (e.g., Helide).

See the repo-level `CLAUDE.md` for general build/test commands and the overall ANARI-SDK architecture.

## Building

Enable the remote device with the `BUILD_REMOTE_DEVICE=ON` CMake flag:

```bash
cmake -DBUILD_REMOTE_DEVICE=ON -DBUILD_HELIDE_DEVICE=ON ..
cmake --build .
```

Optional compression dependencies (recommended):
- **libjpeg-turbo** (TurboJPEG): Lossy color frame compression
- **Snappy**: Lossless compression fallback

## Running

```bash
# Server: loads the real backend device
ANARI_LIBRARY=helide anariRemoteServer

# Client: connect to the server (defaults to localhost:31050)
ANARI_LIBRARY=remote anariViewer
```

Configuration (set after device creation or via environment variables):

| Parameter | Env Var | Default |
|-----------|---------|---------|
| `server.hostname` | `ANARI_REMOTE_SERVER_HOSTNAME` | `localhost` |
| `server.port` | `ANARI_REMOTE_SERVER_PORT` | `31050` |
| Log level | `ANARI_REMOTE_LOG_LEVEL` | — |

Log levels: `error`, `warning`, `stats`, `info`

## Architecture

### Client (`Device.h/.cpp`, `Library.cpp`)

Inherits from `helium::BaseDevice`. On `initClient()`, opens a TCP connection to the server. Every ANARI API call (object creation, parameter setting, array data, frame rendering) is serialized into a typed message and sent over the socket.

Synchronization: the device maintains per-operation condition variables (`ConnectionEstablished`, `FrameIsReady`, `MapArray`, `Properties`, etc.) that block the calling thread until the server acknowledges. All async I/O runs on a separate Boost.ASIO strand.

### Server (`Server.cpp`)

Loads a local ANARI backend (via `ANARI_LIBRARY`), listens on a TCP port, and dispatches incoming messages to the backend device. Currently accepts only one connection at a time.

### Message Protocol (`common.h`)

Messages are identified by a UUID, type enum, and size. Key message types:

- **Object lifecycle**: `NewDevice`, `NewObject`, `NewArray`, `Release`, `Retain`
- **Parameters**: `SetParam`, `UnsetParam`, `CommitParams`
- **Arrays**: `ArrayData`, `MapArray`/`ArrayMapped`, `UnmapArray`/`ArrayUnmapped`
- **Rendering**: `RenderFrame`, `FrameIsReady`, `ChannelColor`, `ChannelDepth`
- **Queries**: `GetProperty`/`Property`, `GetObjectSubtypes`, `GetObjectInfo`, `GetParameterInfo`

### Serialization (`Buffer.h`)

`Buffer` is a typesafe byte buffer used to serialize/deserialize message payloads. Objects and parameters flow through `ObjectDesc` (`ObjectDesc.h`) and `ArrayInfo` (`ArrayInfo.h`).

### Compression (`Compression.h/.cpp`)

Frame buffers are optionally compressed before transmission. TurboJPEG handles lossy color compression; Snappy handles lossless data. Capability negotiation happens at connection setup.

### Async I/O (`async/`)

- `connection.h` / `connection_manager.h`: Boost.ASIO TCP connection wrappers
- `message.h`: Wire format for framed messages
- `work_queue.h`: Thread-safe queue for dispatching incoming messages to handlers
