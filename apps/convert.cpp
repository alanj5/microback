#include "microback/binary.hpp"
#include "microback/replay_engine.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: microback-convert input.csv output.mkt\n";
        return 2;
    }
    std::filesystem::path temporary_directory;
    try {
        const std::filesystem::path out{argv[2]};
        std::error_code ec;
        if (std::filesystem::equivalent(std::filesystem::path{argv[1]}, out, ec))
            throw std::invalid_argument("input and output must differ");
        const auto parent = out.has_parent_path() ? out.parent_path() : std::filesystem::path{"."};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            const auto candidate =
                parent / (".microback-" + std::to_string(nonce) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                temporary_directory = candidate;
                break;
            }
        }
        if (temporary_directory.empty())
            throw std::runtime_error("cannot reserve temporary directory");
        const auto temp = temporary_directory / "events.mkt";
        microback::CsvEventSource source{std::string{argv[1]}};
        std::ofstream file{temp, std::ios::binary};
        microback::write_binary_header(file);
        microback::ReplayEngine replay;
        microback::MarketEvent event;
        while (source.next(event)) {
            replay.ingest(event);
            microback::write_binary_event(file, event);
        }
        replay.finish();
        file.close();
        if (!file)
            throw std::runtime_error("binary close failed");
        std::filesystem::rename(temp, out);
        std::filesystem::remove(temporary_directory, ec);
        std::cout << "events=" << replay.statistics().events << '\n';
    } catch (const std::exception &e) {
        std::error_code ec;
        if (!temporary_directory.empty())
            std::filesystem::remove_all(temporary_directory, ec);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
