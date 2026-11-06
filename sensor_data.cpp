#include "sensor_data.h"
#include "system_calls.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

// Loads and validates readings before replacing the caller's existing data.
bool readTemperatureFile(const std::string& path, std::vector<double>& readings) {
    std::string contents;
    if (!readNativeFile(path, contents)) {
        return false;
    }

    std::istringstream input(contents);
    std::string line;
    std::vector<double> parsedReadings;
    std::size_t lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        while (!line.empty() &&
               (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ' ||
                line[line.size() - 1] == '\t')) {
            line.erase(line.size() - 1);
        }
        if (line.empty()) {
            continue;
        }

        const std::size_t comma = line.find_last_of(',');
        std::string valueText =
            comma == std::string::npos ? line : line.substr(comma + 1);
        while (!valueText.empty() &&
               (valueText[0] == ' ' || valueText[0] == '\t')) {
            valueText.erase(0, 1);
        }
        char* end = 0;
        const double value = std::strtod(valueText.c_str(), &end);

        if (end == valueText.c_str() || *end != '\0' || !std::isfinite(value)) {
            std::string header = valueText;
            std::transform(header.begin(), header.end(), header.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
            if (lineNumber == 1 && header.find("temp") != std::string::npos) {
                continue;
            }
            std::cerr << "Invalid temperature on line " << lineNumber << ".\n";
            return false;
        }
        parsedReadings.push_back(value);
    }

    if (parsedReadings.empty()) {
        std::cerr << "The input file contains no temperature readings.\n";
        return false;
    }
    readings.swap(parsedReadings);
    return true;
}

// Saves each input and its corresponding filtered estimate in CSV format.
bool saveFilteredFile(const std::string& path,
                      const std::vector<double>& raw,
                      const std::vector<double>& filtered) {
    if (raw.size() != filtered.size() || raw.empty()) {
        std::cerr << "Cannot save results: raw and filtered readings do not match.\n";
        return false;
    }

    std::ostringstream output;
    output << "Reading,RawTemperature,FilteredTemperature\n";
    for (std::size_t index = 0; index < raw.size(); ++index) {
        output << index + 1 << ',' << raw[index] << ',' << filtered[index] << '\n';
    }
    return writeNativeFile(path, output.str());
}
