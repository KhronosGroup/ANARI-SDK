# Generator Example

The devices source in this example were generated using the frontend generator scripts from the `code_gen` directory.

## Steps to recreate

1. Create a directory for the new device `mkdir examples/tree_device`
2. (optional) Write a header `examples/tree_device/license.txt` with C style comments
3. (optional) Write a device definition similar to `code_gen/devices/extended_device.json`
3. run the generator
```
python3 code_gen/generate_device_frontend.py
	--json code_gen/
	--namespace tree
	--prefix Tree
	--device code_gen/devices/extended_device.json
	--header examples/tree_device/license.txt
	--output examples/tree_device
```
4. The `examples/tree_device` directory is now populated. Add the subdir in `examples/CMakeLists.txt`
5. Build the SDK including the new tree device (which is now sink like)
6. Insert custom code (see `RecursivePrint.h` and its use in `TreeDevice.cpp`)