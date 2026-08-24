#include "http_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace http {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

}

ParseStatus RequestParser::feed(const char* data, size_t len) {
    buffer_.append(data, len);
    return parse_buffer();
}

ParseStatus RequestParser::parse_buffer() {
    if (state_ == State::RequestLine || state_ == State::Headers) {
        size_t pos;
        while ((pos = buffer_.find("\r\n")) != std::string::npos || state_ == State::RequestLine) {
            if (state_ == State::RequestLine) {
                pos = buffer_.find("\r\n");
                if (pos == std::string::npos) return ParseStatus::Incomplete;
                std::string line = buffer_.substr(0, pos);
                buffer_.erase(0, pos + 2);
                if (!parse_request_line(line)) {
                    status_ = ParseStatus::Error;
                    return status_;
                }
                state_ = State::Headers;
                continue;
            }

            std::string line = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 2);

            if (line.empty()) {
                auto it = request_.headers.find("content-length");
                if (it != request_.headers.end()) {
                    content_length_ = static_cast<size_t>(std::stoul(it->second));
                }
                state_ = content_length_ > 0 ? State::Body : State::Done;
                break;
            }

            if (!parse_header_line(line)) {
                status_ = ParseStatus::Error;
                return status_;
            }
        }
    }

    if (state_ == State::Body) {
        if (buffer_.size() >= content_length_) {
            request_.body = buffer_.substr(0, content_length_);
            buffer_.erase(0, content_length_);
            state_ = State::Done;
        } else {
            return ParseStatus::Incomplete;
        }
    }

    if (state_ == State::Done) {
        status_ = ParseStatus::Complete;
        return status_;
    }

    return ParseStatus::Incomplete;
}

bool RequestParser::parse_request_line(const std::string& line) {
    std::istringstream iss(line);
    if (!(iss >> request_.method >> request_.path >> request_.version)) {
        return false;
    }
    return true;
}

bool RequestParser::parse_header_line(const std::string& line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    std::string key = to_lower(trim(line.substr(0, colon)));
    std::string value = trim(line.substr(colon + 1));
    request_.headers[key] = value;
    return true;
}

Request RequestParser::take_request() {
    Request r = std::move(request_);
    reset();
    return r;
}

void RequestParser::reset() {
    request_ = Request{};
    state_ = State::RequestLine;
    status_ = ParseStatus::Incomplete;
    content_length_ = 0;
}

std::string content_type_for_path(const std::string& path) {
    static const std::unordered_map<std::string, std::string> types = {
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".txt", "text/plain"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
    };
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = to_lower(path.substr(dot));
    auto it = types.find(ext);
    return it != types.end() ? it->second : "application/octet-stream";
}

bool is_connection_keep_alive(const Request& req) {
    auto it = req.headers.find("connection");
    if (it == req.headers.end()) {
        return req.version == "HTTP/1.1";
    }
    return to_lower(it->second) != "close";
}

std::string build_response(int status_code,
                            const std::string& status_text,
                            const std::string& content_type,
                            const std::string& body,
                            bool keep_alive) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

std::string build_error_response(int status_code, const std::string& status_text, bool keep_alive) {
    std::string body = "<html><body><h1>" + std::to_string(status_code) + " " + status_text +
                        "</h1></body></html>";
    return build_response(status_code, status_text, "text/html", body, keep_alive);
}

}
