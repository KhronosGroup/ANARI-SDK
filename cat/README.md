# CAT

The Capability Analysis Tool (CAT) `anariCat` is an application built on top of the `anari_viewer` and
`anari_test_scenes` library. It can run interactively using a graphical user interface (ImGUI).
It can also execute without a GUI. It is meant to analyse the limits and performance of ANARI implementations using procedurally generated scenes that are designed to stress the ANARI renderers.

## Build

You can build the CAT by turning on the CMake configuration option `BUILD_CAT`. Additionally,
set the `INSTALL_CAT` value to `ON` if you wish to install `anariCat`.

### Configure

```sh
cmake -S . -B out -DBUILD_CAT=ON -DINSTALL_CAT=ON
```

### Compile and link

```sh
cmake --build out
```

### Install

```sh
cmake --install out --prefix install
```

## Running the capability analysis tool

You can run the tool with GUI or non-interactively using the command line interface. The CLI has
options to set the total number of frames to render, limit the execution of the tool to a specified
number of seconds, and save screenshots of every N frames. Finally, it also dumps various performance
metrics to a CSV file.

The tool offers two subcommands `list` and `run`. You may view the usage in detail with the `-h`/`--help` argument.

#### Print a list of scenes

```ps
anariCat.exe list scenes 

Available scenes:
0       demo:cornell_box
1       demo:gravity_spheres_volume
2       file:obj
3       perf:materials
4       perf:particles
5       perf:primitives
6       perf:spinning_cubes
7       perf:surfaces
8       test:instanced_cubes
9       test:pbr_spheres
10      test:random_cylinders
11      test:random_spheres
12      test:textured_cube
13      test:triangle_attributes
```

#### Show help for subcommand run
```ps
anariCat.exe run --help

OPTIONS:
  -h,     --help              Print this help message and exit
  -a,     --animate           Play animation
  -g,     --gui               Enable GUI
  -o,     --orthographic      Enable orthographic camera
          --size UINT [[1920,1080]]  x 2
                              In interactive mode, the entered values correspond to the
                              dimensions for GUI window. In non-interactive mode, the entered
                              values correspond to the dimensions of the frame. Ex: --size 1920
                              1080
  -d,     --device UINT [0]   Device ID
  -l,     --library TEXT [helide]
                              ANARI implementation name
  -m,     --log-metrics TEXT [metrics.csv]
                              Save performance metric information to a CSV file.
  -r,     --renderer TEXT [default]
                              ANARI renderer subtype
  -s,     --scene TEXT [perf:particles]
                              Scene name. Ex: <category:scene>

non-interactive-only:
  -t,     --timespan FLOAT [60]
                              Continuously render frames for the specified timespan (seconds).
                              Exits the application if the frame timestamp exceeds timespan.
  -n,     --num-frames UINT [0]
                              Render at least specified number of frames.
  -c,     --capture-screenshot UINT [0]
                              Save screenshot every N frames. Uses PNG format.
```

### Examples

1. Render atmost 100 frames using the "materials" scene. Save a screenshot every 5 frames. Terminate the program if execution takes longer than 3 seconds.

    ```ps
    anariCat.exe run -s perf:materials -n 100 -c 5 -t 3
    ```

    ```ps
    100/100 frames 1.7s/3.0s elapsed

    ===================
    Performance Summary
    ===================
    Total # frames rendered 100
    Scene build     0.3ms
    Device
            Avg     39.0ms
            Min     39.0ms
            Max     39.0ms
    Scene update
            Avg     0.0ms
            Min     0.0ms
            Max     0.0ms
    Saved performance metrics to "C:\\install\\bin\\metrics.csv"
    ```

2. Animate and render atmost 300 frames using the "particles" scene. Use the `visrtx` ANARI library. Save a screenshot every 100 frames. Terminate the program if execution takes longer than 10 seconds.

    ```ps
    anariCat.exe run -a -s perf:particles -n 300 -c 100 -t 10
    ```

    ```ps
    300/300 frames  7.8s/10.0s elapsed

    ===================
    Performance Summary
    ===================
    Total # frames rendered 300
    Scene build     3.9ms
    Device
            Avg     22.6ms
            Min     21.4ms
            Max     42.2ms
    Scene update
            Avg     0.4ms
            Min     0.3ms
            Max     0.8ms
    Saved performance metrics to "C:\\install\\bin\\metrics.csv"
    ```

3. Launch the GUI, go to the scene "perf:surfaces"

    ```ps
    anariCat.exe run -g -s perf:surfaces
    ```

4. Launch the GUI, go to the scene "perf:surfaces", and start animation.

    ```ps
    anariCat.exe run -ag -s perf:surfaces
    ```

5. Launch the GUI and use the `visrtx` ANARI library. Log metrics to "metrics_interactive.csv"

    ```ps
    anariCat.exe run -g -l visrtx -m metrics_interactive.csv
    ```
