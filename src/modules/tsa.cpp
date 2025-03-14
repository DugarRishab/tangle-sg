#include "../headers/tsa.h"
#include <climits>

std::vector<std::string> selectTips(Tangle& tangle) {
    std::vector<std::string> tips;
    int min_weight = INT_MAX;
    std::string weakest_tx = "";

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