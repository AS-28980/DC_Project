#include "tangle_sim.hpp"
#include "util.hpp"

#include <vector>
#include <unordered_set>
#include <queue>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>

// Data structures

struct TxNode {
    int id;
    double timestamp;
    int height;

    std::vector<int> parents;
    std::vector<int> children;
};

struct Process {
    int id;
    std::unordered_set<int> knownNodes;
    std::unordered_set<int> tipSet;
};

struct Message {
    double deliverTime;
    int receiverId;
    int nodeId;
};

struct MessageCompare {
    bool operator()(const Message &a, const Message &b) const {
        return a.deliverTime > b.deliverTime;
    }
};

// Global state

static std::vector<TxNode> g_nodes;
static std::unordered_set<int> g_globalTips;

// message overhead (new metric)
static long long g_messagesSent = 0;

// Update when process receives node
static void processReceiveNode(Process &proc, int nodeId) {
    if (!proc.knownNodes.insert(nodeId).second) return;

    for (int p : g_nodes[nodeId].parents) {
        if (proc.knownNodes.count(p)) proc.tipSet.erase(p);
    }

    bool hasKnownChild = false;
    for (int c : g_nodes[nodeId].children) {
        if (proc.knownNodes.count(c)) { hasKnownChild = true; break; }
    }
    if (!hasKnownChild) proc.tipSet.insert(nodeId);
}

// Uniform random tip
static int uniformRandomTip(const Process &proc, RNG &rng) {
    if (proc.tipSet.empty()) return 0;
    int n = static_cast<int>(proc.tipSet.size());
    int k = rng.uniform_int(0, n - 1);
    auto it = proc.tipSet.begin();
    std::advance(it, k);
    return *it;
}

// Biased random walk
static int biasedRandomWalk(const Process &proc, RNG &rng, double alpha) {
    int current = 0;

    while (true) {
        const auto &children = g_nodes[current].children;
        std::vector<int> knownChildren;
        knownChildren.reserve(children.size());
        for (int c : children) {
            if (proc.knownNodes.count(c)) {
                knownChildren.push_back(c);
            }
        }

        if (knownChildren.empty()) {
            return current;
        }

        std::vector<double> weights(knownChildren.size());
        for (std::size_t i = 0; i < knownChildren.size(); ++i) {
            int childId = knownChildren[i];
            int h = g_nodes[childId].height;
            weights[i] = std::exp(alpha * static_cast<double>(h));
        }

        int idx = weighted_choice(weights, rng);
        if (idx < 0) return knownChildren[0];
        current = knownChildren[idx];
    }
}

static std::vector<int> selectTips(
    const Process &proc,
    RNG &rng,
    TipSelectionMode mode,
    double securityBias,
    double alphaHigh,
    int numTips
) {
    std::vector<int> tips;
    tips.reserve(numTips);

    for (int i = 0; i < numTips; ++i) {
        int tip = 0;

        if (mode == TipSelectionMode::RANDOM_ONLY) {
            tip = uniformRandomTip(proc, rng);
        } else if (mode == TipSelectionMode::MCMC_ONLY) {
            tip = biasedRandomWalk(proc, rng, alphaHigh);
        } else {
            double r = rng.uniform_double(0.0, 1.0);
            if (r < securityBias) {
                tip = biasedRandomWalk(proc, rng, alphaHigh);
            } else {
                tip = uniformRandomTip(proc, rng);
            }
        }

        tips.push_back(tip);
    }

    return tips;
}

// Broadcast
static void broadcastNode(
    int nodeId,
    int senderId,
    std::vector<Process> &procs,
    std::priority_queue<Message, std::vector<Message>, MessageCompare> &pq,
    RNG &rng,
    double now,
    double minDelay,
    double maxDelay
) {
    for (Process &p : procs) {
        if (p.id == senderId) continue;
        double delay = rng.uniform_double(minDelay, maxDelay);
        Message m{now + delay, p.id, nodeId};
        pq.push(m);

        g_messagesSent += 1;
    }
}

// Simulation

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
) {
    RNG rng(seed);
    g_nodes.clear();
    g_globalTips.clear();
    g_messagesSent = 0;

    // Genesis
    TxNode genesis;
    genesis.id = 0;
    genesis.timestamp = 0.0;
    genesis.height = 0;
    g_nodes.push_back(genesis);
    g_globalTips.insert(0);

    // Processes
    std::vector<Process> procs(numProcesses);
    for (int i = 0; i < numProcesses; ++i) {
        procs[i].id = i;
        procs[i].knownNodes.insert(0);
        procs[i].tipSet.insert(0);
    }

    std::priority_queue<Message, std::vector<Message>, MessageCompare> pq;

    double dt = 1.0;
    double now = 0.0;

    double txProb = lambdaPerProcess * dt;
    if (txProb > 1.0) txProb = 1.0;

    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "Failed to open output file: " << outputPath << "\n";
        return;
    }

    out << "time,global_tips,avg_local_tips,min_local_tips,max_local_tips,"
           "total_nodes,tip_ratio,messages_sent\n";

    while (now <= simDuration) {
        // Deliver messages
        while (!pq.empty() && pq.top().deliverTime <= now) {
            Message m = pq.top();
            pq.pop();
            if (m.nodeId < 0 || m.nodeId >= static_cast<int>(g_nodes.size())) continue;

            Process &pr = procs[m.receiverId];
            processReceiveNode(pr, m.nodeId);
        }

        // Create transactions
        for (Process &pr : procs) {
            double r = rng.uniform_double(0.0, 1.0);
            if (r < txProb) {
                int newId = static_cast<int>(g_nodes.size());
                TxNode node;
                node.id = newId;
                node.timestamp = now;

                auto tips = selectTips(pr, rng, mode, securityBias, alphaHigh, 2);
                node.parents = tips;

                int maxH = 0;
                for (int p : tips) {
                    int h = g_nodes[p].height;
                    if (h > maxH) maxH = h;
                }
                node.height = maxH + 1;

                g_nodes.push_back(node);

                for (int p : tips) {
                    g_nodes[p].children.push_back(newId);
                    g_globalTips.erase(p);
                }

                g_globalTips.insert(newId);

                processReceiveNode(pr, newId);

                broadcastNode(newId, pr.id, procs, pq, rng, now, minDelay, maxDelay);
            }
        }

        // Metrics
        int totalLocalTips = 0;
        int minLocalTips = std::numeric_limits<int>::max();
        int maxLocalTips = 0;

        for (const Process &pr : procs) {
            int c = static_cast<int>(pr.tipSet.size());
            totalLocalTips += c;
            if (c < minLocalTips) minLocalTips = c;
            if (c > maxLocalTips) maxLocalTips = c;
        }

        double avgLocalTips = static_cast<double>(totalLocalTips) / numProcesses;
        int totalNodes = static_cast<int>(g_nodes.size());
        int globalTips = static_cast<int>(g_globalTips.size());

        double tipRatio = (totalNodes > 0 ? 
            (double)globalTips / (double)totalNodes : 0.0);

        out << now << ","
            << globalTips << ","
            << avgLocalTips << ","
            << minLocalTips << ","
            << maxLocalTips << ","
            << totalNodes << ","
            << tipRatio << ","
            << g_messagesSent
            << "\n";

        if (static_cast<int>(now) % 1000 == 0) {
            std::cout << "[Tangle] time=" << now
                      << " total_nodes=" << totalNodes
                      << " global_tips=" << globalTips << "\n";
        }

        now += dt;
    }

    std::cout << "Tangle simulation finished. Total nodes: "
              << g_nodes.size() << ". Output: " << outputPath << "\n";
}
