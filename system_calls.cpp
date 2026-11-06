#ifndef _WIN32
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include "system_calls.h"

#include "kalman_filter.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <psapi.h>
#else
#include <fcntl.h>
#include <grp.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#define TM_STRINGIFY_DETAIL(value) #value
#define TM_STRINGIFY(value) TM_STRINGIFY_DETAIL(value)

namespace {

#ifdef _WIN32
std::string windowsError(const char* operation) {
    const DWORD code = GetLastError();
    std::ostringstream message;
    message << operation << " failed (Windows error " << code << ").";
    return message.str();
}
#else
std::string posixError(const char* operation) {
    return std::string(operation) + " failed: " + std::strerror(errno) + ".";
}
#endif

#ifndef _WIN32
bool readAll(int descriptor, char* buffer, std::size_t length) {
    std::size_t total = 0;
    while (total < length) {
        const ssize_t count = read(descriptor, buffer + total, length - total);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        total += static_cast<std::size_t>(count);
    }
    return true;
}

bool writeAll(int descriptor, const char* buffer, std::size_t length) {
    std::size_t total = 0;
    while (total < length) {
        const ssize_t count = write(descriptor, buffer + total, length - total);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        total += static_cast<std::size_t>(count);
    }
    return true;
}
#endif

#ifdef _WIN32
std::string temporaryPath(const char* prefix) {
    char directory[MAX_PATH + 1] = {};
    char fileName[MAX_PATH + 1] = {};
    const DWORD length = GetTempPathA(MAX_PATH, directory);
    if (length == 0 || length > MAX_PATH ||
        GetTempFileNameA(directory, prefix, 0, fileName) == 0) {
        return std::string();
    }
    return fileName;
}
#endif

class TestFileCleanup {
public:
    explicit TestFileCleanup(const std::string& filePath) : path(filePath) {}

    ~TestFileCleanup() {
#ifdef _WIN32
        DeleteFileA(path.c_str());
#else
        unlink(path.c_str());
#endif
    }

private:
    std::string path;
};

void resourceSnapshot(long long& cpuMicroseconds,
                      unsigned long long& peakMemoryBytes) {
#ifdef _WIN32
    FILETIME creation = {}, exit = {}, kernel = {}, user = {};
    PROCESS_MEMORY_COUNTERS_EX memory = {};
    memory.cb = sizeof(memory);
    cpuMicroseconds = 0;
    peakMemoryBytes = 0;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        ULARGE_INTEGER kernelTicks = {};
        ULARGE_INTEGER userTicks = {};
        kernelTicks.LowPart = kernel.dwLowDateTime;
        kernelTicks.HighPart = kernel.dwHighDateTime;
        userTicks.LowPart = user.dwLowDateTime;
        userTicks.HighPart = user.dwHighDateTime;
        cpuMicroseconds = static_cast<long long>(
            (kernelTicks.QuadPart + userTicks.QuadPart) / 10);
    }
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory))) {
        peakMemoryBytes = static_cast<unsigned long long>(memory.PeakWorkingSetSize);
    }
#else
    struct rusage usage = {};
    cpuMicroseconds = 0;
    peakMemoryBytes = 0;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        cpuMicroseconds =
            static_cast<long long>(usage.ru_utime.tv_sec) * 1000000LL +
            usage.ru_utime.tv_usec +
            static_cast<long long>(usage.ru_stime.tv_sec) * 1000000LL +
            usage.ru_stime.tv_usec;
        peakMemoryBytes =
            static_cast<unsigned long long>(usage.ru_maxrss) * 1024ULL;
    }
#endif
}

std::string compilerDescription() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
    return std::string("MSVC ") + TM_STRINGIFY(_MSC_VER);
#else
    return "Unknown compiler";
#endif
}

