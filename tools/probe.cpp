// probe.cpp — сдампить живой DOM new-app.qa.guru после регистрации
#include "selenide.hpp"

int main() {
    using namespace selenide;

    const std::string user =
        "probe_" + std::to_string(std::chrono::steady_clock::now()
                                      .time_since_epoch().count() % 1000000);

    open("https://new-app.qa.guru/register");
    $("[data-testid=register-login-input]").setValue(user);
    $("[data-testid=register-password-input]").setValue("qwerty123");
    $("[data-testid=confirm-password-input]").setValue("qwerty123");
    $("[data-testid=register-submit-button]").click();
    $("main").shouldBe(be::visible);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    std::string dump = execute(R"(
        const out = {};
        out.url = location.href;
        out.testids = [...document.querySelectorAll('[data-testid]')]
            .map(e => e.getAttribute('data-testid') + ' <' + e.tagName.toLowerCase() +
                 '> "' + e.textContent.trim().slice(0, 60) + '"');
        out.main = document.querySelector('main') ? document.querySelector('main').innerText.slice(0, 600) : null;
        out.lsKeys = Object.keys(localStorage);
        return JSON.stringify(out, null, 1);
    )").get<std::string>();
    std::cout << dump << "\n";
}
