#ifndef KALMAN_FILTER_H
#define KALMAN_FILTER_H

// Keeps the state and settings for one scalar temperature Kalman filter.
class KalmanFilter {
public:
    // Sets the first estimate, its uncertainty, and the two noise values.
    KalmanFilter(double initialEstimate, double initialError,
                 double processNoise, double measurementNoise);

    // Predicts the next temperature, then corrects it with a sensor reading.
    double update(double measurement);

private:
    double estimate;
    double error;
    double processNoise;
    double measurementNoise;
};

#endif
