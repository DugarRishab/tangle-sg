#include "../headers/tsa.h"
#include <climits>
#include <vector>
#include <string>
#include <unordered_map>
#include <random>
#include <numeric>
#include <ctime>
using namespace std;

//TSA checklist
//selectTips for default function
//selectTipsMCMC for MCMC TSA
//selectTipsGWW for Greedy Weighted Walk
//selectTipsURW for Unweighted Random Walk
//selectTipsWRW for Weighted Random Walk

vector<string> selectTips(Tangle& tangle) {
    vector<std::string> tips;
    int min_weight = INT_MAX;
    string weakest_tx = "";

    for (const auto& pair : tangle.transactions) {
        if (pair.second.validating_transactions.empty()) {
            tips.push_back(pair.first);
        } else if (pair.second.cumulative_weight < min_weight) {
            min_weight = pair.second.cumulative_weight;
            weakest_tx = pair.first;
        }
    }
    
    if (tips.empty() && !weakest_tx.empty()) {
        tips.push_back(weakest_tx);
    }
    return tips;
}

vector<string> selectTipsMCMC(Tangle& tangle, double alpha = 0.1) {
    srand(time(nullptr));
    
    // Handle empty tangle case
    if (tangle.transactions.empty()) {
        return {};
    }

    // 1. Build children mapping (transaction -> its direct approvers)
    unordered_map<string, vector<string>> children_map;
    for (const auto& pair : tangle.transactions) {
        const string& child = pair.first;
        for (const string& parent : pair.second.validating_transactions) {
            children_map[parent].push_back(child);
        }
    }

    // 2. Find genesis transaction (no parents)
    string genesis;
    for (const auto& pair : tangle.transactions) {
        if (pair.second.validating_transactions.empty()) {
            genesis = pair.first;
            break;
        }
    }

    // Fallback if no genesis found
    if (genesis.empty()) {
        return {"genesis_fallback"};
    }

    // 3. Perform two independent MCMC random walks
    vector<string> tips;
    for (int i = 0; i < 2; ++i) {  // Always select 2 tips
        string current = genesis;
        
        while (true) {
            // Get direct approvers of current transaction
            auto it = children_map.find(current);
            vector<string> approvers;
            if (it != children_map.end()) {
                approvers = it->second;
            }

            // Terminate walk if no approvers (reached tip)
            if (approvers.empty()) {
                break;
            }

            // 4. Calculate selection probabilities with numerical stability
            double max_weight = -1e300;
            for (const auto& tx : approvers) {
                double cw = tangle.transactions[tx].cumulative_weight;
                if (cw > max_weight) max_weight = cw;
            }

            double total_weight = 0.0;
            vector<double> weights;
            for (const auto& tx : approvers) {
                // Exponential bias toward higher weights
                double w = exp(alpha * (tangle.transactions[tx].cumulative_weight - max_weight));
                weights.push_back(w);
                total_weight += w;
            }

            // 5. Probabilistic selection (roulette wheel)
            double r = static_cast<double>(rand()) / RAND_MAX * total_weight;
            double cumulative = 0.0;
            for (int j = 0; j < approvers.size(); ++j) {
                cumulative += weights[j];
                if (r <= cumulative) {
                    current = approvers[j];
                    break;
                }
            }
        }
        tips.push_back(current);
    }
    return tips;
}

vector<string> selectTipsGWW(Tangle& tangle) {
    // Initialize random number generator
    random_device rd;
    mt19937 rng(rd());
    
    // Handle empty tangle case
    if (tangle.transactions.empty()) {
        return {};
    }

    // 1. Build children mapping (transaction -> its direct approvers)
    unordered_map<string, vector<string>> children_map;
    for (const auto& pair : tangle.transactions) {
        const string& child = pair.first;
        for (const string& parent : pair.second.validating_transactions) {
            children_map[parent].push_back(child);
        }
    }

    // 2. Find genesis transaction (no parents)
    string genesis;
    for (const auto& pair : tangle.transactions) {
        if (pair.second.validating_transactions.empty()) {
            genesis = pair.first;
            break;
        }
    }
    if (genesis.empty()) {
        return {"genesis_fallback"};
    }

    // 3. Perform two independent weighted random walks
    vector<string> tips;
    for (int i = 0; i < 2; ++i) {
        string current = genesis;
        
        while (true) {
            auto it = children_map.find(current);
            if (it == children_map.end() || it->second.empty()) {
                break;  // Reached a tip
            }
            
            const vector<string>& approvers = it->second;
            
            // Special case: single approver
            if (approvers.size() == 1) {
                current = approvers[0];
                continue;
            }

            // Calculate total cumulative weight
            double total_weight = 0.0;
            for (const auto& tx : approvers) {
                total_weight += tangle.transactions[tx].cumulative_weight;
            }

            // Generate random number in [0, total_weight)
            uniform_real_distribution<double> dist(0.0, total_weight);
            double r = dist(rng);
            
            // Select approver proportional to its weight
            double cumulative = 0.0;
            for (const auto& tx : approvers) {
                cumulative += tangle.transactions[tx].cumulative_weight;
                if (r <= cumulative) {
                    current = tx;
                    break;
                }
            }
        }
        tips.push_back(current);
    }
    return tips;
}

