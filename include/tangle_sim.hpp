#ifndef TANGLE_SIM_HPP
#define TANGLE_SIM_HPP

#include <string>

enum class TipSelectionMode {
    RANDOM_ONLY,
    MCMC_ONLY,
    HYBRID
};

void runTangleSimulation(
    int numProcesses,
    double lambdaPerProcess,
    double simDuration,
    double minDelay,
    double maxDelay,
    TipSelectionMode mode,
    double securityBias,
    double alphaHigh,
    unsigned int seed,
    const std::string &outputPath
);

#endif // TANGLE_SIM_HPP
