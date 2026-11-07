// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <string>
#include <string_view>

#include "error_journaling/greeting.hpp"
#include "error_journaling/version.hpp"

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--version")
    {
        std::printf("error-journaling %s\n", std::string(error_journaling::VERSION_STRING).c_str());
        return 0;
    }
    if (argc > 2)
    {
        std::fprintf(stderr, "usage: error-journaling [NAME | --version]\n");
        return 2;
    }

    const error_journaling::Greeter greeter(argc == 2 ? argv[1] : "world");
    std::printf("%s\n", greeter.greet().c_str());
    return 0;
}
