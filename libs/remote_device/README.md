# The Remote Device

The remote device and `anari-remote-server` application establish a TCP
connection. On the server side the `anari-remote-server` application connects
to an arbitrary ANARI device. ANARI commands from the client pass through the
TCP connection; the server forwards the commands to the server-side device.
Color and depth images are sent from the server to the client. These color and
depth images can be compressed with TurboJPEG and/or Snappy. This way the
client and server implement TCP passthrough and remote rendering.

## Usage

On the server, run the `anari-remote-server` application with a ANARI library
configured; the server will listen for incoming connections, e.g.:

```
ANARI_LIBRARY=helide anari-remote-server
```

the server will wait for any incoming TCP connections.

On the client side, run any ANARI client with the library `remote`:

```
ANARI_LIBRARY=remote anariViewer
```

The client and server will establish a connection over TCP. The default
hostname and port are "localhost" and "31050". On the server side, the port can
be set via the command line. On the client side, hostname and port can be set
via the ANARI device parameters `server.hostname` and `server.port`, e.g.:

```
const char *hostname = "localhost";
anariSetParameter(device, device, "server.hostname", ANARI_STRING, hostname);

unsigned short port = 31050;
anariSetParameter(device, device, "server.port", ANARI_UINT16, &port);
```

These device parameters must be set once, and right after device creation,
i.e., before any other parameters are set, committed, before device properties
are queried, etc. In order to reset the hostname and port over which to connect
to the server, a new remote device instance must be created.

Currently, the server accepts a single connection at a time.

### Debugging

Set `ANARI_REMOTE_LOG_LEVEL` to "error"|"warning"|"stats"|"info" on the client
and/or server side to generate more verbose output on the command line.
