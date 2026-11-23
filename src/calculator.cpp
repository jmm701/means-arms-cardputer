#include "calculator.hpp"
#include <algorithm>
#include <cmath>

namespace means {

namespace {

double round_to(double v, double factor) {
    return std::round(v * factor) / factor;
}

// Simplified G1 / G7 drag deceleration factor (standard atmosphere ~1116 fps speed of sound)
double drag_coeff(double mach, bool is_g7) {
    if (is_g7) {
        if (mach < 0.85) return 0.12;
        if (mach < 1.05) return 0.12 + (mach - 0.85) * 1.15;
        if (mach < 1.40) return 0.35 - (mach - 1.05) * 0.10;
        return std::max(0.18, 0.315 - (mach - 1.40) * 0.05);
    }
    // G1
    if (mach < 0.85) return 0.21;
    if (mach < 1.05) return 0.21 + (mach - 0.85) * 1.95;
    if (mach < 1.40) return 0.60 - (mach - 1.05) * 0.15;
    return std::max(0.32, 0.547 - (mach - 1.40) * 0.08);
}

// Integrate 2D trajectory at given launch angle (radians) to target range (feet)
// Returns pair(y_feet, velocity_fps)
std::pair<double, double> simulate_to_range(double v0_fps, double bc, bool is_g7, double launch_rad, double sight_ft, double max_range_ft) {
    const double g = 32.174; // ft/s^2
    const double sos = 1116.45;
    // Standard atmospheric air density constant for ballistic coefficient
    const double k_air = 0.000182;

    double x = 0.0;
    double y = -sight_ft;
    double vx = v0_fps * std::cos(launch_rad);
    double vy = v0_fps * std::sin(launch_rad);
    const double dt = 0.001; // 1ms step

    for (int step = 0; step < 30000 && x < max_range_ft; ++step) {
        double v = std::hypot(vx, vy);
        if (v < 50.0) break;
        double cd = drag_coeff(v / sos, is_g7);
        double decel = (k_air * cd * v * v) / std::max(0.05, bc);
        double ax = -decel * (vx / v);
        double ay = -g - decel * (vy / v);

        double nx = x + vx * dt;
        double ny = y + vy * dt;
        if (nx >= max_range_ft) {
            double frac = (max_range_ft - x) / std::max(1e-9, nx - x);
            double iy = y + (ny - y) * frac;
            double iv = v + (std::hypot(vx + ax * dt, vy + ay * dt) - v) * frac;
            return {iy, iv};
        }
        x = nx;
        y = ny;
        vx += ax * dt;
        vy += ay * dt;
    }
    return {y, std::hypot(vx, vy)};
}

} // namespace

ComponentCosts ReloadCalculator::calculateComponentCosts(const ReloadLoad& load) {
    ComponentCosts out;
    int bq = load.bulletQuantityVal();
    double bc = load.bulletCostVal();
    if (bq > 0 && bc > 0.0) {
        out.bulletCostPerRound = bc / bq;
    }

    double bulkFactor = (load.unitOfMeasure == UnitOfMeasure::METRIC) ? GRAMS_PER_KG : GRAINS_PER_POUND;
    double pwBulk = load.powderWeightBulkVal();
    double pg = load.powderGrainsVal();
    double pc = load.powderCostVal();

    if (pwBulk > 0.0 && pg > 0.0 && pc > 0.0) {
        double totalUnits = pwBulk * bulkFactor;
        out.powderCostPerRound = (pc / totalUnits) * pg;
    }
    if (pg > 0.0) {
        out.powderLoadsPerBulkUnit = bulkFactor / pg;
    }

    int pq = load.primerQuantityVal();
    double prc = load.primerCostVal();
    if (pq > 0 && prc > 0.0) {
        out.primerCostPerRound = prc / pq;
    }

    int timesUsed = std::max(1, load.caseTimesUsedVal());
    int cq = load.caseQuantityVal();
    double cc = load.caseCostVal();
    if (cq > 0 && cc > 0.0) {
        out.caseCostPerRound = (cc / cq) / timesUsed;
    }

    out.totalCostPerRound = out.bulletCostPerRound + out.powderCostPerRound +
                            out.primerCostPerRound + out.caseCostPerRound;

    int rc = load.retailRoundCountVal();
    double rcost = load.retailCostVal();
    if (rc > 0 && rcost > 0.0) {
        out.retailCostPerRound = rcost / rc;
    }

    out.savingsPerRound = std::max(0.0, out.retailCostPerRound - out.totalCostPerRound);
    if (out.retailCostPerRound > 0.0) {
        out.savingsPercent = (out.savingsPerRound / out.retailCostPerRound) * 100.0;
    }
    return out;
}

std::vector<SavingsRow> ReloadCalculator::calculateSavingsTable(
    const ReloadLoad& load,
    const std::vector<int>& quantities) {
    auto costs = calculateComponentCosts(load);
    std::vector<SavingsRow> rows;
    rows.reserve(quantities.size());
    for (int qty : quantities) {
        double reloadCost = costs.totalCostPerRound * qty;
        double retail = costs.retailCostPerRound * qty;
        double savings = std::max(0.0, retail - reloadCost);
        double pct = retail > 0.0 ? (savings / retail) * 100.0 : 0.0;
        rows.push_back(SavingsRow{
            qty,
            round_to(reloadCost, 100.0),
            round_to(retail, 100.0),
            round_to(savings, 100.0),
            round_to(pct, 10.0)
        });
    }
    return rows;
}

std::vector<PieSlice> ReloadCalculator::calculatePieChartSlices(const ComponentCosts& costs) {
    double total = costs.totalCostPerRound;
    if (total <= 0.0) return {};
    std::vector<PieSlice> slices;
    auto add_slice = [&](const char* label, double val, uint32_t color) {
        if (val > 0.0) {
            slices.push_back(PieSlice{
                label,
                round_to((val / total) * 100.0, 10.0),
                val,
                color
            });
        }
    };
    add_slice("Bullet", costs.bulletCostPerRound, 0x00D2FF);
    add_slice("Powder", costs.powderCostPerRound, 0xFF4081);
    add_slice("Primer", costs.primerCostPerRound, 0xFFD600);
    add_slice("Case", costs.caseCostPerRound, 0x00E676);
    return slices;
}

double ReloadCalculator::muzzleEnergyFtLbs(double bulletWeightGrains, double velocityFps) {
    if (bulletWeightGrains <= 0.0 || velocityFps <= 0.0) return 0.0;
    return (bulletWeightGrains * velocityFps * velocityFps) / 450240.0;
}

double ReloadCalculator::velocityInFps(const ReloadLoad& load) {
    double v = load.velocityVal();
    if (v <= 0.0) return 0.0;
    return load.shortVelocityUnit() == "m/s" ? (v * 3.28084) : v;
}

std::vector<BallisticRow> ReloadCalculator::calculateDropTable(
    const ReloadLoad& load,
    double zeroYards,
    double sightHeightInches,
    const std::vector<int>& rangesYards) {
    double v0 = velocityInFps(load);
    double bc = load.bcVal();
    if (v0 <= 0.0) return {};
    if (bc <= 0.0) bc = 0.25; // fallback if BC not entered yet
    bool is_g7 = (load.ballisticCoefficientType == "G7" || load.ballisticCoefficientType == "g7");
    double bw = load.bulletWeightGrainsVal();
    double sight_ft = sightHeightInches / 12.0;

    // Solve zero angle via binary search so y(zeroYards) == 0
    double lo = -0.02, hi = 0.05;
    double zero_ft = std::max(10.0, zeroYards) * 3.0;
    for (int i = 0; i < 24; ++i) {
        double mid = 0.5 * (lo + hi);
        auto [y, _] = simulate_to_range(v0, bc, is_g7, mid, sight_ft, zero_ft);
        if (y < 0.0) lo = mid;
        else hi = mid;
    }
    double launch_rad = 0.5 * (lo + hi);

    std::vector<BallisticRow> rows;
    rows.reserve(rangesYards.size());
    for (int rYds : rangesYards) {
        auto [y_ft, vel] = simulate_to_range(v0, bc, is_g7, launch_rad, sight_ft, rYds * 3.0);
        double drop_in = y_ft * 12.0;
        double moa = (rYds > 0) ? (-drop_in / (1.0472 * (rYds / 100.0))) : 0.0;
        double mil = (rYds > 0) ? (-drop_in / (3.6 * (rYds / 100.0))) : 0.0;
        rows.push_back(BallisticRow{
            rYds,
            vel,
            muzzleEnergyFtLbs(bw, vel),
            drop_in,
            moa,
            mil
        });
    }
    return rows;
}

} // namespace means
