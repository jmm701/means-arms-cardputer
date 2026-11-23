#include "storage.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace means {

namespace {

std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h && *h ? std::string(h) : std::string("/tmp");
}

std::string read_file_str(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

std::string Storage::data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base = (xdg && *xdg) ? std::string(xdg) : (home_dir() + "/.local/share");
    return base + "/means-arms-ammo";
}

std::string Storage::loads_path() {
    return data_dir() + "/loads.json";
}

std::string Storage::default_export_path() {
    return home_dir() + "/means_arms_backup.json";
}

std::vector<ReloadLoad> Storage::seed_sample_loads() {
    std::vector<ReloadLoad> list;
    {
        ReloadLoad l;
        l.loadName = "6.5 CM 140 ELD-M";
        l.caliber = "6.5 Creedmoor";
        l.powderGrains = "41.5";
        l.overallLength = "2.810";
        l.caseTrimLength = "1.910";
        l.bulletQuantity = "100";
        l.bulletCost = "42.00";
        l.powderWeightBulk = "1";
        l.powderCost = "52.00";
        l.primerQuantity = "1000";
        l.primerCost = "95.00";
        l.caseQuantity = "100";
        l.caseCost = "110.00";
        l.caseTimesUsed = "5";
        l.retailRoundCount = "20";
        l.retailCost = "36.00";
        l.bulletDescription = "Hornady 140gr ELD-M";
        l.bulletWeightGrains = "140";
        l.bulletStyle = "ELD-M";
        l.powderDescription = "Hodgdon H4350";
        l.primerDescription = "CCI BR-2";
        l.caseDescription = "Lapua SRP";
        l.velocity = "2710";
        l.standardDeviation = "7.2";
        l.extremeSpread = "18";
        l.ballisticCoefficient = "0.326";
        l.ballisticCoefficientType = "G7";
        l.groupSize = "0.48";
        l.shotCount = "5";
        l.targetDistance = "100";
        l.notes = "Node at 41.5gr H4350, 0.020 off lands.";
        list.push_back(std::move(l));
    }
    {
        ReloadLoad l;
        l.loadName = "9mm 124gr Range";
        l.caliber = "9mm Luger";
        l.powderGrains = "4.3";
        l.overallLength = "1.135";
        l.caseTrimLength = "0.750";
        l.bulletQuantity = "500";
        l.bulletCost = "48.00";
        l.powderWeightBulk = "1";
        l.powderCost = "38.00";
        l.primerQuantity = "1000";
        l.primerCost = "75.00";
        l.caseQuantity = "500";
        l.caseCost = "0.00";
        l.caseTimesUsed = "1";
        l.retailRoundCount = "50";
        l.retailCost = "14.50";
        l.bulletDescription = "RMR 124gr FMJ RN";
        l.bulletWeightGrains = "124";
        l.bulletStyle = "FMJ";
        l.powderDescription = "Titegroup";
        l.primerDescription = "CCI 500";
        l.caseDescription = "Mixed Range Brass";
        l.velocity = "1115";
        l.standardDeviation = "11.4";
        l.extremeSpread = "31";
        l.ballisticCoefficient = "0.145";
        l.ballisticCoefficientType = "G1";
        l.groupSize = "1.6";
        l.shotCount = "10";
        l.targetDistance = "15";
        l.notes = "Clean plinking / USPSA minor PF.";
        list.push_back(std::move(l));
    }
    return list;
}

std::vector<ReloadLoad> Storage::load_all() {
    std::string p = loads_path();
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        auto seeded = seed_sample_loads();
        save_all(seeded);
        return seeded;
    }
    std::string err;
    auto loads = import_json_file(p, err);
    return loads;
}

bool Storage::save_all(const std::vector<ReloadLoad>& loads) {
    std::error_code ec;
    fs::create_directories(data_dir(), ec);
    return export_json(loads, loads_path());
}

bool Storage::export_json(const std::vector<ReloadLoad>& loads, const std::string& path) {
    json::Array arr;
    arr.reserve(loads.size());
    for (const auto& l : loads) {
        arr.push_back(l.to_json());
    }
    std::string serialized = json::dump(json::Value(std::move(arr)), 2);
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::vector<ReloadLoad> Storage::import_json_file(const std::string& path, std::string& error_out) {
    error_out.clear();
    std::string raw = read_file_str(path);
    if (raw.empty()) {
        error_out = "Empty or unreadable file";
        return {};
    }
    try {
        json::Value root = json::parse(raw);
        if (!root.is_array()) {
            error_out = "Expected JSON array of loads";
            return {};
        }
        std::vector<ReloadLoad> out;
        for (const auto& item : root.as_array()) {
            if (item.is_object()) {
                out.push_back(ReloadLoad::from_json(item));
            }
        }
        return out;
    } catch (const std::exception& e) {
        error_out = e.what();
        return {};
    }
}

std::vector<std::string> Storage::find_backup_candidates() {
    std::vector<std::string> found;
    std::vector<std::string> dirs = {
        home_dir(),
        home_dir() + "/Downloads",
        home_dir() + "/Documents",
        "/boot/firmware"
    };
    std::error_code ec;
    for (const auto& d : dirs) {
        if (!fs::exists(d, ec)) continue;
        for (const auto& entry : fs::directory_iterator(d, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            auto name = entry.path().filename().string();
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json" && name.rfind("._", 0) != 0) {
                found.push_back(entry.path().string());
            }
        }
    }
    return found;
}

} // namespace means
