# The Helium Library

Helium is a library of base abstractions to reduce the amount of required code
needed to successfully implement the ANARI API. While using helium is not
required to implement ANARI, it does provide base functionality that is very
common for devices to implement. Specifically, `libhelium` provides the
following functionality:

- Parameter management (`anariSetParameter` + `anariUnsetParameter`)
- Object commit management
    - Deferred object commit buffering
    - Top-down commit reordering
- Object lifetime management
- Interface base classes to handle array and frame specfic API calls
- Array change tracking
- Status callback messaging infrastructure

The tradeoff implementors make is that by choosing to use helium, you
necessarily buy into implementation choices made. Thus implementations which
want to tightly control the above set of features (or simply make different
choices) ought to instead directly implement the ANARI API interface.

### Example implementation (Helide)

The [helide device](../devices/helide) is a minimal implementation which
combines helium and Embree to do very basic CPU rendering with ray tracing.
Potential helium library users can use helide as a guide for how helium
abstractions are intended to be used.

### Where did the name "Helium" come from?

The name helium is a pun on the gallium library found in the
[Mesa3D](https://www.mesa3d.org/) graphics library -- both libraries are aimed
at reducing the amount of implementation required to implement their respective
APIs, but doing so for ANARI is a much smaller problem and therefore requires a
much smaller library than gallium. Thus an element lighter than gallium was
chosen as the name...helium!

## Using Helium to implement ANARI

Helium is a small static library shipped by the ANARI SDK, which can be linked
as a CMake target. This may look like:

```cmake
find_package(anari)
add_library(anari_device_myDevice SHARED)
target_link_libraries(anari_device_myDevice PRIVATE anari::helium)
# ...
```

Note that `anari::helium` will still require linking either `anari::anari` or
`anari::anari_static` to give consumers the option whether to link the ANARI
API dynamically or statically.

The following sections will outline various design choices relevent to
implementors using helium.


### TimeStamp

The concept of a [TimeStamp](utility/TimeStamp.h) is just a unique integer for
every time `helium::newTimeStamp()` is called -- this makes it easy to track
when two events occured relative to each other in time, but the precise clock
time does not matter. This concept/abstraction is primarily used to track if
object commits need to occur or can be skipped.

### BaseDevice

The [helium::BaseDevice](BaseDevice.h) class implements a subset of the main API
class that the C API forward to
([anari::DeviceImpl](../anari/include/anari/backend/DeviceImpl.h)).
Subclasses are mostly responsible for implementing all the `new*()` methods
of `anari::DeviceImpl` as helium doesn't know how to create concrete objects.

Some subclasses may desire to still "intercept" all base implemented API calls,
which can easily be done by overriding the method found in `helium::BaseDevice`
and then call it when appropriately. This may look like:

```cpp
void MyDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  // Insert 'MyDevice' specific code (ex: setting an active GPU device)
  //
  // Then call base helium library
  helium::BaseDevice::setParameter(object, name, type, mem);
}
```

NOTE: For `helium::BaseDevice` to function correctly, all objects passed through
the API _must_ derive from `BaseObject`, `BaseArray`, and `BaseFrame`
respectively!

### BaseObject

Helium based devices use [helium::BaseObject](BaseObject.h) to generically
represent API calls involving `ANARIObject`. This includes setting/unsetting
parameters, property queries, and committing parameters. It also includes
information for comparing when parameter changes have occured to when the object
has committed those parameters.

All parameter handling is done through
[helium::ParameterizedObject](utility/ParameterizedObject.h). Helium uses a
'pull' based model for handling parameters -- all parameter values are
generically stored in the object and are expected to be read on
`helium::BaseObject::commitParameters()`. See comments on
[ParameterizedObject](utility/ParameterizedObject.h) methods for a further
explanation.

Object parameter commits are deferred until the device chooses to flush the
[DeferedCommitBuffer](utility/DeferredCommitBuffer.h) that lives in the
global device state instance on the device itself. It is entirely up to the
implementation to choose when this buffer should be flushed -- most commonly
this will be done at the beginning of rendering a frame and when some property
values are queried. Please refer to the documentation within the
[DeferedCommitBuffer](utility/DeferredCommitBuffer.h) header for more deatils.

Note that objects also have an ability to be "finalized" via the
`helium::BaseObject::finalize()` virtual method. This is separately tracked
from committing parameters, so internal state can be triggered to be updated
independent from needing to read parameter values again. The
`DeferredCommitBuffer` handles both parameter commits and finalization and will
only do either option if necessary using time stamps to record object
parameters being changed and internal state being updated. Objects which have
new parameters committed will automatically trigger finalization when the
`DeferredCommitBuffer` is being flushed.

Finally, objects can use `helium::BaseObject::reportMessage()` to generically
report status messages through the application provided callbacks (setup and
managed for you in `helium::BaseDevice`).

### BaseArray + BaseFrame

[helium::BaseArray](BaseArray.h) and [helium::BaseFrame](BaseFrame.h) are both
classes which add additional interface to `helium::BaseObject` for handling
ANARI API calls specific to those objects.

`helium::BaseArray` also maitains a list of objects which opt-in to being
notified when an array changes -- this relationship is often best managed when
an object containing the array is committed. Note that array classes inheriting
from `helium::BaseArray` are responsible for notifying objects observing them
as helium can't know when that is appropriate for every implementation. This
is commonly done when arrays are mapped/unmapped.

`BaseArray` also implements the concept of "privatizing" an array -- when the
public ref count of an object goes to 0 and the internal ref count is non-zero,
a shared array may need to make a copy of the array data from the application
to continue functioning correctly because the application may free that memory.
If any array is released and the above ref count case is encountered, the
`BaseArray::privatize()` method is invoked so the implementation can respond
accordingly based on what the implementation may require. Note that using
`helium::IntrusivePtr` by default will only modify internal ref counts, so
exclusively using it will cleanly divide application ref count changes vs.
internal ref counts.

Additionally, there are basic (host-only) array implementations provided that
can be used out-of-the-box for many implementations. Their common functionality
is found in [Array](array/Array.h), with complete interfaces in
[Array1D](array/Array1D.h), [Array2D](array/Array2D.h),
[Array3D](array/Array3D.h), and [ObjectArray](array/ObjectArray.h) respectively.

### BaseGlobalDeviceState

[helium::BaseGlobalDeviceState](BaseGlobalDeviceState.h) is a struct containing
data that both devices and objects will share, but breaks the need for objects
to need to include the declaration of the device itself. While not required,
implementations are expected to inherit from `BaseGlobalDeviceState` to append
to this storage whatever implementation-specific data is needed.

Implementations must construct the protected `BaseDevice::m_state` variable so
that status callback output can function properly. Devices which extend
`BaseGlobalDeviceState` instead should initialze `m_state` with their own
derived type which the downstream implementation can safely cast to.

### Math + shading utility functions

Helium uses the [ANARI provided math
types](../anari/include/anari/anari_cpp/ext/linalg.h) extensively, as well as
providing various common functions that can be useful for implementing ANARI.
All such functionality is found in the single [helium_math.h](helium_math.h)
header.