std::string environmentDescription() {
#ifdef _WIN32
    SYSTEM_INFO information = {};
    GetNativeSystemInfo(&information);
    std::ostringstream description;
    description << "Windows processors=" << information.dwNumberOfProcessors
                << " architecture=" << information.wProcessorArchitecture;
    return description.str();
#else
    struct utsname information = {};
    if (uname(&information) != 0) {
        return "Linux environment details unavailable";
    }
    std::ostringstream description;
    description << information.sysname << ' ' << information.release << ' '
                << information.machine;
    return description.str();
#endif
}

}  // namespace

// Reads a file with the platform's native file APIs and checks every operation.
bool readNativeFile(const std::string& path, std::string& contents) {
    contents.clear();
#ifdef _WIN32
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << windowsError("CreateFile").c_str() << '\n';
        return false;
    }
    char buffer[4096];
    DWORD bytesRead = 0;
    BOOL success = TRUE;
    while ((success = ReadFile(file, buffer, sizeof(buffer), &bytesRead, NULL)) &&
           bytesRead > 0) {
        contents.append(buffer, bytesRead);
    }
    const bool closed = CloseHandle(file) != 0;
    if (!success || !closed) {
        std::cerr << windowsError(!success ? "ReadFile" : "CloseHandle").c_str()
                  << '\n';
        return false;
    }
    return true;
#else
    const int file = open(path.c_str(), O_RDONLY);
    if (file < 0) {
        std::cerr << posixError("open") << '\n';
        return false;
    }
    char buffer[4096];
    for (;;) {
        const ssize_t bytesRead = read(file, buffer, sizeof(buffer));
        if (bytesRead < 0 && errno == EINTR) {
            continue;
        }
        if (bytesRead < 0) {
            std::cerr << posixError("read") << '\n';
            close(file);
            return false;
        }
        if (bytesRead == 0) {
            break;
        }
        contents.append(buffer, static_cast<std::size_t>(bytesRead));
    }
    if (close(file) != 0) {
        std::cerr << posixError("close") << '\n';
        return false;
    }
    return true;
#endif
}

// Creates or replaces a file using the native write API for this operating system.
bool writeNativeFile(const std::string& path, const std::string& contents) {
#ifdef _WIN32
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << windowsError("CreateFile").c_str() << '\n';
        return false;
    }
    std::size_t total = 0;
    bool success = true;
    while (total < contents.size()) {
        const DWORD chunk = static_cast<DWORD>(
            (contents.size() - total) > 0x7fffffffU
                ? 0x7fffffffU
                : contents.size() - total);
        DWORD bytesWritten = 0;
        if (!WriteFile(file, contents.data() + total, chunk, &bytesWritten, NULL) ||
            bytesWritten == 0) {
            success = false;
            break;
        }
        total += bytesWritten;
    }
    const bool closed = CloseHandle(file) != 0;
    if (!success || !closed) {
        std::cerr << windowsError(!success ? "WriteFile" : "CloseHandle").c_str()
                  << '\n';
        return false;
    }
    return true;
#else
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file < 0) {
        std::cerr << posixError("open") << '\n';
        return false;
    }
    if (!writeAll(file, contents.data(), contents.size())) {
        std::cerr << posixError("write") << '\n';
        close(file);
        return false;
    }
    if (close(file) != 0) {
        std::cerr << posixError("close") << '\n';
        return false;
    }
    return true;
#endif
}

// Appends a log entry without replacing earlier execution history.
bool appendNativeFile(const std::string& path, const std::string& contents) {
#ifdef _WIN32
    HANDLE file = CreateFileA(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << windowsError("CreateFile for append").c_str() << '\n';
        return false;
    }
    std::size_t total = 0;
    bool success = true;
    while (total < contents.size()) {
        const DWORD chunk = static_cast<DWORD>(
            (contents.size() - total) > 0x7fffffffU
                ? 0x7fffffffU
                : contents.size() - total);
        DWORD bytesWritten = 0;
        if (!WriteFile(file, contents.data() + total, chunk, &bytesWritten, NULL) ||
            bytesWritten == 0) {
            success = false;
            break;
        }
        total += bytesWritten;
    }
    const bool closed = CloseHandle(file) != 0;
    if (!success || !closed) {
        std::cerr << windowsError(!success ? "WriteFile" : "CloseHandle").c_str()
                  << '\n';
        return false;
    }
    return true;
#else
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (file < 0) {
        std::cerr << posixError("open for append") << '\n';
        return false;
    }
    if (!writeAll(file, contents.data(), contents.size())) {
        std::cerr << posixError("write for append") << '\n';
        close(file);
        return false;
    }
    if (close(file) != 0) {
        std::cerr << posixError("close after append") << '\n';
        return false;
    }
    return true;
#endif
}

