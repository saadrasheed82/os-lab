# Temperature Monitoring System Using a Kalman Filter

This C++17 project reads temperature measurements, filters their noise with a
scalar Kalman filter, saves results, and demonstrates native operating-system
APIs. The Kalman filter is shared; file, process, memory, IPC, and permission
demonstrations use platform-specific APIs.

## Objectives and workflow

By default, the program asks only for the number of readings and the
temperature values, then displays the raw and filtered values. Run the program
with `--menu` to use file input, saving, system-call demonstrations,
independent streams, and performance trials.

Start the program from this directory so `input/` and `output/` resolve
correctly. The output directory is included in the project. Menu actions are
appended to `output/execution_log.txt`.

## Kalman filter

The filter keeps one estimate and its error covariance. For each new
measurement, it predicts, calculates how much to trust the measurement, then
updates the estimate:

```text
x_predicted = x_previous
P_predicted = P_previous + Q
K = P_predicted / (P_predicted + R)
x_updated = x_predicted + K * (measurement - x_predicted)
P_updated = (1 - K) * P_predicted
```

`Q` is process noise: larger values let the estimate adapt more quickly.
`R` is measurement noise: larger values make the filter trust each sensor
reading less. Defaults are `Q = 0.01` and `R = 1.0`. Initial covariance is
`1.0`, and the first reading initializes the temperature estimate.

## Native APIs demonstrated

| Category | Linux | Windows |
|---|---|---|
| File input/output | `open`, `read`, `write`, `close` | `CreateFile`, `ReadFile`, `WriteFile`, `CloseHandle` |
| Processes | `fork`, `exec`, `wait`; child streams return results through a pipe | `CreateProcess`, `WaitForSingleObject`; child streams receive readings through an anonymous pipe |
| Memory | anonymous `mmap`, `munmap` | `VirtualAlloc`, `VirtualFree` |
| IPC/shared memory | `pipe`, `shmget`, `shmat`, `shmdt`, `shmctl` | `CreatePipe`, `CreateFileMapping`, `MapViewOfFile`, `UnmapViewOfFile` |
| Permissions | `chmod`, `chown`, `umask` | `SetFileSecurity` |

Operations use disposable files under `output/`; they do not modify unrelated
files. The permission demo checks that `umask` creates a file with the expected
mode and restores the previous mask, then applies `chmod`/`chown` (Linux) or a
test-file DACL (Windows). Permission changes can fail when the current account
lacks the required rights, and the program reports the failure. Each
system-call demo run is recorded as passed or failed in
`output/execution_log.txt`. Linux `chown` and Windows `SetFileSecurity` have
different permission models and are not exact one-to-one equivalents.

Each stream is processed sequentially because Kalman estimates depend on
earlier readings. Independent streams are processed in separate processes.
Linux sends each child's filtered values back over a pipe. Windows sends
readings to a `CreateProcess` worker through an anonymous pipe and collects
the results in a temporary file.

## Build and run

### Windows (MinGW / Dev-C++)

From PowerShell in this project directory:

```powershell
g++ -std=c++17 -Wall -Wextra main.cpp kalman_filter.cpp sensor_data.cpp system_calls.cpp -o TemperatureMonitoring.exe -ladvapi32 -lpsapi
.\TemperatureMonitoring.exe
```

In Dev-C++, create a C++17 console project and add all four `.cpp` files.
Add `-ladvapi32 -lpsapi` to the linker options.

### Linux (Ubuntu)

From a terminal in this project directory:

```sh
g++ -std=c++17 -Wall -Wextra main.cpp kalman_filter.cpp sensor_data.cpp system_calls.cpp -o TemperatureMonitoring
./TemperatureMonitoring
```

Run `./TemperatureMonitoring --menu` to load data, save
`output/filtered_results.csv`, or use the additional demonstrations. The
default sample input is
`input/temperature_data.csv`. The performance menu writes
`output/performance_results.csv`.

For the sample input with `Q = 0.01` and `R = 1.0`, the final estimate is
approximately `26.24 C`. The saved CSV includes every raw reading and its
calculated filtered estimate.

## Performance measurements

The performance menu runs 10 trials each for native file read/write, independent
process streams, memory allocation/mapping, and a pipe round trip. It records
each measured elapsed time and the average in CSV. It also records process CPU
time and peak working-set/RSS snapshots where the platform API provides them;
those are process-level measurements, not per-operation attribution.

Each operating system must be run separately with the same build and workload.
The program records the platform name but does not invent a cross-platform
comparison or generate charts. Create comparison tables and charts only after
collecting both actual CSV files. Results vary by machine, compiler, and system
load.

## Platform and testing notes

The Windows implementation is the implementation targeted by this workspace.
The Linux branch is provided for Ubuntu, but it must be compiled and exercised
on Linux before claiming Linux test results. Account permissions can prevent
permission demonstrations from succeeding on either platform.

## Report outline

1. Introduction and problem statement
2. Objectives and system design
3. Kalman filter equations and methodology
4. File handling and native system-call implementation
5. Process management, parallel streams, memory management, and IPC
6. Test setup and operating-system/compiler environment
7. Performance tables and charts derived from the generated CSV files
8. Results, observations, platform differences, and limitations
9. Conclusion
