#include "asset/database.h"
#include "common/scope_exit.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    constexpr std::string_view USAGE =
        "Usage: asset_scan_benchmark PROJECT_DIRECTORY [ROUNDS (1..1000)]";

    Comet::Result<fs::path> create_temporary_project() {
        std::error_code error;
        const fs::path parent = fs::temp_directory_path(error);
        if(error)
            return Comet::Result<fs::path>::failure(
                "Cannot locate temporary directory: " + error.message());
        const auto timestamp = Clock::now().time_since_epoch().count();
        for(int attempt = 0; attempt < 32; ++attempt) {
            const fs::path candidate = parent
                                       / ("comet_asset_scan_benchmark_" + std::to_string(timestamp)
                                           + "_" + std::to_string(attempt));
            if(fs::create_directory(candidate, error))
                return Comet::Result<fs::path>::success(candidate);
            if(error && error != std::errc::file_exists)
                return Comet::Result<fs::path>::failure(
                    "Cannot create temporary directory: " + error.message());
            error.clear();
        }
        return Comet::Result<fs::path>::failure("Cannot create unique temporary directory");
    }

    double elapsed_ms(const Clock::time_point begin, const Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }

    void print_summary(const std::string_view phase, std::vector<double> samples,
        const std::size_t indexed_assets) {
        std::ranges::sort(samples);
        const auto percentile = [&](const std::size_t numerator, const std::size_t denominator) {
            const std::size_t rank = (samples.size() * numerator + denominator - 1) / denominator;
            return samples[rank - 1];
        };
        std::cout << "unchanged_full_scan," << phase << ',' << indexed_assets << ','
                  << samples.size() << ',' << percentile(1, 2) << ',' << percentile(95, 100) << ','
                  << samples.back() << '\n';
    }
}

int main(int argc, char** argv) {
    if(argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << USAGE << '\n';
        return 0;
    }
    if(argc != 2 && argc != 3) {
        std::cerr << USAGE << '\n';
        return 2;
    }

    unsigned rounds = 20;
    if(argc == 3) {
        const std::string_view input(argv[2]);
        const auto [end, error] =
            std::from_chars(input.data(), input.data() + input.size(), rounds);
        if(error != std::errc{} || end != input.data() + input.size() || rounds < 1
            || rounds > 1000) {
            std::cerr << "Invalid round count: " << input << '\n';
            return 2;
        }
    }

    const fs::path source_assets = fs::path(argv[1]) / "assets";
    std::error_code error;
    const auto source_status = fs::symlink_status(source_assets, error);
    if(error || !fs::is_directory(source_status) || fs::is_symlink(source_status)) {
        std::cerr << "Project needs a real, accessible assets directory: " << source_assets << '\n';
        return 1;
    }

    auto temporary = create_temporary_project();
    if(!temporary) {
        std::cerr << temporary.error() << '\n';
        return 1;
    }
    const fs::path temporary_root = std::move(temporary).value();
    const Comet::ScopeExit cleanup([&] {
        std::error_code ignored;
        fs::remove_all(temporary_root, ignored);
        if(ignored)
            std::cerr << "Cannot remove temporary project '" << temporary_root
                      << "': " << ignored.message() << '\n';
    });
    fs::copy(source_assets, temporary_root / "assets",
        fs::copy_options::recursive | fs::copy_options::copy_symlinks, error);
    if(error) {
        std::cerr << "Cannot copy assets to temporary project: " << error.message() << '\n';
        return 1;
    }

    const Comet::ProjectPaths paths(temporary_root);
    Comet::AssetDatabase database(paths);
    const Comet::AssetScanReport warmup = database.scan();
    if(!warmup.snapshot_updated) {
        std::cerr << "Initial asset scan could not publish an index\n";
        return 1;
    }
    if(!warmup.issues.empty())
        std::cerr << "Initial scan reported " << warmup.issues.size() << " issue(s)\n";

    std::vector<double> prepare_times;
    std::vector<double> publish_times;
    prepare_times.reserve(rounds);
    publish_times.reserve(rounds);
    for(unsigned round = 0; round < rounds; ++round) {
        const auto begin = Clock::now();
        auto prepared = Comet::AssetDatabase::prepare_scan(paths, database.generation());
        const auto prepared_at = Clock::now();
        auto report = database.publish_scan(std::move(prepared));
        const auto published_at = Clock::now();
        if(!report || !report->snapshot_updated) {
            std::cerr << "Asset scan failed during round " << (round + 1) << '\n';
            return 1;
        }
        prepare_times.push_back(elapsed_ms(begin, prepared_at));
        publish_times.push_back(elapsed_ms(prepared_at, published_at));
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "scenario,phase,indexed_assets,rounds,p50_ms,p95_ms,max_ms\n";
    print_summary("prepare", std::move(prepare_times), database.size());
    print_summary("publish", std::move(publish_times), database.size());
    return 0;
}
