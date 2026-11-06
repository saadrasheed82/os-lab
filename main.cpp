#include "kalman_filter.h"
#include "sensor_data.h"
#include "system_calls.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const double DEFAULT_PROCESS_NOISE = 0.01;
const double DEFAULT_MEASUREMENT_NOISE = 1.0;

// Reads and checks the number of measurements requested by the user.
bool readCount(std::size_t& count) {
    if (!(std::cin >> count) || count == 0 || count > 100000) {
        std::cin.clear();
        std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
        std::cerr << "Enter a whole number between 1 and 100000.\n";
        return false;
    }
    return true;
}

// Runs one filter in order so each estimate uses the previous reading.
std::vector<double> filterReadings(const std::vector<double>& raw,
                                   double processNoise,
                                   double measurementNoise) {
    if (raw.empty()) {
        throw std::invalid_argument("There are no readings to filter.");
    }
    KalmanFilter filter(raw[0], 1.0, processNoise, measurementNoise);
    std::vector<double> filtered;
    filtered.reserve(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index) {
        filtered.push_back(filter.update(raw[index]));
    }
    return filtered;
}

// Displays the raw measurements beside their calculated estimates.
void displayResults(const std::vector<double>& raw,
                    const std::vector<double>& filtered) {
    if (raw.size() != filtered.size() || raw.empty()) {
        std::cout << "Filter the readings first.\n";
        return;
    }
    std::cout << "\nTime\tRaw temperature\tFiltered temperature\n";
    for (std::size_t index = 0; index < raw.size(); ++index) {
        std::cout << index + 1 << '\t' << std::fixed << std::setprecision(2)
                  << raw[index] << "\t\t" << filtered[index] << '\n';
    }
    std::cout << "Final estimate: " << filtered.back() << "\n";
}

// Prints the actions available in the console program.
void showMenu() {
    std::cout << "\nTEMPERATURE MONITORING SYSTEM\n"
                 "USING KALMAN FILTER\n\n"
                 "1. Enter temperature readings\n"
                 "2. Read readings from a file\n"
                 "3. Apply the Kalman Filter\n"
                 "4. Display filtered results\n"
                 "5. Save filtered results\n"
                 "6. Process independent streams using processes\n"
                 "7. Run native system-call demonstrations\n"
                 "8. Save local performance measurements\n"
                 "9. Exit\n"
                 "Choose an option: ";
}

// Appends completed user actions to the project execution log.
void logAction(const std::string& action) {
    if (!appendNativeFile("output/execution_log.txt", action + "\n")) {
        std::cerr << "Could not append to output/execution_log.txt.\n";
    }
}

// Replaces the current readings only after every manual value is valid.
void readManualReadings(std::vector<double>& raw) {
    std::size_t count = 0;
    std::cout << "Number of readings: ";
    if (!readCount(count)) {
        return;
    }
    std::vector<double> newReadings;
    newReadings.reserve(count);
    std::cout << "Enter " << count << " finite temperature values:\n";
    for (std::size_t index = 0; index < count; ++index) {
        double value = 0.0;
        if (!(std::cin >> value) || !std::isfinite(value)) {
            std::cin.clear();
            std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            std::cerr << "Invalid temperature. No readings were changed.\n";
            return;
        }
        newReadings.push_back(value);
    }
    raw.swap(newReadings);
}

// Adds a second independent sensor and processes both in separate processes.
bool processIndependentStreams(const std::vector<double>& raw,
                               const std::string& executablePath) {
    if (raw.empty()) {
        std::cout << "Enter or load readings first.\n";
        return false;
    }
    std::vector<std::vector<double> > streams(2, raw);
    for (std::size_t index = 0; index < streams[1].size(); ++index) {
        streams[1][index] += 2.0;
    }
    std::vector<std::vector<double> > results;
    if (!processStreamsInProcesses(streams, results, executablePath)) {
        std::cerr << "Independent stream processing failed.\n";
        return false;
    }
    std::cout << "Processed " << results.size()
              << " independent streams in separate processes.\n";
    for (std::size_t stream = 0; stream < results.size(); ++stream) {
        std::cout << "Stream " << stream + 1 << " final estimate: "
                  << std::fixed << std::setprecision(2)
                  << results[stream].back() << '\n';
    }
    return true;
}

