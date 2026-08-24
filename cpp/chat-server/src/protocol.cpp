#include "protocol.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace chat::protocol {

std::string trim(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string formatBroadcastLine(const std::string& message, const std::string& senderName) {
    std::string line = senderName.empty() ? message : ("[" + senderName + "] " + message);
    if (!line.empty() && line.back() != '\n') {
        line.push_back('\n');
    }
    return line;
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &now);
#else
    localtime_r(&now, &localTm);
#endif
    std::ostringstream out;
    out << std::put_time(&localTm, "%H:%M:%S");
    return out.str();
}

}  // namespace chat::protocol
