#pragma once

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using json = nlohmann::json;

namespace selenide {

inline constexpr const char* EL_KEY = "element-6066-11e4-a52e-4f735466cecf";

struct Configuration {
    inline static std::string webdriverUrl;
    inline static std::string driverExecutable = "chromedriver";
    inline static std::string driverLogFile;  // empty = discard driver output
    inline static std::string browser = "chrome";
    inline static bool headless = true;
    // Prevent browser-owned autofill UI from intercepting auth-form key events.
    inline static bool incognito = true;
    inline static std::chrono::milliseconds timeout{4000};
    inline static std::chrono::milliseconds pollingInterval{200};
};

struct Http {
    static size_t onWrite(char* p, size_t s, size_t n, void* out) {
        static_cast<std::string*>(out)->append(p, s * n);
        return s * n;
    }
    static json call(const std::string& method, const std::string& url,
                     const json& body = nullptr, long timeoutMs = 0) {
        std::string resp, payload = body.is_null() ? "{}" : body.dump();
        CURL* c = curl_easy_init();
        if (!c) throw std::runtime_error("curl_easy_init failed");
        curl_slist* hdr = curl_slist_append(nullptr, "Content-Type: application/json");
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onWrite);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, timeoutMs);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
        if (method == "POST")
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.c_str());
        CURLcode rc = curl_easy_perform(c);
        curl_slist_free_all(hdr);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK)
            throw std::runtime_error(std::string("http: ") + curl_easy_strerror(rc));
        json v = json::parse(resp).at("value");
        if (v.contains("error")) {
            throw std::runtime_error(method + " " + url + " -> " +
                v.value("error", "error") + ": " + v.value("message", "").substr(0, 200));
        }
        return v;
    }
};

template <typename F>
void retry(F&& f, const std::string& what,
           std::chrono::milliseconds timeout = Configuration::timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string last;
    for (;;) {
        try {
            if (f()) return;
            last = "condition is false";
        } catch (const std::exception& e) {
            last = e.what();
        }
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(what + "\n  last error: " + last);
        std::this_thread::sleep_for(Configuration::pollingInterval);
    }
}

class Driver {
    std::string sid_;
    std::string url_ = Configuration::webdriverUrl;
#ifndef _WIN32
    pid_t pid_ = -1;
#endif

    bool serviceRunning() noexcept {
#ifndef _WIN32
        if (pid_ <= 0) return false;
        pid_t state;
        do { state = waitpid(pid_, nullptr, WNOHANG); } while (state < 0 && errno == EINTR);
        if (state == 0) return true;
        pid_ = -1;
#endif
        return false;
    }

    void stopService() noexcept {
#ifndef _WIN32
        if (!serviceRunning()) return;
        kill(pid_, SIGTERM);
        for (int i = 0; i < 40 && serviceRunning(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        if (serviceRunning()) {
            kill(pid_, SIGKILL);
            while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
            pid_ = -1;
        }
#endif
    }

    void startService() {
#ifdef _WIN32
        throw std::runtime_error("Set Configuration::webdriverUrl on Windows");
#else
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) throw std::runtime_error("Cannot allocate a ChromeDriver port");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t size = sizeof(address);
        int failed = bind(fd, reinterpret_cast<sockaddr*>(&address), size) ||
                     getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size);
        int error = errno;
        close(fd);
        if (failed)
            throw std::runtime_error(std::string("Cannot allocate a ChromeDriver port: ") +
                                     std::strerror(error));

        auto port = std::to_string(ntohs(address.sin_port));
        url_ = "http://127.0.0.1:" + port;
        std::string portArg = "--port=" + port;
        char* args[] = {Configuration::driverExecutable.data(), portArg.data(), nullptr};
        const char* sink = Configuration::driverLogFile.empty()
            ? "/dev/null" : Configuration::driverLogFile.c_str();
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, sink,
                                         O_WRONLY | O_CREAT | O_APPEND, 0600);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        error = posix_spawnp(&pid_, args[0], &actions, nullptr, args, ::environ);
        posix_spawn_file_actions_destroy(&actions);
        if (error) {
            pid_ = -1;
            throw std::runtime_error("Cannot start ChromeDriver '" +
                Configuration::driverExecutable + "': " + std::strerror(error) +
                ". Put it in PATH or set Configuration::driverExecutable.");
        }
        retry([&] {
            if (!serviceRunning())
                throw std::runtime_error("ChromeDriver exited before becoming ready");
            return Http::call("GET", url_ + "/status", nullptr,
                (std::max)(1L, static_cast<long>(Configuration::timeout.count())))
                .value("ready", false);
        }, "start ChromeDriver");
#endif
    }
