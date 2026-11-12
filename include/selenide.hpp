// selenide.hpp — mini-Selenide for C++ over W3C WebDriver (libcurl + nlohmann/json)
//
// Single header, no codegen. Build: CMake (see CMakeLists.txt) or
//   clang++ -std=c++17 -Iinclude tests/the_internet.cpp -lcurl -o the_internet
// Run:    chromedriver --port=9515 &  &&  ./the_internet
#pragma once

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace selenide {

static const char* EL_KEY = "element-6066-11e4-a52e-4f735466cecf";

// ---------- Configuration (как в Selenide — статические поля) ----------
struct Configuration {
    inline static std::string webdriverUrl = "http://127.0.0.1:9515";
    inline static std::string browser = "chrome";
    inline static bool headless = true;
    inline static std::chrono::milliseconds timeout{4000};
    inline static std::chrono::milliseconds pollingInterval{200};
};

// ---------- HTTP-транспорт ----------
struct Http {
    static size_t onWrite(char* p, size_t s, size_t n, std::string* out) {
        out->append(p, s * n);
        return s * n;
    }
    static json call(const std::string& method, const std::string& url,
                     const json& body = nullptr) {
        CURL* c = curl_easy_init();
        if (!c) throw std::runtime_error("curl_easy_init failed");
        std::string resp, payload;
        curl_slist* hdr = curl_slist_append(nullptr, "Content-Type: application/json");
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onWrite);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
        if (!body.is_null()) payload = body.dump();
        if ((method == "POST" || method == "PUT") && payload.empty()) payload = "{}";
        if (!payload.empty()) curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.c_str());
        CURLcode rc = curl_easy_perform(c);
        curl_slist_free_all(hdr);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK)
            throw std::runtime_error(std::string("http: ") + curl_easy_strerror(rc));
        json r = resp.empty() ? json::object() : json::parse(resp);
        json& v = r["value"];
        if (v.is_object() && v.contains("error")) {
            std::string msg = v.value("message", "");
            throw std::runtime_error(method + " " + url + " -> " +
                v.value("error", "error") +
                (msg.empty() ? "" : ": " + msg.substr(0, 200)));
        }
        return v;
    }
};

// ---------- Driver: сессия + auto-teardown в деструкторе ----------
class Driver {
    std::string sid_;
public:
    Driver() {
        json args = {"--window-size=1440,900"};
        if (Configuration::headless) args.push_back("--headless=new");
        auto v = Http::call("POST", Configuration::webdriverUrl + "/session", {
            {"capabilities", {{"alwaysMatch", {
                {"browserName", Configuration::browser},
                {"unhandledPromptBehavior", "ignore"},
                {"goog:chromeOptions", {{"args", args}}}
            }}}}
        });
        sid_ = v.at("sessionId").get<std::string>();
    }
    ~Driver() {
        try { Http::call("DELETE", Configuration::webdriverUrl + "/session/" + sid_); }
        catch (...) {}
    }
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;
    json cmd(const std::string& m, const std::string& p, const json& b = nullptr) {
        return Http::call(m, Configuration::webdriverUrl + "/session/" + sid_ + p, b);
    }
};

// ленивая сессия: браузер открывается при первом обращении, закрывается на exit
inline Driver& driver() {
    static Driver d;
    return d;
}

// ---------- ядро Selenide: retry до таймаута ----------
// bool-функция: повторяем пока не true; void-функция: пока не перестанет кидать
template <typename F>
void retry(F&& f, const std::string& what,
           std::chrono::milliseconds timeout = Configuration::timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string last = "no attempts yet";
    for (;;) {
        try {
            if constexpr (std::is_same_v<std::invoke_result_t<F>, bool>) {
                if (f()) return;
                last = "condition is false";
            } else {
                f();
                return;
            }
        } catch (const std::exception& e) {
            last = e.what();
        }
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(what + "\n  last error: " + last);
        std::this_thread::sleep_for(Configuration::pollingInterval);
    }
}

