#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>

#include "store/store.hpp"

namespace
{
    struct CachedProbe
    {
        int value = 1;
    };
}  // namespace

TEST_CASE("Store::cachedObject returns the registered object and follows replaceObject", "[store][cache]")
{
    auto original = Store::getObject<CachedProbe>();
    CHECK(Store::cachedObject<CachedProbe>() == original);
    CHECK(Store::cachedObject<CachedProbe>() == original);  // second call uses the thread's own pointer

    auto replacement = std::make_shared<CachedProbe>();
    Store::replaceObject<CachedProbe>(replacement);
    CHECK(Store::cachedObject<CachedProbe>() == replacement);  // the replace is noticed without any other signal

    Store::replaceObject<CachedProbe>(original);
    CHECK(Store::cachedObject<CachedProbe>() == original);
}

TEST_CASE("Store::cachedObject notices a replace made by another thread", "[store][cache]")
{
    auto original    = Store::getObject<CachedProbe>();
    auto replacement = std::make_shared<CachedProbe>();
    REQUIRE(Store::cachedObject<CachedProbe>() == original);  // this thread has cached the original

    std::thread other([&] { Store::replaceObject<CachedProbe>(replacement); });
    other.join();

    CHECK(Store::cachedObject<CachedProbe>() == replacement);
    Store::replaceObject<CachedProbe>(original);
}
