#include <fstream>
#include <iostream>
#include <sstream>

#include "compiler.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "vm.hpp"

namespace {
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Could not open file '" + path + "'.");
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: bytecode-vm <script>\n";
        return 64;
    }

    std::string source;
    try {
        source = readFile(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 74;
    }

    try {
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        auto program = parser.parseProgram();

        Compiler compiler;
        auto script = compiler.compile(program);

        VM vm;
        vm.run(script);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 70;
    }

    return 0;
}
