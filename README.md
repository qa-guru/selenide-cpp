# selenide-cpp

Selenide-style C++17 UI testing in one header over W3C WebDriver:
lazy CSS locators, retrying actions/assertions and managed local ChromeDriver.
Inspired by [Selenide](https://github.com/codeborne/selenide).

## Requirements

CMake 3.20+, Clang/GCC with C++17 support, libcurl headers/library and Chrome.
For local runs on macOS/Linux, install a matching ChromeDriver in `PATH` or set
`Configuration::driverExecutable` to its path. Drivers are not downloaded.
CMake finds [nlohmann/json](https://github.com/nlohmann/json) or fetches `v3.11.3`.
On Windows, use an external driver via `Configuration::webdriverUrl`.

## Use in a project

```cmake
include(FetchContent)
FetchContent_Declare(selenide
    GIT_REPOSITORY https://github.com/qa-guru/selenide-cpp.git
    GIT_TAG v0.1.4)
FetchContent_MakeAvailable(selenide)

target_link_libraries(my_tests PRIVATE selenide::selenide)
```

```cpp
#include "selenide.hpp"
using namespace selenide;

int main() {
    open("https://the-internet.herokuapp.com/login");
    $("#username").setValue("tomsmith");
    $("#password").setValue("SuperSecretPassword!");
    $("button[type=submit]").click();
    $(".flash.success")
        .shouldBe(be::visible)
        .shouldHave(have::text("You logged into a secure area"));
}
```

## API

Indices are zero-based. Locators do not start a browser. With an empty
`Configuration::webdriverUrl`, the first browser command starts ChromeDriver
on a free local port and creates a session. `closeWebDriver()` closes the session
and stops that owned process; the next command starts a new one. Cleanup also
runs on normal process exit and when session creation fails.

Set `webdriverUrl` before the first browser command to use an existing driver,
Grid or Selenoid. Only the session is closed; the external server is never stopped.

```cpp
open(url);
closeWebDriver();
execute(script, args);       // returns nlohmann::json; args defaults to []
screenshot(path);            // viewport PNG

$(css); $(css, i); $$(css);   // first match, indexed match, collection
el.click(); el.setValue(v);  // clear + type
el.text(); el.displayed(); el.selected();
el.shouldBe(condition); el.shouldHave(condition);
els.size(); els.shouldHave(have::size(n));

be::visible; be::checked;
have::text(t); have::exactText(t); have::size(n);
```

Element IDs are re-resolved per command. Element actions and assertions retry
until `Configuration::timeout`, then throw `std::runtime_error`. Defaults:

```cpp
Configuration::webdriverUrl = "";  // managed local ChromeDriver
Configuration::driverExecutable = "chromedriver";  // PATH name or executable path
Configuration::driverLogFile = "";                 // discard output; path = keep it
Configuration::browser = "chrome";
Configuration::headless = true;
Configuration::incognito = true;  // prevent autofill UI from intercepting input
Configuration::timeout = std::chrono::milliseconds{4000};
Configuration::pollingInterval = std::chrono::milliseconds{200};
```

## Run the example

First setup (from this repository):

```bash
cmake -B build             # configure/generate build/, not compile
```

Build and run after setup or after ordinary C++ edits:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

In `--build build`, `--build` is the action; `build` is the directory name.

The [example](examples/the_internet.cpp) covers login, logout, checkboxes and
screenshots on [the-internet.herokuapp.com](https://the-internet.herokuapp.com).
[Google Test consumer tests](https://github.com/qa-guru/selenide-cpp-multistack-app-tests)
run against [autotests.ai](https://autotests.ai/stack/backend-java-spring/frontend-typescript-react/login).

## Install instead of FetchContent

From this repository. `JSON_Install=ON` also installs nlohmann_json if fetched:

```bash
cmake -B build -DJSON_Install=ON
cmake --install build --prefix "$HOME/.local"
```

```cmake
find_package(selenide REQUIRED)
target_link_libraries(my_tests PRIVATE selenide::selenide)
```

Configure the consumer with `-DCMAKE_PREFIX_PATH="$HOME/.local"`.

CSS selectors only; no iframe/window switching, download or CDP helpers.

MIT, see [LICENSE](LICENSE).
