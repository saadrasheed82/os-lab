#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H
#include <string>
#include <vector>

// Reads numeric readings from a text or CSV file using native file APIs.
bool readTemperatureFile(const std::string& path, std::vector<double>& readings);

// Writes raw and filtered readings as a CSV using native file APIs.
bool saveFilteredFile(const std::string& path,
                      const std::vector<double>& raw,
                      const std::vector<double>& filtered);

#endif
