#include "microback/replay_engine.hpp"
#include <exception>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: microback_cli canonical.csv\n";
        return 2;
    }
    try {
        microback::CsvEventSource source{std::string{argv[1]}};
        microback::ReplayEngine engine;
        engine.run(source);
        const auto &stats = engine.statistics();
        std::cout << "events=" << stats.events << " snapshots=" << stats.snapshots
                  << " sequence_gaps=" << stats.sequence_gaps << '\n';
        for (auto side : {microback::Side::Buy, microback::Side::Sell})
            for (const auto &level : engine.book().depth(side, 10))
                std::cout << (side == microback::Side::Buy ? "bid" : "ask") << ','
                          << level.price.value() << ',' << level.quantity.value() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
