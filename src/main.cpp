#include <iostream>
#include <string>
#include "tangle_sim.hpp"
#include "witness_sim.hpp"

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage:\n"
                  << "  " << argv[0] << " tangle\n"
                  << "  " << argv[0] << " witness\n";
        return 0;
    }

    std::string mode = argv[1];

    if (mode == "tangle") {
        int numProcesses = 10;
        double lambdaPerProcess = 0.3;   // tx per time unit per process
        double simDuration = 100.0; //5000.0;     // time units
        double minDelay = 1.0;
        double maxDelay = 5.0;
        TipSelectionMode selMode = TipSelectionMode::HYBRID;
        double securityBias = 0.7;       // probability of using MCMC
        double alphaHigh = 0.001;
        unsigned int seed = 42;
        std::string outputPath = "../data/tangle_results.csv";

        runTangleSimulation(numProcesses, lambdaPerProcess, simDuration,
                            minDelay, maxDelay, selMode,
                            securityBias, alphaHigh, seed, outputPath);

    } else if (mode == "witness") {
        int numUsers = 100;
        double postProbPerStep = 0.02;   // per user per step
        double simDuration = 100.0; //5000.0;
        double minDelay = 1.0;
        double maxDelay = 5.0;
        int maxWitnesses = 3;
        unsigned int seed = 1337;
        std::string outputPath = "../data/witness_results.csv";

        runWitnessSimulation(numUsers, postProbPerStep, simDuration,
                             minDelay, maxDelay, maxWitnesses, seed, outputPath);
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }

    return 0;
}
