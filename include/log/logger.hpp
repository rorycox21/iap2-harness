#pragma once
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace log {

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void debug(std::string_view) = 0;
    virtual void info(std::string_view)  = 0;
    virtual void warn(std::string_view)  = 0;
    virtual void error(std::string_view) = 0;
};

class StderrLogger : public ILogger {
public:
    void debug(std::string_view m) override { std::fprintf(stderr, "[debug] %.*s\n", (int)m.size(), m.data()); }
    void info(std::string_view m)  override { std::fprintf(stderr, "[info ] %.*s\n", (int)m.size(), m.data()); }
    void warn(std::string_view m)  override { std::fprintf(stderr, "[warn ] %.*s\n", (int)m.size(), m.data()); }
    void error(std::string_view m) override { std::fprintf(stderr, "[error] %.*s\n", (int)m.size(), m.data()); }
};

class BufferLogger : public ILogger {
public:
    void debug(std::string_view m) override { lines_.emplace_back("debug", std::string(m)); }
    void info(std::string_view m)  override { lines_.emplace_back("info",  std::string(m)); }
    void warn(std::string_view m)  override { lines_.emplace_back("warn",  std::string(m)); }
    void error(std::string_view m) override { lines_.emplace_back("error", std::string(m)); }
    std::vector<std::pair<std::string, std::string>> const& lines() const { return lines_; }
    void clear() { lines_.clear(); }
private:
    std::vector<std::pair<std::string, std::string>> lines_;
};

}  // namespace log
