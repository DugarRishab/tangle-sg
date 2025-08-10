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
    std::vector<std::string> zeroWeightVerified;
    std::vector<std::string> otherVerified;
    std::string genesisTx = "genesis"; // Assuming you store genesis transaction ID here

    // Separate transactions into categories
    for (const auto &pair : tangle.transactions)
    {
        const auto &tx = pair.second;
        bool verified = !tx.metadata.signature1.empty() && !tx.metadata.signature2.empty();

        if (!verified)
            continue;

        if (tx.metadata.cumulative_weight == 0 && pair.first != genesisTx)
            zeroWeightVerified.push_back(pair.first);
        else if (pair.first != genesisTx)
            otherVerified.push_back(pair.first);
    }

    std::vector<std::string> selected;
    selected.reserve(numTips);

    // RNG
    static std::random_device rd;
    static std::mt19937 gen(rd());

    // 1. Pick from zero-weight verified
    if (!zeroWeightVerified.empty())
    {
        std::shuffle(zeroWeightVerified.begin(), zeroWeightVerified.end(), gen);
        int take = min(numTips, (int)zeroWeightVerified.size());
        selected.insert(selected.end(), zeroWeightVerified.begin(), zeroWeightVerified.begin() + take);
    }

    // 2. If not enough, pick from other verified
    if ((int)selected.size() < numTips && !otherVerified.empty())
    {
        std::shuffle(otherVerified.begin(), otherVerified.end(), gen);
        int remaining = numTips - (int)selected.size();
        int take = min(remaining, (int)otherVerified.size());
        selected.insert(selected.end(), otherVerified.begin(), otherVerified.begin() + take);
    }

    // 3. If still not enough, add genesis once (only if no verified tx at all)
    if (selected.empty() && tangle.transactions.find(genesisTx) != tangle.transactions.end())
    {
        selected.push_back(genesisTx);
    }
    else if ((int)selected.size() < numTips)
    {
        // Fill with genesis if needed, but not repeated
        if (find(selected.begin(), selected.end(), genesisTx) == selected.end())
            selected.push_back(genesisTx);
    }

    return selected;
}