// Runs a separate child process for each stream and returns its filtered values.
bool processStreamsInProcesses(const std::vector<std::vector<double>>& streams,
                               std::vector<std::vector<double>>& results,
                               const std::string& executablePath) {
    results.clear();
    results.resize(streams.size());
    if (streams.empty()) {
        std::cerr << "There are no independent streams to process.\n";
        return false;
    }

#ifdef _WIN32
    for (std::size_t index = 0; index < streams.size(); ++index) {
        if (streams[index].empty()) {
            std::cerr << "A sensor stream is empty.\n";
            return false;
        }
        const std::string outputPath = temporaryPath("tmo");
        if (outputPath.empty()) {
            std::cerr << windowsError("GetTempFileName").c_str() << '\n';
            return false;
        }
        std::ostringstream input;
        for (std::size_t item = 0; item < streams[index].size(); ++item) {
            input << std::setprecision(17) << streams[index][item] << '\n';
        }
        SECURITY_ATTRIBUTES inherited = {};
        inherited.nLength = sizeof(inherited);
        inherited.bInheritHandle = TRUE;
        HANDLE workerRead = NULL;
        HANDLE parentWrite = NULL;
        if (!CreatePipe(&workerRead, &parentWrite, &inherited, 0) ||
            !SetHandleInformation(parentWrite, HANDLE_FLAG_INHERIT, 0)) {
            std::cerr << windowsError("CreatePipe for worker").c_str() << '\n';
            if (workerRead != NULL) CloseHandle(workerRead);
            if (parentWrite != NULL) CloseHandle(parentWrite);
            DeleteFileA(outputPath.c_str());
            return false;
        }
        HANDLE childOutput = CreateFileA(outputPath.c_str(), GENERIC_WRITE,
                                         FILE_SHARE_READ, &inherited, CREATE_ALWAYS,
                                         FILE_ATTRIBUTE_NORMAL, NULL);
        if (childOutput == INVALID_HANDLE_VALUE) {
            std::cerr << windowsError("CreateFile for worker").c_str() << '\n';
            CloseHandle(workerRead);
            CloseHandle(parentWrite);
            DeleteFileA(outputPath.c_str());
            return false;
        }

        STARTUPINFOA startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = workerRead;
        startup.hStdOutput = childOutput;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process = {};
        std::string command = "\"" + executablePath + "\" --worker";
        std::vector<char> commandBuffer(command.begin(), command.end());
        commandBuffer.push_back('\0');
        const BOOL created = CreateProcessA(
            executablePath.c_str(), &commandBuffer[0], NULL, NULL, TRUE, 0, NULL,
            NULL, &startup, &process);
        CloseHandle(workerRead);
        CloseHandle(childOutput);
        if (!created) {
            std::cerr << windowsError("CreateProcess").c_str() << '\n';
            CloseHandle(parentWrite);
            DeleteFileA(outputPath.c_str());
            return false;
        }
        const std::string inputText = input.str();
        std::size_t sent = 0;
        while (sent < inputText.size()) {
            const DWORD chunk = static_cast<DWORD>(
                (inputText.size() - sent) > 0x7fffffffU
                    ? 0x7fffffffU
                    : inputText.size() - sent);
            DWORD bytesWritten = 0;
            if (!WriteFile(parentWrite, inputText.data() + sent, chunk,
                           &bytesWritten, NULL) || bytesWritten == 0) {
                std::cerr << windowsError("WriteFile to worker pipe").c_str()
                          << '\n';
                CloseHandle(parentWrite);
                WaitForSingleObject(process.hProcess, INFINITE);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                DeleteFileA(outputPath.c_str());
                return false;
            }
            sent += bytesWritten;
        }
        CloseHandle(parentWrite);
        const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode = 1;
        const BOOL gotExitCode = GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (waitResult != WAIT_OBJECT_0 || !gotExitCode || exitCode != 0) {
            std::cerr << "Worker process did not finish successfully.\n";
            DeleteFileA(outputPath.c_str());
            return false;
        }

        std::string output;
        const bool readSuccess = readNativeFile(outputPath, output);
        DeleteFileA(outputPath.c_str());
        if (!readSuccess) return false;
        std::istringstream values(output);
        double value = 0.0;
        while (values >> value) results[index].push_back(value);
        if (results[index].size() != streams[index].size()) {
            std::cerr << "Worker returned an incomplete result stream.\n";
            return false;
        }
    }
#else
    for (std::size_t index = 0; index < streams.size(); ++index) {
        if (streams[index].empty()) {
            std::cerr << "A sensor stream is empty.\n";
            return false;
        }
        int channel[2];
        if (pipe(channel) != 0) {
            std::cerr << posixError("pipe") << '\n';
            return false;
        }
        const pid_t child = fork();
        if (child < 0) {
            std::cerr << posixError("fork") << '\n';
            close(channel[0]);
            close(channel[1]);
            return false;
        }
        if (child == 0) {
            close(channel[0]);
            KalmanFilter filter(streams[index][0], 1.0, 0.01, 1.0);
            for (std::size_t item = 0; item < streams[index].size(); ++item) {
                const double estimate = filter.update(streams[index][item]);
                if (!writeAll(channel[1], reinterpret_cast<const char*>(&estimate),
                              sizeof(estimate))) {
                    _exit(2);
                }
            }
            close(channel[1]);
            _exit(0);
        }

        close(channel[1]);
        results[index].resize(streams[index].size());
        const bool received = readAll(
            channel[0], reinterpret_cast<char*>(&results[index][0]),
            results[index].size() * sizeof(double));
        close(channel[0]);
        int status = 0;
        pid_t waited;
        do {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (!received || waited != child || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            std::cerr << "Child process failed to return a complete stream.\n";
            return false;
        }
    }
    (void)executablePath;
#endif
    return true;
}

// Filters the readings delivered to a Windows worker over its standard input.
int runWorkerMode() {
    std::string line;
    bool first = true;
    KalmanFilter* filter = NULL;
    while (std::getline(std::cin, line)) {
        char* end = NULL;
        const double measurement = std::strtod(line.c_str(), &end);
        if (end == line.c_str() || *end != '\0') {
            delete filter;
            return 2;
        }
        if (first) {
            filter = new KalmanFilter(measurement, 1.0, 0.01, 1.0);
            first = false;
        }
        std::cout << std::setprecision(17) << filter->update(measurement) << '\n';
        if (!std::cout) {
            delete filter;
            return 3;
        }
    }
    delete filter;
    return first ? 2 : 0;
}

// Exercises native file, process, memory, IPC, and permission APIs safely.
bool runSystemCallTests(const std::string& executablePath) {
    (void)executablePath;
    struct RunLogger {
        bool passed;
        ~RunLogger() {
            appendNativeFile(
                "output/execution_log.txt",
                std::string("Native system-call demonstrations: ") +
                    (passed ? "passed.\n" : "failed.\n"));
        }
    } runLogger = {false};

    std::cout << "\nRunning safe native system-call demonstrations...\n";
    const std::string testPath = "output/system_call_test.tmp";
    const TestFileCleanup cleanup(testPath);
    const std::string testData = "native file I/O test\n";
    std::string readBack;
    if (!writeNativeFile(testPath, testData) ||
        !readNativeFile(testPath, readBack) || readBack != testData) {
        std::cerr << "Native file I/O test failed.\n";
        return false;
    }
    std::cout << "Native file read/write: passed.\n";

#ifdef _WIN32
    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readPipe = NULL;
    HANDLE writePipe = NULL;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        std::cerr << windowsError("CreatePipe").c_str() << '\n';
        return false;
    }
    const char message[] = "pipe";
    DWORD transferred = 0;
    char received[sizeof(message)] = {};
    const BOOL pipeWritten = WriteFile(writePipe, message, sizeof(message),
                                       &transferred, NULL);
    const BOOL pipeRead = ReadFile(readPipe, received, sizeof(received),
                                   &transferred, NULL);
    const bool readPipeClosed = CloseHandle(readPipe) != 0;
    const bool writePipeClosed = CloseHandle(writePipe) != 0;
    if (!pipeWritten || !pipeRead || transferred != sizeof(message) ||
        !readPipeClosed || !writePipeClosed ||
        std::strcmp(message, received) != 0) {
        std::cerr << windowsError("CreatePipe transfer").c_str() << '\n';
        return false;
    }
    std::cout << "Anonymous pipe IPC: passed.\n";

    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
                                        PAGE_READWRITE, 0, 4096, NULL);
    if (mapping == NULL) {
        std::cerr << windowsError("CreateFileMapping").c_str() << '\n';
        return false;
    }
    char* mapped = static_cast<char*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096));
    if (mapped == NULL) {
        std::cerr << windowsError("MapViewOfFile").c_str() << '\n';
        CloseHandle(mapping);
        return false;
    }
    double* mappedTemperature = reinterpret_cast<double*>(mapped);
    *mappedTemperature = 25.5;
    const bool mappingOkay = *mappedTemperature == 25.5;
    const bool unmapped = UnmapViewOfFile(mapped) != 0;
    const bool mappingClosed = CloseHandle(mapping) != 0;
    if (!mappingOkay || !unmapped || !mappingClosed) {
        std::cerr << "File mapping validation or cleanup failed.\n";
        return false;
    }
    std::cout << "File mapping shared memory: passed.\n";

    void* allocated = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
    if (allocated == NULL) {
        std::cerr << windowsError("VirtualAlloc").c_str() << '\n';
        return false;
    }
    if (!VirtualFree(allocated, 0, MEM_RELEASE)) {
        std::cerr << windowsError("VirtualFree").c_str() << '\n';
        return false;
    }
    std::cout << "VirtualAlloc/VirtualFree: passed.\n";

    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    char command[] = "cmd.exe /c exit 0";
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &process)) {
        std::cerr << windowsError("CreateProcess").c_str() << '\n';
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    const BOOL gotExitCode = GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (waitResult != WAIT_OBJECT_0 || !gotExitCode || exitCode != 0) {
        std::cerr << "CreateProcess worker test failed.\n";
        return false;
    }
    std::cout << "CreateProcess/WaitForSingleObject: passed.\n";

    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:(A;;FA;;;OW)", SDDL_REVISION_1, &descriptor, NULL)) {
        std::cerr << windowsError("ConvertStringSecurityDescriptor").c_str()
                  << '\n';
        return false;
    }
    const BOOL securityOkay =
        SetFileSecurityA(testPath.c_str(), DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor);
    if (!securityOkay) {
        std::cerr << windowsError("SetFileSecurity (permission may be restricted)")
                  .c_str() << '\n';
        return false;
    } else {
        std::cout << "SetFileSecurity on the test file: passed.\n";
    }
