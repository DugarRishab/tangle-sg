#include "../headers/tsa.h"
#include <climits>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <unordered_set>

using namespace std;

std::vector<std::string> selectTips(Tangle &tangle, int numTips)
{
    std::string genesisTx = "genesis";

    if (numTips <= 0) {
        return {genesisTx};
    }

    // Gather all FINAL transactions as potential MCMC starting points
    auto allTx = tangle.getAllTransactions();
    std::vector<std::string> finalTransactions;
    for (const auto& [tx_id, tx] : allTx) {
        if (tx.metadata.status == TransactionStatus::FINAL && tx_id != genesisTx) {
            finalTransactions.push_back(tx_id);
        }
    }

    if (finalTransactions.empty()) {
        return {genesisTx};
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());

    std::unordered_set<std::string> selected;
    const int maxWalks = numTips * 10;
    const int maxSteps = 50;

    for (int walk = 0; walk < maxWalks && (int)selected.size() < numTips; ++walk) {
        // Step 1: pick a random verified (FINAL) starting point
        std::uniform_int_distribution<size_t> startDist(0, finalTransactions.size() - 1);
        std::string current = finalTransactions[startDist(gen)];

        // Step 2: walk toward tips through the FINAL subgraph
        for (int step = 0; step < maxSteps; ++step) {
            auto children = tangle.getChildren(current, TransactionStatus::FINAL);

            if (children.empty()) {
                // Reached a tip in the FINAL subgraph
                break;
            }

            // Transition probability ∝ cumulative_weight + 1
            std::vector<double> weights;
            weights.reserve(children.size());
            double totalWeight = 0.0;

            for (const auto& childId : children) {
                Transaction childTx = tangle.getTransaction(childId);
                double w = static_cast<double>(childTx.metadata.cumulative_weight) + 1.0;
                weights.push_back(w);
                totalWeight += w;
            }

            if (totalWeight <= 0.0) {
                break;
            }

            double r = std::uniform_real_distribution<double>(0.0, totalWeight)(gen);
            double cum = 0.0;
            for (size_t i = 0; i < children.size(); ++i) {
                cum += weights[i];
                if (r <= cum) {
                    current = children[i];
                    break;
                }
            }
        }

        // Only keep endpoints that are actually FINAL tips
        if (tangle.isTip(current) && tangle.getTransaction(current).metadata.status == TransactionStatus::FINAL) {
            selected.insert(current);
        }
    }

    std::vector<std::string> result(selected.begin(), selected.end());

    // Fallback: if MCMC didn't find enough, fill from the direct FINAL tip pool
    if ((int)result.size() < numTips) {
        auto allTips = tangle.getTips(TransactionStatus::FINAL);
        std::vector<std::string> remaining;
        for (const auto& tip : allTips) {
            if (selected.find(tip) == selected.end() && tip != genesisTx) {
                remaining.push_back(tip);
            }
        }

        std::shuffle(remaining.begin(), remaining.end(), gen);
        for (const auto& tip : remaining) {
            if ((int)result.size() >= numTips) break;
            result.push_back(tip);
        }
    }

    // Pad with genesis if still not enough
    if ((int)result.size() < numTips) {
        if (std::find(result.begin(), result.end(), genesisTx) == result.end()) {
            result.push_back(genesisTx);
        }
    }

    return result;
}