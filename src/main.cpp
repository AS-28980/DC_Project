#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
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

    // small key=value config parser (ignores lines starting with '#')
    auto parseConfig = [](const std::string &path) {
        std::unordered_map<std::string, std::string> m;
        std::ifstream in(path);
        if (!in) return m;
        std::string line;
        while (std::getline(in, line)) {
            size_t i = 0;
            while (i < line.size() && isspace((unsigned char)line[i])) ++i;
            if (i == line.size()) continue;
            if (line[i] == '#') continue;
            size_t eq = line.find('=', i);
            if (eq == std::string::npos) continue;
            std::string key = line.substr(i, eq - i);
            std::string val = line.substr(eq + 1);
            while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
            size_t j = 0;
            while (j < val.size() && isspace((unsigned char)val[j])) ++j;
            val = val.substr(j);
            while (!val.empty() && isspace((unsigned char)val.back())) val.pop_back();
            if (!key.empty()) m[key] = val;
        }
        return m;
    };

    if (mode == "tangle") {
        if (argc < 3) {
            std::cout << "Run tangle mode with a parameter file:\n"
                      << "  " << argv[0] << " tangle <path-to-params>\n"
                      << "See config/tangle_params.ini for an example.\n";
            return 0;
        }

        auto cfg = parseConfig(argv[2]);

        // defaults
        int numProcesses = 10;
        double lambdaPerProcess = 0.3;
        double simDuration = 100.0;
        double minDelay = 1.0;
        double maxDelay = 5.0;
        TipSelectionMode selMode = TipSelectionMode::HYBRID;
        double securityBias = 0.7;
        double alphaHigh = 0.001;
        unsigned int seed = 42;
        std::string outputPath = "data/tangle_results.csv";

        try {
            if (cfg.count("numProcesses")) numProcesses = std::stoi(cfg.at("numProcesses"));
            if (cfg.count("lambdaPerProcess")) lambdaPerProcess = std::stod(cfg.at("lambdaPerProcess"));
            if (cfg.count("simDuration")) simDuration = std::stod(cfg.at("simDuration"));
            if (cfg.count("minDelay")) minDelay = std::stod(cfg.at("minDelay"));
            if (cfg.count("maxDelay")) maxDelay = std::stod(cfg.at("maxDelay"));
            if (cfg.count("selMode")) {
                std::string s = cfg.at("selMode");
                if (s == "HYBRID") selMode = TipSelectionMode::HYBRID;
                else if (s == "UNIFORM" || s == "RANDOM_ONLY") selMode = TipSelectionMode::RANDOM_ONLY;
                else if (s == "MCMC" || s == "MCMC_ONLY") selMode = TipSelectionMode::MCMC_ONLY;
            }
            if (cfg.count("securityBias")) securityBias = std::stod(cfg.at("securityBias"));
            if (cfg.count("alphaHigh")) alphaHigh = std::stod(cfg.at("alphaHigh"));
            if (cfg.count("seed")) seed = static_cast<unsigned int>(std::stoul(cfg.at("seed")));
            if (cfg.count("outputPath")) outputPath = cfg.at("outputPath");
        } catch (const std::exception &e) {
            std::cerr << "Error parsing config: " << e.what() << "\n";
            return 1;
        }

        std::cout << "Running tangle with: numProcesses=" << numProcesses
                  << " lambdaPerProcess=" << lambdaPerProcess
                  << " simDuration=" << simDuration << "\n";

        runTangleSimulation(numProcesses, lambdaPerProcess, simDuration,
                            minDelay, maxDelay, selMode,
                            securityBias, alphaHigh, seed, outputPath);

    } else if (mode == "witness") {
        if (argc < 3) {
            std::cout << "Run witness mode with a parameter file:\n"
                      << "  " << argv[0] << " witness <path-to-params>\n"
                      << "See config/witness_params.ini for an example.\n";
            return 0;
        }

        auto cfg = parseConfig(argv[2]);

        // defaults
        int numUsers = 100;
        double postProbPerStep = 0.02;
        double simDuration = 100.0;
        double minDelay = 1.0;
        double maxDelay = 5.0;
        int maxWitnesses = 3;
        unsigned int seed = 1337;
        std::string outputPath = "data/witness_results.csv";

        try {
            if (cfg.count("numUsers")) numUsers = std::stoi(cfg.at("numUsers"));
            if (cfg.count("postProbPerStep")) postProbPerStep = std::stod(cfg.at("postProbPerStep"));
            if (cfg.count("simDuration")) simDuration = std::stod(cfg.at("simDuration"));
            if (cfg.count("minDelay")) minDelay = std::stod(cfg.at("minDelay"));
            if (cfg.count("maxDelay")) maxDelay = std::stod(cfg.at("maxDelay"));
            if (cfg.count("maxWitnesses")) maxWitnesses = std::stoi(cfg.at("maxWitnesses"));
            if (cfg.count("seed")) seed = static_cast<unsigned int>(std::stoul(cfg.at("seed")));
            if (cfg.count("outputPath")) outputPath = cfg.at("outputPath");
        } catch (const std::exception &e) {
            std::cerr << "Error parsing config: " << e.what() << "\n";
            return 1;
        }

        std::cout << "Running witness with: numUsers=" << numUsers
                  << " postProbPerStep=" << postProbPerStep
                  << " simDuration=" << simDuration << "\n";

        runWitnessSimulation(numUsers, postProbPerStep, simDuration,
                             minDelay, maxDelay, maxWitnesses, seed, outputPath);
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }

    return 0;
}
