#include "witness_sim.hpp"
#include "util.hpp"

#include <vector>
#include <unordered_set>
#include <queue>
#include <fstream>
#include <iostream>
#include <algorithm>

// ----------------------------
//  Data structures
// ----------------------------

struct WNode {
    int id;
    int owner;                  // user id (-1 for genesis)
    double timestamp;

    std::vector<int> parents;   // own parent + witnesses
    std::vector<int> children;

    bool isLeaf = true;
};

struct WProcess {
    int id;                     // user id
    int lastBlockId = -1;       // last own block (global id)
    std::unordered_set<int> knownBlocks; // ids
};

struct WMessage {
    double deliverTime;
    int receiverId;
    int nodeId;
};

struct WMessageCompare {
    bool operator()(const WMessage &a, const WMessage &b) const {
        return a.deliverTime > b.deliverTime;
    }
};

// ----------------------------
//  Global-ish state per run
// ----------------------------

static std::vector<WNode> g_wnodes;
static std::unordered_set<int> g_globalLeaves;
static int g_numUsers = 0;

// ----------------------------
//  Helper functions
// ----------------------------

static void processReceiveBlock(WProcess &proc, int nodeId) {
    proc.knownBlocks.insert(nodeId);
}

// Broadcast helper
static void broadcastNode(
    int nodeId,
    int senderId,
    std::vector<WProcess> &procs,
    std::priority_queue<WMessage, std::vector<WMessage>, WMessageCompare> &pq,
    RNG &rng,
    double now,
    double minDelay,
    double maxDelay
) {
    for (WProcess &p : procs) {
        if (p.id == senderId) continue;
        double delay = rng.uniform_double(minDelay, maxDelay);
        WMessage m;
        m.receiverId = p.id;
        m.nodeId = nodeId;
        m.deliverTime = now + delay;
        pq.push(m);
    }
}

// Select witnesses based on local knowledge:
// up to maxWitnesses latest blocks from other users
static std::vector<int> selectWitnesses(
    const WProcess &proc,
    int maxWitnesses
) {
    struct Cand {
        int owner;
        int blockId;
        double ts;
    };

    std::vector<int> bestBlock(g_numUsers, -1);
    std::vector<double> bestTs(g_numUsers, -1.0);

    // For each known block, track latest per owner (excluding self and genesis)
    for (int bid : proc.knownBlocks) {
        if (bid < 0 || bid >= static_cast<int>(g_wnodes.size())) continue;
        const WNode &n = g_wnodes[bid];
        if (n.owner < 0) continue;          // skip genesis
        if (n.owner == proc.id) continue;   // skip own chain
        if (n.timestamp > bestTs[n.owner]) {
            bestTs[n.owner] = n.timestamp;
            bestBlock[n.owner] = bid;
        }
    }

    std::vector<Cand> cands;
    for (int owner = 0; owner < g_numUsers; ++owner) {
        if (owner == proc.id) continue;
        if (bestBlock[owner] != -1) {
            cands.push_back({owner, bestBlock[owner], bestTs[owner]});
        }
    }

    // Sort by recency (descending)
    std::sort(cands.begin(), cands.end(),
              [](const Cand &a, const Cand &b) {
                  return a.ts > b.ts;
              });

    std::vector<int> witnesses;
    for (const Cand &c : cands) {
        if (static_cast<int>(witnesses.size()) >= maxWitnesses) break;
        witnesses.push_back(c.blockId);
    }
    return witnesses;
}

// ----------------------------
//  Simulation
// ----------------------------

void runWitnessSimulation(
    int numUsers,
    double postProbPerStep,
    double simDuration,
    double minDelay,
    double maxDelay,
    int maxWitnesses,
    unsigned int seed,
    const std::string &outputPath
) {
    RNG rng(seed);
    g_wnodes.clear();
    g_globalLeaves.clear();
    g_numUsers = numUsers;

    // Create processes/users
    std::vector<WProcess> procs(numUsers);
    for (int i = 0; i < numUsers; ++i) {
        procs[i].id = i;
    }

    // Create a global genesis node (owner = -1)
    WNode genesis;
    genesis.id = 0;
    genesis.owner = -1;
    genesis.timestamp = 0.0;
    genesis.isLeaf = true;
    g_wnodes.push_back(genesis);
    g_globalLeaves.insert(0);

    // Initially, everyone knows genesis
    for (WProcess &pr : procs) {
        pr.knownBlocks.insert(0);
    }

    // Message queue
    std::priority_queue<WMessage, std::vector<WMessage>, WMessageCompare> pq;

    double dt = 1.0;
    double now = 0.0;

    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "Failed to open output file: " << outputPath << "\n";
        return;
    }
    out << "time,global_leaves,total_nodes\n";

    while (now <= simDuration) {
        // 1) Deliver messages
        while (!pq.empty() && pq.top().deliverTime <= now) {
            WMessage m = pq.top();
            pq.pop();
            if (m.nodeId < 0 || m.nodeId >= static_cast<int>(g_wnodes.size())) continue;
            WProcess &pr = procs[m.receiverId];
            processReceiveBlock(pr, m.nodeId);
        }

        // 2) Each user may post a block
        for (WProcess &pr : procs) {
            double r = rng.uniform_double(0.0, 1.0);
            if (r < postProbPerStep) {
                int newId = static_cast<int>(g_wnodes.size());
                WNode node;
                node.id = newId;
                node.owner = pr.id;
                node.timestamp = now;
                node.isLeaf = true;

                // Own chain parent: last own block or genesis
                if (pr.lastBlockId != -1) {
                    node.parents.push_back(pr.lastBlockId);
                } else {
                    node.parents.push_back(0); // attach first block to genesis
                }

                // Witnesses based on local view
                auto witnesses = selectWitnesses(pr, maxWitnesses);
                for (int w : witnesses) {
                    if (w != node.parents[0]) {
                        node.parents.push_back(w);
                    }
                }

                g_wnodes.push_back(node);

                // Update parents' children & leaf status
                for (int p : g_wnodes[newId].parents) {
                    g_wnodes[p].children.push_back(newId);
                    if (g_wnodes[p].isLeaf) {
                        g_wnodes[p].isLeaf = false;
                        g_globalLeaves.erase(p);
                    }
                }

                // New node is a leaf
                g_wnodes[newId].isLeaf = true;
                g_globalLeaves.insert(newId);

                // Update user state
                pr.lastBlockId = newId;
                pr.knownBlocks.insert(newId);

                // Broadcast to others
                broadcastNode(newId, pr.id, procs, pq, rng, now, minDelay, maxDelay);
            }
        }

        // 3) Log global width (number of leaves) and total nodes
        int leaves = static_cast<int>(g_globalLeaves.size());
        int totalNodes = static_cast<int>(g_wnodes.size());
        out << now << "," << leaves << "," << totalNodes << "\n";

        now += dt;
    }

    std::cout << "Witness simulation finished. Total nodes: "
              << g_wnodes.size() << ". Output: " << outputPath << "\n";
}