int runSimpleInput() {
    std::cout << "TEMPERATURE MONITORING SYSTEM\n"
                 "USING KALMAN FILTER\n\n"
                 "Enter number of readings: ";

    std::size_t count = 0;
    if (!readCount(count)) {
        return 1;
    }

    std::vector<double> raw;
    raw.reserve(count);
    std::cout << "\nEnter temperature readings:\n";
    for (std::size_t index = 0; index < count; ++index) {
        double value = 0.0;
        if (!(std::cin >> value) || !std::isfinite(value)) {
            std::cerr << "Invalid temperature reading.\n";
            return 1;
        }
        raw.push_back(value);
    }

    const std::vector<double> filtered =
        filterReadings(raw, DEFAULT_PROCESS_NOISE, DEFAULT_MEASUREMENT_NOISE);
    std::cout << "\n-------------------------------\n"
                 "Raw Data      Filtered Data\n"
                 "-------------------------------\n";
    for (std::size_t index = 0; index < raw.size(); ++index) {
        std::cout << std::fixed << std::setprecision(1) << std::left
                  << std::setw(14) << raw[index] << filtered[index] << '\n';
    }
    std::cout << "-------------------------------\n";
    return 0;
}

}  // namespace

// Runs the optional menu for file input and system-call demonstrations.
int runMenu(int argc, char* argv[]) {
    std::vector<double> raw;
    std::vector<double> filtered;
    const std::string executablePath = argc > 0 ? argv[0] : "TemperatureMonitoring";

    for (;;) {
        showMenu();
        int choice = 0;
        if (!(std::cin >> choice)) {
            if (std::cin.eof()) {
                return 0;
            }
            std::cin.clear();
            std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            std::cerr << "Enter a menu number.\n";
            continue;
        }

        if (choice == 1) {
            readManualReadings(raw);
            filtered.clear();
            if (!raw.empty()) logAction("Entered manual temperature readings.");
        } else if (choice == 2) {
            std::string path;
            std::cout << "Input file [input/temperature_data.csv]: ";
            std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            std::getline(std::cin, path);
            if (path.empty()) {
                path = "input/temperature_data.csv";
            }
            if (readTemperatureFile(path, raw)) {
                filtered.clear();
                std::cout << "Loaded " << raw.size() << " readings.\n";
                logAction("Loaded temperature readings from " + path + ".");
            }
        } else if (choice == 3) {
            double processNoise = DEFAULT_PROCESS_NOISE;
            double measurementNoise = DEFAULT_MEASUREMENT_NOISE;
            std::cout << "Process noise Q (default 0.01): ";
            if (!(std::cin >> processNoise) || processNoise < 0.0 ||
                !std::isfinite(processNoise)) {
                std::cin.clear();
                std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
                std::cerr << "Q must be a finite non-negative number.\n";
                continue;
            }
            std::cout << "Measurement noise R (default 1.0): ";
            if (!(std::cin >> measurementNoise) || measurementNoise <= 0.0 ||
                !std::isfinite(measurementNoise)) {
                std::cin.clear();
                std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
                std::cerr << "R must be a finite positive number.\n";
                continue;
            }
            try {
                filtered = filterReadings(raw, processNoise, measurementNoise);
                displayResults(raw, filtered);
                logAction("Applied Kalman filter to " +
                          std::to_string(filtered.size()) + " readings.");
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
            }
        } else if (choice == 4) {
            displayResults(raw, filtered);
        } else if (choice == 5) {
            if (saveFilteredFile("output/filtered_results.csv", raw, filtered)) {
                std::cout << "Filtered results saved to output/filtered_results.csv.\n";
                logAction("Saved output/filtered_results.csv.");
            }
        } else if (choice == 6) {
            if (processIndependentStreams(raw, executablePath)) {
                logAction("Processed independent sensor streams.");
            }
        } else if (choice == 7) {
            if (!runSystemCallTests(executablePath)) {
                std::cerr << "One or more system-call demonstrations failed.\n";
            }
        } else if (choice == 8) {
            if (savePerformanceResults("output/performance_results.csv",
                                       executablePath)) {
                std::cout << "Measured results saved to output/performance_results.csv.\n";
                logAction("Saved output/performance_results.csv.");
            }
        } else if (choice == 9) {
            return 0;
        } else {
            std::cout << "Choose a number from 1 to 9.\n";
        }
    }
}

// Starts the simple input flow, or an optional worker/menu mode.
int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--worker") {
        return runWorkerMode();
    }
    if (argc > 1 && std::string(argv[1]) == "--menu") {
        return runMenu(argc, argv);
    }
    return runSimpleInput();
}
