#include "microback/binary.hpp"
#include "microback/replay_engine.hpp"
#include <filesystem>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: microback-convert input.csv output.mkt\n";
        return 2;
    }
    std::filesystem::path temp;
    try {
        const std::filesystem::path out{argv[2]};
        temp = out.string() + ".tmp";
        std::error_code ec;
        if (std::filesystem::equivalent(std::filesystem::path{argv[1]}, out, ec))
            throw std::invalid_argument("input and output must differ");
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
        std::cout << "events=" << replay.statistics().events << '\n';
    } catch (const std::exception &e) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
