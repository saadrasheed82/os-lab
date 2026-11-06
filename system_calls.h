#ifndef SYSTEM_CALLS_H
#define SYSTEM_CALLS_H

#include <string>
#include <vector>

bool readNativeFile(const std::string& path, std::string& contents);
bool writeNativeFile(const std::string& path, const std::string& contents);
bool appendNativeFile(const std::string& path, const std::string& contents);

// Processes each independent sensor stream in a separate operating-system process.
bool processStreamsInProcesses(const std::vector<std::vector<double>>& streams,
                               std::vector<std::vector<double>>& results,
                               const std::string& executablePath);

// Runs small, safe demonstrations of the required native API categories.
bool runSystemCallTests(const std::string& executablePath);

// Saves repeated local measurements; values are measured on this machine only.
bool savePerformanceResults(const std::string& path,
                            const std::string& executablePath);

// Entry point used by CreateProcess workers on Windows.
int runWorkerMode();

#endif
