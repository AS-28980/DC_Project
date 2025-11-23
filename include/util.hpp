#ifndef UTIL_HPP
#define UTIL_HPP

#include <random>
#include <vector>

struct RNG {
    std::mt19937 gen;

    explicit RNG(unsigned int seed) : gen(seed) {}

    double uniform_double(double a, double b) {
        std::uniform_real_distribution<double> dist(a, b);
        return dist(gen);
    }

    int uniform_int(int a, int b) {
        std::uniform_int_distribution<int> dist(a, b);
        return dist(gen);
    }
};

// Return index chosen from weights (or uniform if weights non-positive)
inline int weighted_choice(const std::vector<double> &weights, RNG &rng) {
    if (weights.empty()) return -1;

    double sum = 0.0;
    for (double w : weights) sum += w;

    // If all weights are zero or negative, fall back to uniform choice
    if (sum <= 0.0) {
        return rng.uniform_int(0, static_cast<int>(weights.size()) - 1);
    }

    double r = rng.uniform_double(0.0, sum);
    double acc = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        acc += weights[i];
        if (r <= acc) return static_cast<int>(i);
    }
    return static_cast<int>(weights.size() - 1);
}

#endif // UTIL_HPP
