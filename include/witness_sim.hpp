#ifndef WITNESS_SIM_HPP
#define WITNESS_SIM_HPP

#include <string>

void runWitnessSimulation(
    int numUsers,
    double postProbPerStep,
    double simDuration,
    double minDelay,
    double maxDelay,
    int maxWitnesses,
    unsigned int seed,
    const std::string &outputPath
);

#endif // WITNESS_SIM_HPP