public:
    Driver() {
        try {
            if (url_.empty()) startService();
            json args = {"--window-size=1440,900"};
            if (Configuration::headless)  args.push_back("--headless=new");
            if (Configuration::incognito) args.push_back("--incognito");
            json prefs = {
                {"credentials_enable_service", false},
                {"profile.password_manager_enabled", false}
            };
            auto v = Http::call("POST", url_ + "/session", {
                {"capabilities", {{"alwaysMatch", {
                    {"browserName", Configuration::browser},
                    {"goog:chromeOptions", {{"args", args}, {"prefs", prefs}}}
                }}}}
            });
            sid_ = v.at("sessionId").get<std::string>();
        } catch (...) {
            stopService();
            throw;
        }
    }
    ~Driver() {
        try {
            Http::call("DELETE", url_ + "/session/" + sid_, nullptr,
                (std::max)(1L, static_cast<long>(Configuration::timeout.count())));
        }
        catch (...) {}
        stopService();
    }
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;
    json cmd(const std::string& m, const std::string& p, const json& b = nullptr) {
        return Http::call(m, url_ + "/session/" + sid_ + p, b);
    }
};

// A lazy session, reset by closeWebDriver() and closed on process exit.
inline std::unique_ptr<Driver>& driverSlot() {
    static std::unique_ptr<Driver> d;
    return d;
}
inline Driver& driver() {
    if (!driverSlot()) driverSlot() = std::make_unique<Driver>();
    return *driverSlot();
}
inline void closeWebDriver() { driverSlot().reset(); }

class El;
struct Condition {
    std::string name;
    std::function<bool(const El&)> check;
};

class El {
    std::string css_;
    int index_;
public:
    explicit El(std::string css, int index = -1)
        : css_(std::move(css)), index_(index) {}

    El& click() {
        retry([&] { elCmd("POST", "/click"); return true; }, "click " + describe());
        return *this;
    }
    El& setValue(const std::string& v) {
        retry([&] {
            elCmd("POST", "/clear");
            elCmd("POST", "/value", {{"text", v}});
            return true;
        }, "setValue " + describe());
        return *this;
    }

    std::string text() const { return elCmd("GET", "/text").get<std::string>(); }
    bool displayed() const { return elCmd("GET", "/displayed").get<bool>(); }
    bool selected()  const { return elCmd("GET", "/selected").get<bool>(); }

    El& shouldBe(const Condition& c,
                 std::chrono::milliseconds t = Configuration::timeout) {
        retry([&] { return c.check(*this); }, describe() + " should be " + c.name, t);
        return *this;
    }
    El& shouldHave(const Condition& c,
                   std::chrono::milliseconds t = Configuration::timeout) {
        return shouldBe(c, t);
    }
private:
    // Resolve again for every command so retries do not reuse stale element IDs.
    std::string resolve() const {
        json v = driver().cmd("POST", index_ < 0 ? "/element" : "/elements",
                              {{"using", "css selector"}, {"value", css_}});
        return (index_ < 0 ? v : v.at(index_)).at(EL_KEY).get<std::string>();
    }
    json elCmd(const std::string& m, const std::string& p,
               const json& b = nullptr) const {
        return driver().cmd(m, "/element/" + resolve() + p, b);
    }
    std::string describe() const {
        return "'" + css_ + "'" + (index_ < 0 ? "" : "[" + std::to_string(index_) + "]");
    }
};

struct ColCondition {
    std::string name;
    std::function<bool(const class Els&)> check;
};

class Els {
    std::string css_;
public:
    explicit Els(std::string css) : css_(std::move(css)) {}

    size_t size() const {
        return driver().cmd("POST", "/elements",
                            {{"using", "css selector"}, {"value", css_}}).size();
    }
    Els& shouldHave(const ColCondition& c,
                    std::chrono::milliseconds t = Configuration::timeout) {
        retry([&] { return c.check(*this); },
              "'" + css_ + "' should have " + c.name, t);
        return *this;
    }
};

namespace be {
    inline const Condition visible {"visible", [](const El& e) { return e.displayed(); }};
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
    inline ColCondition size(size_t n) {
        return {"size " + std::to_string(n),
                [n](const Els& c) { return c.size() == n; }};
    }
}

inline El  $(const std::string& css)        { return El(css); }
inline El  $(const std::string& css, int i) { return El(css, i); }
inline Els $$(const std::string& css)       { return Els(css); }

inline void open(const std::string& url) {
    driver().cmd("POST", "/url", {{"url", url}});
}
inline json execute(const std::string& script, json args = json::array()) {
    return driver().cmd("POST", "/execute/sync",
                        {{"script", script}, {"args", args}});
}

inline void screenshot(const std::string& file) {
    std::string b64 = driver().cmd("GET", "/screenshot").get<std::string>();
    static const std::string T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned val = 0;
    int bits = -8;
    for (char c : b64) {
        auto p = T.find(c);
        if (p == std::string::npos) continue;
        val = (val << 6) + static_cast<unsigned>(p);
        bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
    }
    std::ofstream(file, std::ios::binary) << out;
}

} // namespace selenide