#else
    bool permissionsOkay = true;
    int channel[2];
    if (pipe(channel) != 0) {
        std::cerr << posixError("pipe") << '\n';
        return false;
    }
    const char message[] = "pipe";
    char received[sizeof(message)] = {};
    const bool pipeOkay =
        writeAll(channel[1], message, sizeof(message)) &&
        readAll(channel[0], received, sizeof(received));
    close(channel[0]);
    close(channel[1]);
    if (!pipeOkay || std::strcmp(message, received) != 0) {
        std::cerr << "pipe IPC transfer failed.\n";
        return false;
    }
    std::cout << "pipe IPC: passed.\n";

    void* mapped = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        std::cerr << posixError("mmap") << '\n';
        return false;
    }
    double* mappedTemperature = static_cast<double*>(mapped);
    *mappedTemperature = 25.5;
    const bool mappingOkay = *mappedTemperature == 25.5;
    if (munmap(mapped, 4096) != 0 || !mappingOkay) {
        std::cerr << posixError("munmap") << '\n';
        return false;
    }
    std::cout << "mmap/munmap: passed.\n";

    const int sharedId = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    if (sharedId < 0) {
        std::cerr << posixError("shmget") << '\n';
        return false;
    }
    void* shared = shmat(sharedId, NULL, 0);
    if (shared == reinterpret_cast<void*>(-1)) {
        std::cerr << posixError("shmat") << '\n';
        shmctl(sharedId, IPC_RMID, NULL);
        return false;
    }
    std::strcpy(static_cast<char*>(shared), "shared sensor sample");
    const bool sharedOkay =
        std::strcmp(static_cast<char*>(shared), "shared sensor sample") == 0;
    const bool detached = shmdt(shared) == 0;
    const bool removed = shmctl(sharedId, IPC_RMID, NULL) == 0;
    if (!sharedOkay || !detached || !removed) {
        std::cerr << "System V shared memory cleanup or validation failed.\n";
        return false;
    }
    std::cout << "shmget/shmat/shmdt/shmctl: passed.\n";

    const pid_t child = fork();
    if (child < 0) {
        std::cerr << posixError("fork") << '\n';
        return false;
    }
    if (child == 0) {
        execl("/bin/true", "true", static_cast<char*>(NULL));
        _exit(127);
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "fork/exec/wait test failed.\n";
        return false;
    }
    std::cout << "fork/exec/wait: passed.\n";

    void* allocation = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (allocation == MAP_FAILED) {
        std::cerr << posixError("memory allocation mmap") << '\n';
        return false;
    }
    if (munmap(allocation, 4096) != 0) {
        std::cerr << posixError("memory munmap") << '\n';
        return false;
    }
    std::cout << "Anonymous mapped allocation/release: passed.\n";

    const std::string permissionPath = testPath + ".permissions";
    const TestFileCleanup permissionCleanup(permissionPath);
    const mode_t oldMask = umask(0077);
    const int permissionFile =
        open(permissionPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    const int createError = errno;
    umask(oldMask);
    if (permissionFile < 0) {
        errno = createError;
        std::cerr << posixError("open for umask test") << '\n';
        permissionsOkay = false;
    } else {
        struct stat permissionStatus = {};
        const bool closed = close(permissionFile) == 0;
        const bool inspected = stat(permissionPath.c_str(), &permissionStatus) == 0;
        if (!closed || !inspected ||
            (permissionStatus.st_mode & 0777) != 0600) {
            std::cerr << "umask did not create the expected 0600 test file.\n";
            permissionsOkay = false;
        } else {
            std::cout << "umask created a 0600 test file and was restored: passed.\n";
        }
    }
    if (chmod(testPath.c_str(), 0600) != 0) {
        std::cerr << posixError("chmod") << '\n';
        permissionsOkay = false;
    } else {
        std::cout << "chmod on the test file: passed.\n";
    }
    if (chown(testPath.c_str(), getuid(), getgid()) != 0) {
        std::cerr << posixError("chown (permissions may be restricted)") << '\n';
        permissionsOkay = false;
    } else {
        std::cout << "chown on the test file: passed.\n";
    }
    if (!permissionsOkay) {
        return false;
    }
#endif
    runLogger.passed = true;
    return true;
}

