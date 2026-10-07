// CLI driver. Reads commands from a file (argv[1]) or stdin:
//   ADD <id> <B|S> <price> <qty>
//   CANCEL <id>
//   PRINT
// Prints trades after ADD, the result after CANCEL, and the book after
// every command. Blank lines and lines starting with '#' are ignored.

#include "ob/order_book.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void print_trades(const std::vector<ob::Trade>& trades) {
    if (trades.empty()) {
        std::cout << "  no trades\n";
        return;
    }
    for (const auto& t : trades) {
        std::cout << "  TRADE taker=" << t.taker_id << " maker=" << t.maker_id
                  << " price=" << t.price << " qty=" << t.qty << '\n';
    }
}

// Returns false on a malformed line (and says why) without stopping the run.
bool handle(ob::OrderBook& book, const std::string& line) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;

    if (cmd == "ADD") {
        ob::OrderId id; char side_ch; ob::Price price; ob::Quantity qty;
        if (!(in >> id >> side_ch >> price >> qty) || (side_ch != 'B' && side_ch != 'S')) {
            std::cout << "  usage: ADD <id> <B|S> <price> <qty>\n";
            return false;
        }
        auto side = side_ch == 'B' ? ob::Side::Buy : ob::Side::Sell;
        print_trades(book.add(id, side, price, qty));
    } else if (cmd == "CANCEL") {
        ob::OrderId id;
        if (!(in >> id)) {
            std::cout << "  usage: CANCEL <id>\n";
            return false;
        }
        std::cout << (book.cancel(id) ? "  cancelled\n" : "  not found\n");
    } else if (cmd == "PRINT") {
        // book is dumped below for every command anyway
    } else {
        std::cout << "  unknown command: " << cmd << '\n';
        return false;
    }
    std::cout << book.dump();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::ifstream file;
    std::istream* in = &std::cin;
    if (argc > 1) {
        file.open(argv[1]);
        if (!file) {
            std::cerr << "cannot open " << argv[1] << '\n';
            return 1;
        }
        in = &file;
    }

    ob::OrderBook book;
    std::string line;
    while (std::getline(*in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::cout << "> " << line << '\n';
        handle(book, line);
    }
    return 0;
}
