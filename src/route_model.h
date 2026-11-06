#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace magpie {
using Json = nlohmann::json;

inline std::string Text(const Json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : "";
}

struct Route {
    std::string agent, provider, model, requested, served, upstream, time, phase;
    int status = 0;
    bool fallback = false;
    bool failed = false;
    long long ms = 0;
    long long ttftMs = 0;
    double outputSpeed = 0;
};

inline bool Bool(const Json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() && found->get<bool>();
}

inline double Number(const Json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<double>() : 0.0;
}

inline Route ParseRoute(const Json& value, bool history)
{
    Route route;
    route.agent = Text(value, "agent");
    route.provider = Text(value, "provider");
    route.model = Text(value, "model");
    route.requested = Text(value, "req");
    route.served = Text(value, "served");
    route.upstream = Text(value, "upstream");
    route.time = Text(value, history ? "t" : "time");
    route.phase = Text(value, "phase");
    if (value.contains("status") && value["status"].is_number_integer())
        route.status = value["status"].get<int>();
    if (value.contains("fallback") && value["fallback"].is_boolean())
        route.fallback = value["fallback"].get<bool>();
    route.failed = Bool(value, "failed");
    route.ms = static_cast<long long>(Number(value, "ms"));
    route.ttftMs = static_cast<long long>(Number(value, "ttft_ms"));
    double output = Number(value, "out");
    const double reasoning = Number(value, "reasoning");
    const double firstText = Number(value, "first_text_ms");
    const double flow = Number(value, "flow_ms");
    double start = route.ttftMs;
    bool measured = !Bool(value, "reasoning_hidden") || firstText > 0;
    if (reasoning > 0) {
        if (firstText > 0) {
            output -= reasoning;
            start = firstText;
        } else if (Number(value, "first_reasoning_ms") >= route.ttftMs && flow > 0 && reasoning <= output) {
            start = Number(value, "first_reasoning_ms");
        } else {
            measured = false;
        }
    }
    const double window = route.ms - start;
    if (history && measured && !route.failed && route.status < 400 && Text(value, "err").empty() &&
        route.ttftMs > 0 && output > 0 && window >= 100 && output * 1000 <= 10000 * window &&
        !(flow > 0 && flow * 4 < window) && !(window < 1000 && start > 4 * window) &&
        !(reasoning > 0 && firstText > 0 && output * (firstText - route.ttftMs) > 20 * reasoning * window))
        route.outputSpeed = output * 1000 / window;
    return route;
}

inline std::string FormatOutputSpeed(const Route& route)
{
    if (route.outputSpeed <= 0 || !std::isfinite(route.outputSpeed)) return "-";
    char text[64];
    std::snprintf(text, sizeof(text), "%.1fT/S", route.outputSpeed);
    return text;
}

inline std::vector<Route> ParseRoutes(const std::string& body, const std::string& agent, bool history)
{
    const auto document = Json::parse(body);
    const char* key = history ? "rows" : "routes";
    if (!document.is_object() || !document.contains(key) || !document[key].is_array())
        throw std::runtime_error("Unexpected Magpie response schema");
    std::vector<Route> routes;
    for (const auto& value : document[key]) {
        if (!value.is_object())
            throw std::runtime_error("Unexpected Magpie route schema");
        Route route = ParseRoute(value, history);
        if (!agent.empty() && route.agent != agent)
            continue;
        if (history && (Text(value, "source") == "log" || Text(value, "source") == "reported"))
            continue;
        if (!history && (route.phase == "done" || route.phase == "dropped"))
            continue;
        routes.push_back(std::move(route));
    }
    return routes;
}

inline std::string ToUpper(std::string s)
{
    for (char& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

inline std::string ExtractGroupName(const std::string& requested)
{
    if (requested.rfind("group/", 0) == 0)
        return requested.size() > 6 ? ToUpper(requested.substr(6)) : "-";
    return requested.empty() ? "-" : "DIRECT";
}

inline std::string FormatProvider(const std::string& provider)
{
    if (provider.empty()) return "-";
    return ToUpper(provider);
}

inline std::string FormatModelName(std::string model)
{
    if (model.rfind("claude-", 0) == 0)
        model = model.substr(7);
    else if (model.rfind("deepseek-", 0) == 0)
        model = model.substr(9);

    // 替换点号形式: sonnet-5-5 -> sonnet-5.5
    size_t lastDash = model.rfind('-');
    if (lastDash != std::string::npos && lastDash > 0) {
        size_t prevDash = model.rfind('-', lastDash - 1);
        if (prevDash != std::string::npos) {
            bool isNum = true;
            for (size_t i = prevDash + 1; i < lastDash; ++i) if (!isdigit(model[i])) isNum = false;
            for (size_t i = lastDash + 1; i < model.size(); ++i) if (!isdigit(model[i])) isNum = false;
            if (isNum) {
                model[lastDash] = '.';
            }
        }
    }

    // 去除冗余后缀
    const std::vector<std::string> suffixes = {"-high", "-low", "-medium", "-thinking", "-preview", "-latest", "-0806", "-2024", "-2025", "-2026"};
    for (const auto& s : suffixes) {
        if (model.size() > s.size() && model.rfind(s) == model.size() - s.size()) {
            model.erase(model.size() - s.size());
        }
    }

    // 针对 gemini-3.8-flash 去掉 flash 保留核心
    if (model.rfind("-flash") != std::string::npos) {
        size_t pos = model.rfind("-flash");
        model.erase(pos, 6);
    }

    return ToUpper(model);
}

inline std::string RouteText(const Route& route, bool compact = true)
{
    const auto model = route.served.empty() ? route.model : route.served;
    if (route.provider.empty() && model.empty())
        return "...";
    if (compact)
        return FormatModelName(model);
    return ToUpper(model);
}

inline std::string PercentEncode(const std::string& value)
{
    const char* digits = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char character : value) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.')
            encoded += static_cast<char>(character);
        else {
            encoded += '%';
            encoded += digits[character >> 4];
            encoded += digits[character & 15];
        }
    }
    return encoded;
}