// Runs repeated measurements and records actual local timing/resource values.
bool savePerformanceResults(const std::string& path,
                            const std::string& executablePath) {
    const std::string testPath = "output/performance_test.tmp";
    const TestFileCleanup cleanup(testPath);
    const std::string sample(4096, 'x');
    std::ostringstream csv;
    const std::string compiler = compilerDescription();
    const std::string environment = environmentDescription();
#ifdef _WIN32
    const char* platform = "Windows";
#else
    const char* platform = "Linux";
#endif
    csv << "Trial,Operation,ElapsedMicroseconds,ProcessCpuMicroseconds,"
           "PeakWorkingSetBytes,Platform,Compiler,Environment\n";
    const int trials = 10;
    const char* operations[] = {
        "native_file_read_write",
        "independent_process_streams",
        "native_memory_allocation",
        "pipe_ipc_round_trip"
    };
    long long totals[4] = {0, 0, 0, 0};
    for (int trial = 1; trial <= trials; ++trial) {
        for (int operation = 0; operation < 4; ++operation) {
            const std::chrono::steady_clock::time_point start =
                std::chrono::steady_clock::now();
            bool success = true;
            if (operation == 0) {
                success = writeNativeFile(testPath, sample);
                std::string result;
                success = success && readNativeFile(testPath, result) &&
                          result == sample;
            } else if (operation == 1) {
                std::vector<std::vector<double> > streams;
                streams.push_back(std::vector<double>{21.0, 22.0, 21.5});
                streams.push_back(std::vector<double>{24.0, 23.5, 24.5});
                std::vector<std::vector<double> > results;
                success = processStreamsInProcesses(streams, results,
                                                    executablePath);
            } else if (operation == 2) {
#ifdef _WIN32
                void* memory = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT,
                                            PAGE_READWRITE);
                success = memory != NULL;
                if (memory != NULL) {
                    success = VirtualFree(memory, 0, MEM_RELEASE) != 0 && success;
                }
#else
                void* memory = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                success = memory != MAP_FAILED;
                if (memory != MAP_FAILED) {
                    success = munmap(memory, 4096) == 0 && success;
                }
#endif
            } else {
#ifdef _WIN32
                SECURITY_ATTRIBUTES security = {};
                security.nLength = sizeof(security);
                HANDLE readPipe = NULL;
                HANDLE writePipe = NULL;
                success = CreatePipe(&readPipe, &writePipe, &security, 0) != 0;
                if (success) {
                    const char sent[] = "ipc";
                    char received[sizeof(sent)] = {};
                    DWORD bytes = 0;
                    const BOOL wrote =
                        WriteFile(writePipe, sent, sizeof(sent), &bytes, NULL);
                    const BOOL readOkay =
                        ReadFile(readPipe, received, sizeof(received), &bytes, NULL);
                    success = wrote && readOkay &&
                              std::strcmp(sent, received) == 0;
                    CloseHandle(readPipe);
                    CloseHandle(writePipe);
                }
#else
                int channel[2];
                success = pipe(channel) == 0;
                if (success) {
                    const char sent[] = "ipc";
                    char received[sizeof(sent)] = {};
                    success = writeAll(channel[1], sent, sizeof(sent)) &&
                              readAll(channel[0], received, sizeof(received)) &&
                              std::strcmp(sent, received) == 0;
                    close(channel[0]);
                    close(channel[1]);
                }
#endif
            }
            const std::chrono::steady_clock::time_point end =
                std::chrono::steady_clock::now();
            if (!success) {
                std::cerr << "Performance trial failed: " << operations[operation]
                          << ".\n";
                return false;
            }
            const long long microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                    .count();
            totals[operation] += microseconds;
            long long cpuMicroseconds = 0;
            unsigned long long peakMemoryBytes = 0;
            resourceSnapshot(cpuMicroseconds, peakMemoryBytes);
            csv << trial << ',' << operations[operation] << ',' << microseconds
                << ',' << cpuMicroseconds << ',' << peakMemoryBytes << ','
                << platform << ',' << compiler << ',' << environment << '\n';
        }
    }
    for (int operation = 0; operation < 4; ++operation) {
        long long cpuMicroseconds = 0;
        unsigned long long peakMemoryBytes = 0;
        resourceSnapshot(cpuMicroseconds, peakMemoryBytes);
        csv << "Average," << operations[operation] << ','
            << (totals[operation] / trials) << ',' << cpuMicroseconds << ','
            << peakMemoryBytes << ',' << platform << ',' << compiler << ','
            << environment << '\n';
    }
    if (!writeNativeFile(path, csv.str())) {
        return false;
    }
    return true;
}
