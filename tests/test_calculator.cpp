#include "calculator.hpp"
#include "model.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace means;

static bool approx(double a, double b, double eps = 1e-4) {
    return std::abs(a - b) <= eps;
}

int main() {
    ReloadLoad l;
    l.loadName = "Test 9mm";
    l.caliber = "9mm";
    l.unitOfMeasure = UnitOfMeasure::IMPERIAL;
    l.powderGrains = "4.0";
    l.powderWeightBulk = "1";
    l.powderCost = "35.0";       // 35 / 7000 * 4 = 0.02
    l.bulletQuantity = "500";
    l.bulletCost = "50.0";       // 0.10
    l.primerQuantity = "1000";
    l.primerCost = "80.0";       // 0.08
    l.caseQuantity = "500";
    l.caseCost = "50.0";
    l.caseTimesUsed = "5";       // (50 / 500) / 5 = 0.02
    l.retailRoundCount = "50";
    l.retailCost = "16.0";       // 0.32

    auto c = ReloadCalculator::calculateComponentCosts(l);
    assert(approx(c.bulletCostPerRound, 0.10));
    assert(approx(c.powderCostPerRound, 0.02));
    assert(approx(c.primerCostPerRound, 0.08));
    assert(approx(c.caseCostPerRound, 0.02));
    assert(approx(c.totalCostPerRound, 0.22));
    assert(approx(c.retailCostPerRound, 0.32));
    assert(approx(c.savingsPerRound, 0.10));
    assert(approx(c.savingsPercent, 31.25));
    assert(approx(c.powderLoadsPerBulkUnit, 1750.0));

    auto rows = ReloadCalculator::calculateSavingsTable(l, {100});
    assert(rows.size() == 1);
    assert(approx(rows[0].costToReload, 22.0));
    assert(approx(rows[0].retailCost, 32.0));
    assert(approx(rows[0].savings, 10.0));

    // Metric check
    ReloadLoad m = l;
    m.unitOfMeasure = UnitOfMeasure::METRIC;
    m.powderGrains = "5.0";
    m.powderCost = "100.0"; // 100 / 1000 * 5 = 0.50
    auto mc = ReloadCalculator::calculateComponentCosts(m);
    assert(approx(mc.powderCostPerRound, 0.50));
    assert(approx(mc.powderLoadsPerBulkUnit, 200.0));

    // JSON round-trip check
    auto j = l.to_json();
    auto dumped = json::dump(j);
    auto parsed = ReloadLoad::from_json(json::parse(dumped));
    assert(parsed.loadName == l.loadName);
    assert(parsed.caliber == l.caliber);
    assert(parsed.caseTimesUsedVal() == 5);

    // Ballistic drop check (100 yd zero should have ~0 drop at 100 yds, negative drop at 300 yds)
    ReloadLoad r;
    r.bulletWeightGrains = "140";
    r.velocity = "2710";
    r.ballisticCoefficient = "0.326";
    r.ballisticCoefficientType = "G7";
    auto drop = ReloadCalculator::calculateDropTable(r, 100.0, 1.5, {100, 300});
    assert(drop.size() == 2);
    assert(std::abs(drop[0].dropInches) < 0.05);
    assert(drop[1].dropInches < -8.0 && drop[1].dropInches > -25.0);
    assert(drop[0].energyFtLbs > 1800.0);

    std::cout << "All means_arms_tests passed!\n";
    return 0;
}
