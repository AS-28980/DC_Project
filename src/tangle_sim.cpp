#include "tangle_sim.hpp"
#include "util.hpp"

#include <vector>
#include <unordered_set>
#include <queue>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>

// ----------------------------
//  Data structures
// ----------------------------

struct TxNode {
    int id;
    double timestamp;
    int height;                 // distance from genesis (approx depth)

    std::vector<int> parents;
    std::vector<int> children;
};

struct Process {
    int id;
    std::unordered_set<int> knownNodes;  // ids
    std::unordered_set<int> tipSet;      // tips in local view
};

struct Message {
    double deliverTime;
    int receiverId;
    int nodeId;
};

struct MessageCompare {
    bool operator()(const Message &a, const Message &b) const {
        return a.deliverTime > b.deliverTime; // min-heap by time
    }
};

// ----------------------------
//  Global-ish state per run
// ----------------------------

static std::vector<TxNode> g_nodes;
static std::unordered_set<int> g_globalTips;   // global tips (true DAG width)

// ----------------------------
//  Helper functions
// ----------------------------

static void processReceiveNode(Process &proc, int nodeId) {
    // If already known, nothing to do
    if (!proc.knownNodes.insert(nodeId).second) return;

    // Parents that are known can no longer be tips in this local view
    for (int p : g_nodes[nodeId].parents) {
        if (proc.knownNodes.count(p)) {
            proc.tipSet.erase(p);
        }
    }

    // If this node has no known children, it's a tip in this process's view
    bool hasKnownChild = false;
    for (int c : g_nodes[nodeId].children) {
        if (proc.knownNodes.count(c)) {
            hasKnownChild = true;
            break;
        }
    }
    if (!hasKnownChild) {
        proc.tipSet.insert(nodeId);
    }
}

// Uniform random tip from process's local tip set
static int uniformRandomTip(const Process &proc, RNG &rng) {
    if (proc.tipSet.empty()) {
        // Fallback: if no tips known, attach to genesis
        return 0;
    }
    int n = static_cast<int>(proc.tipSet.size());
    int k = rng.uniform_int(0, n - 1);
    auto it = proc.tipSet.begin();
    std::advance(it, k);
    return *it;
}

// Biased random walk from genesis using local view:
// bias towards higher height children.
static int biasedRandomWalk(const Process &proc, RNG &rng, double alpha) {
    int current = 0;  // genesis

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
            return current; // leaf in local view
        }

        std::vector<double> weights(knownChildren.size());
        for (std::size_t i = 0; i < knownChildren.size(); ++i) {
            int childId = knownChildren[i];
            int h = g_nodes[childId].height;
            // Bias towards higher height (deeper nodes): exp(alpha * h)
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
        } else { // HYBRID
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

// Broadcast node to all other processes with random delay
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
        Message m;
        m.nodeId = nodeId;
        m.receiverId = p.id;
        m.deliverTime = now + delay;
        pq.push(m);
    }
}

// ----------------------------
//  Simulation
// ----------------------------

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

    // Create genesis tx
    TxNode genesis;
    genesis.id = 0;
    genesis.timestamp = 0.0;
    genesis.height = 0;
    g_nodes.push_back(genesis);
    g_globalTips.insert(0);

    // Create processes
    std::vector<Process> procs(numProcesses);
    for (int i = 0; i < numProcesses; ++i) {
        procs[i].id = i;
        procs[i].knownNodes.insert(0);
        procs[i].tipSet.insert(0);
    }

    // Message queue
    std::priority_queue<Message, std::vector<Message>, MessageCompare> pq;

    double dt = 1.0;
    double now = 0.0;

    // Probability of tx per process per step
    double txProb = lambdaPerProcess * dt;
    if (txProb > 1.0) txProb = 1.0; // clamp

    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "Failed to open output file: " << outputPath << "\n";
        return;
    }
    // global_tips = true DAG width; local_* = width as seen by processes
    out << "time,global_tips,avg_local_tips,min_local_tips,max_local_tips,total_nodes\n";

    while (now <= simDuration) {
        // 1) Deliver messages due by 'now'
        while (!pq.empty() && pq.top().deliverTime <= now) {
            Message m = pq.top();
            pq.pop();
            if (m.nodeId < 0 || m.nodeId >= static_cast<int>(g_nodes.size())) continue;

            Process &pr = procs[m.receiverId];
            processReceiveNode(pr, m.nodeId);
        }

        // 2) Each process may generate a transaction this step
        for (Process &pr : procs) {
            double r = rng.uniform_double(0.0, 1.0);
            if (r < txProb) {
                int newId = static_cast<int>(g_nodes.size());
                TxNode node;
                node.id = newId;
                node.timestamp = now;

                // Select two tips in local view
                auto tips = selectTips(pr, rng, mode, securityBias, alphaHigh, 2);
                node.parents = tips;

                // height = 1 + max parent height
                int maxH = 0;
                for (int p : tips) {
                    int h = g_nodes[p].height;
                    if (h > maxH) maxH = h;
                }
                node.height = maxH + 1;

                g_nodes.push_back(node);

                // Update parents' children and global tips
                for (int p : tips) {
                    g_nodes[p].children.push_back(newId);
                    g_globalTips.erase(p);
                }

                // New node is a global tip
                g_globalTips.insert(newId);

                // Sender learns its own node immediately
                processReceiveNode(pr, newId);

                // Broadcast to others
                broadcastNode(newId, pr.id, procs, pq, rng, now, minDelay, maxDelay);
            }
        }

        // 3) Log metrics: global width + local tip statistics
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

        out << now << ","
            << globalTips << ","
            << avgLocalTips << ","
            << minLocalTips << ","
            << maxLocalTips << ","
            << totalNodes << "\n";

        // Light progress print so you know it's not hung
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
