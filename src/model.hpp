#pragma once

#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

namespace means {

enum class AmmoType { CARTRIDGE, SHOTSHELL };
enum class UnitOfMeasure { IMPERIAL, METRIC };
enum class LoadSortOrder { NAME, CALIBER, COST, BULLET_WEIGHT, UPDATED };

inline std::string ammo_type_str(AmmoType t) {
    return t == AmmoType::SHOTSHELL ? "SHOTSHELL" : "CARTRIDGE";
}

inline AmmoType parse_ammo_type(const std::string& s) {
    if (s == "SHOTSHELL" || s == "shotshell") return AmmoType::SHOTSHELL;
    return AmmoType::CARTRIDGE;
}

inline std::string unit_str(UnitOfMeasure u) {
    return u == UnitOfMeasure::METRIC ? "METRIC" : "IMPERIAL";
}

inline UnitOfMeasure parse_unit(const std::string& s) {
    if (s == "METRIC" || s == "metric") return UnitOfMeasure::METRIC;
    return UnitOfMeasure::IMPERIAL;
}

inline double parse_double_clean(const std::string& s, double def = 0.0) {
    if (s.empty()) return def;
    try {
        size_t idx = 0;
        double v = std::stod(s, &idx);
        if (idx == s.size()) return v;
    } catch (...) {}
    std::string clean;
    bool dot = false;
    for (unsigned char c : s) {
        if (std::isdigit(c)) clean.push_back(static_cast<char>(c));
        else if (c == '.' && !dot) { clean.push_back('.'); dot = true; }
    }
    if (clean.empty() || clean == ".") return def;
    try { return std::stod(clean); } catch (...) { return def; }
}

inline int parse_int_clean(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string make_uuid() {
    static std::mt19937_64 rng(static_cast<uint64_t>(now_ms()) ^ 0x9e3779b97f4a7c15ULL);
    uint64_t a = rng();
    uint64_t b = rng();
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%08llx-%04llx-4%03llx-%04llx-%012llx",
        static_cast<unsigned long long>((a >> 32) & 0xffffffff),
        static_cast<unsigned long long>((a >> 16) & 0xffff),
        static_cast<unsigned long long>(a & 0x0fff),
        static_cast<unsigned long long>(0x8000 | ((b >> 48) & 0x3fff)),
        static_cast<unsigned long long>(b & 0xffffffffffffULL));
    return buf;
}

struct ReloadLoad {
    std::string id = make_uuid();
    int64_t createdAt = now_ms();
    int64_t updatedAt = now_ms();

    // Header & Specifications
    std::string loadName;
    std::string caliber;
    AmmoType ammoType = AmmoType::CARTRIDGE;
    UnitOfMeasure unitOfMeasure = UnitOfMeasure::IMPERIAL;
    std::string powderGrains;
    std::string overallLength;
    std::string caseTrimLength;

    // Component Quantities & Costs
    std::string bulletQuantity;
    std::string bulletCost;
    std::string powderWeightBulk = "1";
    std::string powderCost;
    std::string primerQuantity;
    std::string primerCost;
    std::string caseQuantity;
    std::string caseCost;
    std::string caseTimesUsed = "1";
    std::string retailRoundCount;
    std::string retailCost;

    // Component Descriptions
    std::string bulletDescription;
    std::string bulletWeightGrains;
    std::string bulletStyle;
    std::string powderDescription;
    std::string primerDescription;
    std::string caseDescription;

    // Firing Results & Ballistics
    std::string velocity;
    std::string velocityUnit = "Feet/Second (fps)";
    std::string standardDeviation;
    std::string extremeSpread;
    std::string ballisticCoefficient;
    std::string ballisticCoefficientType = "G1";
    std::string bulletLength;
    std::string groupSize;
    std::string groupSizeUnit = "Inches";
    std::string shotCount;
    std::string targetDistance;
    std::string targetDistanceUnit = "Yards";

    // Load Notes & Photo
    std::string notes;
    std::string photoPath;
    std::string photoBase64;

    double powderGrainsVal() const { return parse_double_clean(powderGrains, 0.0); }
    double overallLengthVal() const { return parse_double_clean(overallLength, 0.0); }
    double caseTrimLengthVal() const { return parse_double_clean(caseTrimLength, 0.0); }
    int bulletQuantityVal() const { return parse_int_clean(bulletQuantity, 0); }
    double bulletCostVal() const { return parse_double_clean(bulletCost, 0.0); }
    double powderWeightBulkVal() const { return parse_double_clean(powderWeightBulk, 1.0); }
    double powderCostVal() const { return parse_double_clean(powderCost, 0.0); }
    int primerQuantityVal() const { return parse_int_clean(primerQuantity, 0); }
    double primerCostVal() const { return parse_double_clean(primerCost, 0.0); }
    int caseQuantityVal() const { return parse_int_clean(caseQuantity, 0); }
    double caseCostVal() const { return parse_double_clean(caseCost, 0.0); }
    int caseTimesUsedVal() const { return std::max(1, parse_int_clean(caseTimesUsed, 1)); }
    int retailRoundCountVal() const { return parse_int_clean(retailRoundCount, 0); }
    double retailCostVal() const { return parse_double_clean(retailCost, 0.0); }
    double bulletWeightGrainsVal() const { return parse_double_clean(bulletWeightGrains, 0.0); }
    double velocityVal() const { return parse_double_clean(velocity, 0.0); }
    double sdVal() const { return parse_double_clean(standardDeviation, 0.0); }
    double esVal() const { return parse_double_clean(extremeSpread, 0.0); }
    double bcVal() const { return parse_double_clean(ballisticCoefficient, 0.0); }
    double groupSizeVal() const { return parse_double_clean(groupSize, 0.0); }
    int shotCountVal() const { return parse_int_clean(shotCount, 0); }
    double targetDistanceVal() const { return parse_double_clean(targetDistance, 0.0); }

