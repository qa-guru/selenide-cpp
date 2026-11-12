# selenide-cpp

Selenide-style UI testing in modern C++ — a single header over the W3C
WebDriver protocol. Inspired by [codeborne/selenide](https://github.com/codeborne/selenide).

Selenium has no official C++ bindings, but WebDriver is just HTTP. This is a
~350-line proof that the parts people actually love about Selenide —
lazy locators, fluent assertions, automatic waiting, automatic teardown —
fit in one `.hpp`.

```cpp
#include "selenide.hpp"
using namespace selenide;

open("https://example.com/login");

$("#username").setValue("tomsmith");
$("#password").setValue("SuperSecretPassword!");
$("button[type=submit]").click();

$(".flash.success")
    .shouldBe(be::visible)
    .shouldHave(have::text("You logged into a secure area"));

$$("input[type=checkbox]").shouldHave(have::size(2));
```

## Why it feels like Selenide

- **Lazy locators** — `$()`, `$$()`, `el.find()`, `el.get(i)` do not touch the
  browser until an action or assertion.
- **Automatic waiting** — every action and every `should*` retries until
  `Configuration::timeout` (4 s default). No sleeps, no explicit waits.
- **No stale references** — the element id is re-resolved on each call;
  `StaleElementReferenceException` cannot happen.
- **Auto teardown** — the driver session is a lazy singleton closed by its
  destructor when the process exits.
- **Fluent conditions** — `be::visible/hidden/enabled/checked/exist`,
  `have::text/exactText/attr/value`; collections get
  `have::size/sizeGreaterThan/texts`.

## Dependencies

- C++17 compiler
- `libcurl` (system on macOS/Linux)
- [`nlohmann/json`](https://github.com/nlohmann/json) — `find_package` or
  fetched automatically by CMake
- `chromedriver` (or geckodriver — point `Configuration::webdriverUrl` at it)

## Use it as a dependency

`selenide-cpp` is a proper CMake package — `selenide::selenide` INTERFACE
target, header + transitive deps (`CURL`, `nlohmann_json`) resolved for you.

**FetchContent — pin to a release tag (no install step):**

```cmake
include(FetchContent)
FetchContent_Declare(selenide
    GIT_REPOSITORY https://github.com/qa-guru/selenide-cpp.git
    GIT_TAG v0.1.0)
FetchContent_MakeAvailable(selenide)

target_link_libraries(my_tests PRIVATE selenide::selenide)
```

**find_package — after installing to a prefix:**

```bash
git clone https://github.com/qa-guru/selenide-cpp.git
cmake -B build -S selenide-cpp && cmake --install build --prefix ~/.local
```

```cmake
find_package(selenide REQUIRED)
target_link_libraries(my_tests PRIVATE selenide::selenide)
```

## Quick start (framework dev loop)

```bash
# chromedriver in another shell (any WebDriver-capable hub works)
chromedriver --port=9515

cmake -B build && cmake --build build
ctest --test-dir build          # runs the_internet e2e
```

Or without CMake (macOS):

```bash
brew install nlohmann-json
clang++ -std=c++17 -Iinclude examples/the_internet.cpp -lcurl -o the_internet
./the_internet
```

`$`/`$$` are legal identifiers in clang/gcc; on clang pass
`-Wno-dollar-in-identifier-extension` to silence the warning (CMake does it).

## Examples & tools

| Binary | Target | What it covers |
|--------|--------|----------------|
| `the_internet` | [the-internet.herokuapp.com](https://the-internet.herokuapp.com) | form auth, flash asserts, checkbox collection, `screenshot()` |
| `dom_probe` | any | dumps every `[data-testid]` on a page — the tool used to discover real selectors before writing assertions |

Real project tests live in the consumer repo:
[qa-guru/selenide-cpp-multistack-app-tests](https://github.com/qa-guru/selenide-cpp-multistack-app-tests) —
register/login/logout/delete-account e2e against the Multistack demo stands,
consuming this library via `FetchContent`.

## API cheat sheet

```cpp
// browser
open(url)                        // GET /session/{id}/url
title()                          // page title
execute(script, args)            // /execute/sync — JS, returns nlohmann::json
screenshot(path)                 // viewport PNG
alertText() / acceptAlert() / dismissAlert()

// elements
$(css)  $(css, i)  $$(css)
el.find(css)  el.get(i)
el.click()  el.setValue(v)       // clear + sendKeys
el.text()  el.attr(name)  el.displayed()  el.enabled()  el.selected()  el.exists()

// assertions — all retry until Configuration::timeout
el.shouldBe(c) / shouldHave(c) / shouldNotBe(c) / shouldNotHave(c)
els.shouldHave(colCondition)

// conditions
be::exist visible hidden enabled checked
have::text(t) exactText(t) attr(n, v) value(v)
have::size(n) sizeGreaterThan(n) texts({...})

// configuration (static fields, like Selenide's Configuration)
selenide::Configuration::webdriverUrl = "http://127.0.0.1:9515";
selenide::Configuration::headless = true;
selenide::Configuration::incognito = true;  // clean profile, no autofill UI
selenide::Configuration::timeout = std::chrono::milliseconds{4000};
selenide::Configuration::pollingInterval = std::chrono::milliseconds{200};
```

## How it maps to WebDriver

| Selenide-style call | W3C WebDriver |
|---|---|
| `$(css)` → resolve | `POST /session/{id}/element` (css selector) |
| `$$(css)` / `.get(i)` | `POST .../elements` |
| `.find(css)` | `POST /element/{id}/element` |
| `.click()` | `POST /element/{id}/click` |
| `.setValue()` | `POST /element/{id}/clear` + `.../value` |
| `.text()` | `GET /element/{id}/text` |
| `displayed/enabled/selected` | `GET /element/{id}/displayed|enabled|selected` |
| `screenshot()` | `GET /screenshot` |
| `execute()` | `POST /execute/sync` |
| alerts | `GET /alert/text`, `POST /alert/accept|dismiss` |

`unhandledPromptBehavior: ignore` is set on session creation, so
`window.confirm` stays open until you accept/dismiss it explicitly.

## Deliberate limits (vs real Selenide)

- CSS selectors only (add more `using` strategies if you need xpath)
- no page-load / download waiting helpers, no iframe or window switching
- no Page Objects, no proxy/CDP network interception
- runner integration is just `main()` — wrap `should*` failures in your
  framework's `ASSERT_*` if you want per-test reports

## License

MIT — see [LICENSE](LICENSE).