// ---------- Element ----------
class El;
struct Condition {
    std::string name;
    std::function<bool(const El&)> check;
};

class El {
public:
    using Loc = std::pair<std::string, int>;  // css + index (-1 = first match)
    std::vector<Loc> chain_;                  // цепочка локаторов (find().find())

    explicit El(std::string css, int index = -1) : chain_{{std::move(css), index}} {}

    El find(const std::string& css) const {     // scoped child: $("form").find("input")
        El e = *this;
        e.chain_.emplace_back(css, -1);
        return e;
    }
    El get(int i) const {                       // nth match последнего локатора
        El e = *this;
        e.chain_.back().second = i;
        return e;
    }

    // actions — тоже retry'ятся до таймаута, как в Selenide
    El& click() {
        retry([&] { elCmd("POST", "/click"); }, "click " + describe());
        return *this;
    }
    El& setValue(const std::string& v) {
        retry([&] {
            elCmd("POST", "/clear");
            json chars = json::array();
            for (char ch : v) chars.push_back(std::string(1, ch));
            elCmd("POST", "/value", {{"text", v}, {"value", chars}});
        }, "setValue " + describe());
        return *this;
    }

    // state queries — кидают исключение, что внутри should* = "ещё не"
    std::string text() const { return elCmd("GET", "/text").get<std::string>(); }
    std::string attr(const std::string& n) const {
        json v = elCmd("GET", "/attribute/" + n);
        return v.is_null() ? "" : v.get<std::string>();
    }
    bool displayed() const { return elCmd("GET", "/displayed").get<bool>(); }
    bool enabled()   const { return elCmd("GET", "/enabled").get<bool>(); }
    bool selected()  const { return elCmd("GET", "/selected").get<bool>(); }
    bool exists()    const {
        try { resolve(); return true; } catch (const std::exception&) { return false; }
    }

    El& shouldBe(const Condition& c,
                 std::chrono::milliseconds t = Configuration::timeout) {
        retry([&] { return c.check(*this); }, describe() + " should be " + c.name, t);
        return *this;
    }
    El& shouldHave(const Condition& c,
                   std::chrono::milliseconds t = Configuration::timeout) {
        return shouldBe(c, t);
    }
    El& shouldNotBe(const Condition& c,
                    std::chrono::milliseconds t = Configuration::timeout) {
        retry([&] { return !c.check(*this); },
              describe() + " should not be " + c.name, t);
        return *this;
    }
    El& shouldNotHave(const Condition& c,
                      std::chrono::milliseconds t = Configuration::timeout) {
        return shouldNotBe(c, t);
    }

private:
    // свежий element id при каждом обращении — защита от stale reference
    std::string resolve() const {
        std::string base;
        for (auto& [css, idx] : chain_) {
            std::string p = base.empty() ? "" : "/element/" + base;
            if (idx < 0) {
                base = driver().cmd("POST", p + "/element",
                                    {{"using", "css selector"}, {"value", css}})
                           .at(EL_KEY).get<std::string>();
            } else {
                json v = driver().cmd("POST", p + "/elements",
                                      {{"using", "css selector"}, {"value", css}});
                if (!v.is_array() || (size_t)idx >= v.size())
                    throw std::runtime_error("no element #" + std::to_string(idx) +
                                             " for '" + css + "'");
                base = v.at(idx).at(EL_KEY).get<std::string>();
            }
        }
        return base;
    }
    json elCmd(const std::string& m, const std::string& p,
               const json& b = nullptr) const {
        return driver().cmd(m, "/element/" + resolve() + p, b);
    }
    std::string describe() const {
        std::string s;
        for (auto& [css, idx] : chain_) {
            if (!s.empty()) s += " ";
            s += "'" + css + "'";
            if (idx >= 0) s += "[" + std::to_string(idx) + "]";
        }
        return s;
    }
};