    std::string shortVelocityUnit() const {
        std::string low = velocityUnit;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return low.find("m/s") != std::string::npos ? "m/s" : "fps";
    }

    std::string velocityDisplay() const {
        double v = velocityVal();
        if (v <= 0.0) return "";
        char buf[32];
        if (static_cast<double>(static_cast<int64_t>(v)) == v) {
            std::snprintf(buf, sizeof(buf), "%lld %s", static_cast<long long>(v), shortVelocityUnit().c_str());
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f %s", v, shortVelocityUnit().c_str());
        }
        return buf;
    }

    json::Value to_json() const {
        json::Object o;
        o["id"] = id;
        o["createdAt"] = createdAt;
        o["updatedAt"] = updatedAt;
        o["loadName"] = loadName;
        o["caliber"] = caliber;
        o["ammoType"] = ammo_type_str(ammoType);
        o["unitOfMeasure"] = unit_str(unitOfMeasure);
        o["powderGrains"] = powderGrains;
        o["overallLength"] = overallLength;
        o["caseTrimLength"] = caseTrimLength;
        o["bulletQuantity"] = bulletQuantity;
        o["bulletCost"] = bulletCost;
        o["powderWeightBulk"] = powderWeightBulk;
        o["powderCost"] = powderCost;
        o["primerQuantity"] = primerQuantity;
        o["primerCost"] = primerCost;
        o["caseQuantity"] = caseQuantity;
        o["caseCost"] = caseCost;
        o["caseTimesUsed"] = caseTimesUsed;
        o["retailRoundCount"] = retailRoundCount;
        o["retailCost"] = retailCost;
        o["bulletDescription"] = bulletDescription;
        o["bulletWeightGrains"] = bulletWeightGrains;
        o["bulletStyle"] = bulletStyle;
        o["powderDescription"] = powderDescription;
        o["primerDescription"] = primerDescription;
        o["caseDescription"] = caseDescription;
        o["velocity"] = velocity;
        o["velocityUnit"] = velocityUnit;
        o["standardDeviation"] = standardDeviation;
        o["extremeSpread"] = extremeSpread;
        o["ballisticCoefficient"] = ballisticCoefficient;
        o["ballisticCoefficientType"] = ballisticCoefficientType;
        o["bulletLength"] = bulletLength;
        o["groupSize"] = groupSize;
        o["groupSizeUnit"] = groupSizeUnit;
        o["shotCount"] = shotCount;
        o["targetDistance"] = targetDistance;
        o["targetDistanceUnit"] = targetDistanceUnit;
        o["notes"] = notes;
        o["photoPath"] = photoPath;
        o["photoBase64"] = photoBase64;
        return json::Value(std::move(o));
    }

    static ReloadLoad from_json(const json::Value& v) {
        ReloadLoad l;
        auto s = [&](const char* k, const std::string& def = "") {
            const auto& val = v.get(k);
            return val.is_null() ? def : val.as_string(def);
        };
        if (!v.get("id").is_null() && !s("id").empty()) l.id = s("id");
        if (!v.get("createdAt").is_null()) l.createdAt = v.get("createdAt").as_int64(l.createdAt);
        if (!v.get("updatedAt").is_null()) l.updatedAt = v.get("updatedAt").as_int64(l.updatedAt);
        l.loadName = s("loadName");
        l.caliber = s("caliber");
        l.ammoType = parse_ammo_type(s("ammoType", "CARTRIDGE"));
        l.unitOfMeasure = parse_unit(s("unitOfMeasure", "IMPERIAL"));
        l.powderGrains = s("powderGrains");
        l.overallLength = s("overallLength");
        l.caseTrimLength = s("caseTrimLength");
        l.bulletQuantity = s("bulletQuantity");
        l.bulletCost = s("bulletCost");
        l.powderWeightBulk = s("powderWeightBulk", "1");
        l.powderCost = s("powderCost");
        l.primerQuantity = s("primerQuantity");
        l.primerCost = s("primerCost");
        l.caseQuantity = s("caseQuantity");
        l.caseCost = s("caseCost");
        l.caseTimesUsed = s("caseTimesUsed", "1");
        l.retailRoundCount = s("retailRoundCount");
        l.retailCost = s("retailCost");
        l.bulletDescription = s("bulletDescription");
        l.bulletWeightGrains = s("bulletWeightGrains");
        l.bulletStyle = s("bulletStyle");
        l.powderDescription = s("powderDescription");
        l.primerDescription = s("primerDescription");
        l.caseDescription = s("caseDescription");
        l.velocity = s("velocity");
        l.velocityUnit = s("velocityUnit", "Feet/Second (fps)");
        l.standardDeviation = s("standardDeviation");
        l.extremeSpread = s("extremeSpread");
        l.ballisticCoefficient = s("ballisticCoefficient");
        l.ballisticCoefficientType = s("ballisticCoefficientType", "G1");
        l.bulletLength = s("bulletLength");
        l.groupSize = s("groupSize");
        l.groupSizeUnit = s("groupSizeUnit", "Inches");
        l.shotCount = s("shotCount");
        l.targetDistance = s("targetDistance");
        l.targetDistanceUnit = s("targetDistanceUnit", "Yards");
        l.notes = s("notes");
        l.photoPath = s("photoPath");
        l.photoBase64 = s("photoBase64");
        return l;
    }
};

} // namespace means
