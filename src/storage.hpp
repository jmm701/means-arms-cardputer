#pragma once

#include "model.hpp"
#include <string>
#include <vector>

namespace means {

class Storage {
public:
    static std::string data_dir();
    static std::string loads_path();
    static std::string default_export_path();

    static std::vector<ReloadLoad> load_all();
    static bool save_all(const std::vector<ReloadLoad>& loads);

    static bool export_json(const std::vector<ReloadLoad>& loads, const std::string& path);
    static std::vector<ReloadLoad> import_json_file(const std::string& path, std::string& error_out);
    static std::vector<std::string> find_backup_candidates();
    static std::vector<ReloadLoad> seed_sample_loads();
};

} // namespace means
