// the_internet.cpp — demo test on https://the-internet.herokuapp.com
// Build & run: see README (ctest -R the_internet)
#include "selenide.hpp"

int main() {
    using namespace selenide;

    open("https://the-internet.herokuapp.com/login");
    $("#username").setValue("tomsmith");
    $("#password").setValue("SuperSecretPassword!");
    $("button[type=submit]").click();

    $(".flash.success")
        .shouldBe(be::visible)
        .shouldHave(have::text("You logged into a secure area"));

    $("a[href='/logout']").click();
    $(".flash.success").shouldHave(have::text("You logged out"));

    open("https://the-internet.herokuapp.com/checkboxes");
    $$("input[type=checkbox]").shouldHave(have::size(2));
    $("input[type=checkbox]", 1).shouldBe(be::checked);
    $("input[type=checkbox]", 0).click().shouldBe(be::checked);

    screenshot("checkboxes.png");
    std::cout << "PASS\n";
}
