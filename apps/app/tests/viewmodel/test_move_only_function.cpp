// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/move_only_function.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using ej::viewmodel::MoveOnlyFunction;

TEST_CASE("A move-only function is empty until it is given a callable", "[viewmodel][function]")
{
    MoveOnlyFunction<void()> empty;
    MoveOnlyFunction<void()> null(nullptr);
    CHECK_FALSE(static_cast<bool>(empty));
    CHECK_FALSE(static_cast<bool>(null));

    MoveOnlyFunction<void()> full = [] {};
    CHECK(static_cast<bool>(full));
}

TEST_CASE("A move-only function calls its callable with the arguments and returns the result", "[viewmodel][function]")
{
    MoveOnlyFunction<int(int, const std::string&)> add = [](int number, const std::string& text)
    { return number + static_cast<int>(text.size()); };

    CHECK(add(2, "abc") == 5);
}

TEST_CASE("A move-only function owns a move-only capture", "[viewmodel][function]")
{
    auto owned = std::make_unique<int>(41);
    MoveOnlyFunction<int()> function = [owned = std::move(owned)]() mutable { return ++*owned; };

    CHECK(function() == 42);
    CHECK(function() == 43);
}

TEST_CASE("A move-only function can be moved, and the source is then empty", "[viewmodel][function]")
{
    int calls = 0;
    MoveOnlyFunction<void()> first = [&calls] { ++calls; };

    MoveOnlyFunction<void()> second = std::move(first);
    CHECK_FALSE(static_cast<bool>(first)); // NOLINT(bugprone-use-after-move): the state after a move is specified here
    second();
    CHECK(calls == 1);

    MoveOnlyFunction<void()> third;
    third = std::move(second);
    third();
    CHECK(calls == 2);
}

TEST_CASE("A move-only function cannot be copied", "[viewmodel][function]")
{
    STATIC_CHECK_FALSE(std::is_copy_constructible_v<MoveOnlyFunction<void()>>);
    STATIC_CHECK_FALSE(std::is_copy_assignable_v<MoveOnlyFunction<void()>>);
    STATIC_CHECK(std::is_nothrow_move_constructible_v<MoveOnlyFunction<void()>>);
}

TEST_CASE("A move-only function takes a reference argument and passes it on", "[viewmodel][function]")
{
    MoveOnlyFunction<void(std::string&)> append = [](std::string& text) { text += "!"; };
    std::string text = "done";

    append(text);

    CHECK(text == "done!");
}

TEST_CASE("The callable is destroyed with the function", "[viewmodel][function]")
{
    auto witness = std::make_shared<int>(1);
    {
        MoveOnlyFunction<void()> function = [witness] {};
        CHECK(witness.use_count() == 2);
    }
    CHECK(witness.use_count() == 1);
}
