#include <iostream>

#include "btree.hpp"

static void usage() {
    std::cerr << "usage: btree-store <dbfile> <put|get|del|scan> [args...]\n";
    std::cerr << "  put <key> <value>\n";
    std::cerr << "  get <key>\n";
    std::cerr << "  del <key>\n";
    std::cerr << "  scan <start> <end>\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 1;
    }

    std::string dbfile = argv[1];
    std::string cmd = argv[2];

    btree::BTree tree(dbfile);

    if (cmd == "put" && argc == 5) {
        tree.put(argv[3], argv[4]);
        std::cout << "OK\n";
    } else if (cmd == "get" && argc == 4) {
        auto v = tree.get(argv[3]);
        if (v) {
            std::cout << *v << "\n";
        } else {
            std::cout << "(not found)\n";
            return 1;
        }
    } else if (cmd == "del" && argc == 4) {
        bool removed = tree.remove(argv[3]);
        std::cout << (removed ? "OK\n" : "(not found)\n");
        if (!removed) return 1;
    } else if (cmd == "scan" && argc == 5) {
        auto results = tree.range_scan(argv[3], argv[4]);
        for (auto& [k, v] : results) {
            std::cout << k << " = " << v << "\n";
        }
    } else {
        usage();
        return 1;
    }

    return 0;
}
