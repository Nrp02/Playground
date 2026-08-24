#pragma once

#include <map>
#include <string>

namespace http {

struct Request {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
};

enum class ParseStatus {
    Incomplete,
    Complete,
    Error,
};

class RequestParser {
public:
    ParseStatus feed(const char* data, size_t len);
    bool has_request() const { return status_ == ParseStatus::Complete; }
    Request take_request();
    void reset();

private:
    enum class State {
        RequestLine,
        Headers,
        Body,
        Done,
    };

    ParseStatus parse_buffer();
    bool parse_request_line(const std::string& line);
    bool parse_header_line(const std::string& line);

    std::string buffer_;
    Request request_;
    State state_ = State::RequestLine;
    ParseStatus status_ = ParseStatus::Incomplete;
    size_t content_length_ = 0;
};

std::string content_type_for_path(const std::string& path);
bool is_connection_keep_alive(const Request& req);

std::string build_response(int status_code,
                            const std::string& status_text,
                            const std::string& content_type,
                            const std::string& body,
                            bool keep_alive);

std::string build_error_response(int status_code, const std::string& status_text, bool keep_alive);

}