inline std::string HistoryPath(const std::string& agent)
{
    std::string path = "/v1/magpie/usage/requests?period=month&via=through&computer=this&limit=1&offset=0";
    if (!agent.empty())
        path += "&agent=" + PercentEncode(agent);
    return path;
}

struct Concurrency {
    int requests = 0, waiting = 0, tools = 0;
};

inline Concurrency ParseConcurrency(const std::string& body)
{
    const auto document = Json::parse(body);
    if (!document.is_object() || !document.contains("requests"))
        throw std::runtime_error("Unexpected Magpie concurrency schema");
    Concurrency result;
    result.requests = static_cast<int>(Number(document, "requests"));
    result.waiting = static_cast<int>(Number(document, "waiting"));
    result.tools = static_cast<int>(Number(document, "tools"));
    return result;
}

struct Quota {
    std::string text = "-";
    std::string detail;
};

inline std::string Percent(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f%%", std::round(value));
    return buffer;
}

inline Quota ParseQuota(const std::string& body)
{
    const auto document = Json::parse(body);
    if (!document.is_object() || !document.contains("data") || !document["data"].is_array())
        throw std::runtime_error("Unexpected Magpie quota schema");
    const Json* chosen = nullptr;
    for (const auto& card : document["data"])
        if (card.is_object() && Bool(card, "last")) { chosen = &card; break; }
    if (!chosen)
        for (const auto& card : document["data"])
            if (card.is_object()) { chosen = &card; break; }
    Quota quota;
    if (!chosen)
        return quota;
    quota.detail = "Quota: " + Text(*chosen, "provider");
    const Json* worst = nullptr;
    const auto windows = chosen->find("windows");
    if (windows != chosen->end() && windows->is_array()) {
        for (const auto& window : *windows) {
            if (!window.is_object() || Bool(window, "unlimited"))
                continue;
            const auto used = window.find("used");
            if (used == window.end() || !used->is_number())
                continue;
            quota.detail += "\n  " + Text(window, "name") + ": " + Percent(used->get<double>()) + " used";
            if (!worst || used->get<double>() > Number(*worst, "used"))
                worst = &window;
        }
    }
    const auto balance = Text(*chosen, "balance");
    if (!balance.empty())
        quota.detail += "\n  Balance: " + balance;
    if (worst) {
        const auto name = ToUpper(Text(*worst, "name"));
        quota.text = (name.empty() ? "" : name + " ") + Percent(Number(*worst, "used"));
    } else if (!balance.empty()) {
        quota.text = balance;
    }
    return quota;
}

struct UsageSummary {
    long long calls = 0, errors = 0, input = 0, output = 0;
    double cost = 0.0;
};

inline UsageSummary ParseUsage(const std::string& body)
{
    const auto document = Json::parse(body);
    if (!document.is_object() || !document.contains("calls") || !document.contains("cost"))
        throw std::runtime_error("Unexpected Magpie usage schema");
    UsageSummary usage;
    usage.calls = static_cast<long long>(Number(document, "calls"));
    usage.errors = static_cast<long long>(Number(document, "errors"));
    usage.input = static_cast<long long>(Number(document, "input"));
    usage.output = static_cast<long long>(Number(document, "output"));
    usage.cost = Number(document, "cost");
    return usage;
}

inline std::string FormatCost(double cost)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), cost >= 1000 ? "$%.0f" : "$%.2f", cost);
    return buffer;
}

inline std::string FormatCount(long long count)
{
    char buffer[32];
    if (count >= 1000000)
        std::snprintf(buffer, sizeof(buffer), "%.1fM", count / 1000000.0);
    else if (count >= 1000)
        std::snprintf(buffer, sizeof(buffer), "%.1fK", count / 1000.0);
    else
        std::snprintf(buffer, sizeof(buffer), "%lld", count);
    return buffer;
}

inline std::string FormatLatency(long long milliseconds)
{
    if (milliseconds <= 0)
        return "-";
    char buffer[32];
    if (milliseconds < 1000)
        std::snprintf(buffer, sizeof(buffer), "%lldMS", milliseconds);
    else
        std::snprintf(buffer, sizeof(buffer), "%.1fS", milliseconds / 1000.0);
    return buffer;
}

inline std::string StatusText(const std::vector<Route>& live, const std::vector<Route>& history)
{
    for (const auto& route : live)
        if (route.failed || route.status >= 400)
            return "ERR " + (route.status ? std::to_string(route.status) : std::string("?"));
    for (const auto& route : live)
        if (route.fallback)
            return "FALLBACK";
    if (!history.empty() && (history.front().failed || history.front().status >= 400))
        return "ERR " + (history.front().status ? std::to_string(history.front().status) : std::string("?"));
    return "OK";
}
}