// ---------- Elements collection ----------
struct ColCondition {
    std::string name;
    std::function<bool(const class Els&)> check;
};

class Els {
    std::string css_;
    json elements() const {
        return driver().cmd("POST", "/elements",
                            {{"using", "css selector"}, {"value", css_}});
    }
public:
    explicit Els(std::string css) : css_(std::move(css)) {}

    size_t size() const {
        json v = elements();
        return v.is_array() ? v.size() : 0;
    }
    El get(int i) const { return El(css_, i); }
    std::vector<std::string> texts() const {
        std::vector<std::string> r;
        json v = elements();
        if (v.is_array())
            for (auto& e : v)
                r.push_back(driver().cmd("GET", "/element/" +
                    e.at(EL_KEY).get<std::string>() + "/text").get<std::string>());
        return r;
    }
    Els& shouldHave(const ColCondition& c,
                    std::chrono::milliseconds t = Configuration::timeout) {
        retry([&] { return c.check(*this); },
              "'" + css_ + "' should have " + c.name, t);
        return *this;
    }
};

// ---------- Conditions ----------
namespace be {
    inline const Condition exist   {"exist",   [](const El& e) { return e.exists(); }};
    inline const Condition visible {"visible", [](const El& e) { return e.displayed(); }};
    inline const Condition hidden  {"hidden",  [](const El& e) {
        return !e.exists() || !e.displayed(); }};
    inline const Condition enabled {"enabled", [](const El& e) { return e.enabled(); }};
    inline const Condition checked {"checked", [](const El& e) { return e.selected(); }};
}

namespace have {
    inline Condition text(const std::string& t) {
        return {"text '" + t + "'", [t](const El& e) {
            return e.text().find(t) != std::string::npos; }};
    }
    inline Condition exactText(const std::string& t) {
        return {"exactText '" + t + "'", [t](const El& e) { return e.text() == t; }};
    }
    inline Condition attr(const std::string& n, const std::string& v) {
        return {n + "='" + v + "'", [n, v](const El& e) { return e.attr(n) == v; }};
    }
    inline Condition value(const std::string& v) { return attr("value", v); }

    // collection conditions
    inline ColCondition size(size_t n) {
        return {"size " + std::to_string(n),
                [n](const Els& c) { return c.size() == n; }};
    }
    inline ColCondition sizeGreaterThan(size_t n) {
        return {"size > " + std::to_string(n),
                [n](const Els& c) { return c.size() > n; }};
    }
    inline ColCondition texts(const std::vector<std::string>& expected) {
        return {"texts", [expected](const Els& c) { return c.texts() == expected; }};
    }
}

// ---------- API ----------
inline El  $(const std::string& css)        { return El(css); }
inline El  $(const std::string& css, int i) { return El(css, i); }
inline Els $$(const std::string& css)       { return Els(css); }

inline void open(const std::string& url) {
    driver().cmd("POST", "/url", {{"url", url}});
}
inline std::string title() {
    return driver().cmd("GET", "/title").get<std::string>();
}
inline json execute(const std::string& script, json args = json::array()) {
    return driver().cmd("POST", "/execute/sync",
                        {{"script", script}, {"args", args}});
}

// alerts (window.confirm / alert / prompt)
inline std::string alertText() {
    return driver().cmd("GET", "/alert/text").get<std::string>();
}
inline void acceptAlert()  { driver().cmd("POST", "/alert/accept"); }
inline void dismissAlert() { driver().cmd("POST", "/alert/dismiss"); }

inline void screenshot(const std::string& file) {
    std::string b64 = driver().cmd("GET", "/screenshot").get<std::string>();
    static const std::string T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -8;
    for (char c : b64) {
        auto p = T.find(c);
        if (p == std::string::npos) continue;
        val = (val << 6) + (int)p;
        bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
    }
    std::ofstream(file, std::ios::binary) << out;
}

} // namespace selenide
