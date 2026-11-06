#include "kalman_filter.h"

#include <stdexcept>

KalmanFilter::KalmanFilter(double initialEstimate, double initialError,
                           double processNoiseValue, double measurementNoiseValue)
    : estimate(initialEstimate),
      error(initialError),
      processNoise(processNoiseValue),
      measurementNoise(measurementNoiseValue) {
    if (initialError < 0.0 || processNoiseValue < 0.0 ||
        measurementNoiseValue <= 0.0) {
        throw std::invalid_argument(
            "Error covariance and process noise must be non-negative; "
            "measurement noise must be positive.");
    }
}

double KalmanFilter::update(double measurement) {
    const double predictedEstimate = estimate;
    const double predictedError = error + processNoise;
    const double gain = predictedError / (predictedError + measurementNoise);

    estimate = predictedEstimate + gain * (measurement - predictedEstimate);
    error = (1.0 - gain) * predictedError;
    return estimate;
}
