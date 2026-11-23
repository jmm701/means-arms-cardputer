#pragma once

#include "model.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace means {

struct ComponentCosts {
    double bulletCostPerRound = 0.0;
    double powderCostPerRound = 0.0;
    double primerCostPerRound = 0.0;
    double caseCostPerRound = 0.0;
    double totalCostPerRound = 0.0;
    double retailCostPerRound = 0.0;
    double savingsPerRound = 0.0;
    double savingsPercent = 0.0;
    double powderLoadsPerBulkUnit = 0.0;
};

struct SavingsRow {
    int quantity = 0;
    double costToReload = 0.0;
    double retailCost = 0.0;
    double savings = 0.0;
    double savingsPercent = 0.0;
};

struct PieSlice {
    std::string label;
    double percentage = 0.0;
    double costPerRound = 0.0;
    uint32_t colorHex = 0xFFFFFFFF;
};

struct BallisticRow {
    int rangeYards = 0;
    double velocityFps = 0.0;
    double energyFtLbs = 0.0;
    double dropInches = 0.0;
    double dropMoa = 0.0;
    double dropMil = 0.0;
};

class ReloadCalculator {
public:
    static constexpr double GRAINS_PER_POUND = 7000.0;
    static constexpr double GRAMS_PER_KG = 1000.0;

    static ComponentCosts calculateComponentCosts(const ReloadLoad& load);
    static std::vector<SavingsRow> calculateSavingsTable(
        const ReloadLoad& load,
        const std::vector<int>& quantities = {1, 20, 25, 50, 100, 500, 1000});
    static std::vector<PieSlice> calculatePieChartSlices(const ComponentCosts& costs);

    static double muzzleEnergyFtLbs(double bulletWeightGrains, double velocityFps);
    static double velocityInFps(const ReloadLoad& load);
    static std::vector<BallisticRow> calculateDropTable(
        const ReloadLoad& load,
        double zeroYards = 100.0,
        double sightHeightInches = 1.5,
        const std::vector<int>& rangesYards = {100, 200, 300, 400, 500});
};

} // namespace means