vector<string> selectTipsURW(Tangle& tangle) {
    // Initialize random engine -L> This makes the randomness unpredictable and is suitable for a DAG
    random_device rd;
    mt19937 rng(rd()); // the rng is used later to generate random numbers
    
    // Handle empty tangle
    if (tangle.transactions.empty()) {
        return {};
    }

    // Build children mapping (parent -> direct approvers)
    unordered_map<string, vector<string>> children_map;
    for (const auto& pair : tangle.transactions) {
        const string& child = pair.first;
        for (const string& parent : pair.second.validating_transactions) {
            children_map[parent].push_back(child);
        }
    }

    // Find genesis transaction (no parents)
    string genesis;
    for (const auto& pair : tangle.transactions) {
        if (pair.second.validating_transactions.empty()) {
            genesis = pair.first;
            break;
        }
    }
    if (genesis.empty()) {
        return {"genesis_fallback"}; //reminder to check what exception to raise for genesis fallback
    }

    // Perform two independent unweighted random walks
    vector<string> tips;
    for (int i = 0; i < 2; ++i) {
        string current = genesis;
        
        while (true) {
            auto it = children_map.find(current);
            if (it == children_map.end() || it->second.empty()) {
                break;  // Reached tip
            }
            
            // Uniform random selection
            const vector<string>& approvers = it->second;
            uniform_int_distribution<size_t> dist(0, approvers.size() - 1);
            size_t idx = dist(rng); //rng used to generate a random index
            current = approvers[idx];
        }
        tips.push_back(current);
    }
    return tips;
}

vector<string> selectTipsWRW(Tangle& tangle) {
    // 1. Initialize random number generator
    random_device rd;
    mt19937 rng(rd());

    // 2. Handle empty tangle case
    if (tangle.transactions.empty()) {
        return {};
    }

    // 3. Build parent->children mapping for forward traversal
    unordered_map<string, vector<string>> children_map;
    for (const auto& pair : tangle.transactions) {
        const string& child = pair.first;
        for (const string& parent : pair.second.validating_transactions) {
            children_map[parent].push_back(child);
        }
    }

    // 4. Find genesis transaction (no parents)
    string genesis;
    for (const auto& pair : tangle.transactions) {
        if (pair.second.validating_transactions.empty()) {
            genesis = pair.first;
            break;
        }
    }
    if (genesis.empty()) {
        return {"genesis_fallback"};
    }

    // 5. Perform two independent weighted random walks
    vector<string> tips;
    for (int i = 0; i < 2; ++i) {
        string current = genesis;
        
        while (true) {
            // 5a. Get direct approvers (children)
            auto it = children_map.find(current);
            if (it == children_map.end() || it->second.empty()) {
                break;  // Reached a tip
            }
            
            const vector<string>& approvers = it->second;
            
            // 5b. Special case: single approver
            if (approvers.size() == 1) {
                current = approvers[0];
                continue;
            }

            // 5c. Calculate total weight of approvers
            double total_weight = 0.0;
            for (const auto& tx : approvers) {
                total_weight += tangle.transactions[tx].cumulative_weight;
            }

            // 5d. Weighted random selection (roulette wheel)
            uniform_real_distribution<double> dist(0.0, total_weight);
            double r = dist(rng);
            
            double cumulative = 0.0;
            for (const auto& tx : approvers) {
                cumulative += tangle.transactions[tx].cumulative_weight;
                if (r <= cumulative) {
                    current = tx;
                    break;
                }
            }
        }
        tips.push_back(current);
    }
    return tips;
}