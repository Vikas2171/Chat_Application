#pragma once
#include <string>
#include <vector>

// TCP is a byte stream with no message boundaries: a single recv() can
// return a partial message, multiple messages concatenated together, or
// both. LineFramer accumulates raw bytes as they arrive and yields complete
// '\n'-delimited messages (delimiter stripped) as soon as they're fully
// available, retaining any trailing partial data for the next feed() call.
//
// Both Server.cpp and Client.cpp include this header so the two endpoints
// can never silently disagree on how messages are framed on the wire.
class LineFramer {
public:
    std::vector<std::string> feed(const char* data, int len) {
        buffer_.append(data, len);

        std::vector<std::string> lines;
        size_t pos;
        while ((pos = buffer_.find('\n')) != std::string::npos) {
            lines.push_back(buffer_.substr(0, pos));
            buffer_.erase(0, pos + 1);
        }
        return lines;
    }

private:
    std::string buffer_;
};
