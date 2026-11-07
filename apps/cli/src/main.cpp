// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <string>
#include <string_view>

#include "icl_template/greeting.hpp"
#include "icl_template/version.hpp"

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--version")
    {
        std::printf("icl-template %s\n", std::string(icl_template::VERSION_STRING).c_str());
        return 0;
    }
    if (argc > 2)
    {
        std::fprintf(stderr, "usage: icl-template [NAME | --version]\n");
        return 2;
    }

    const icl_template::Greeter greeter(argc == 2 ? argv[1] : "world");
    std::printf("%s\n", greeter.greet().c_str());
    return 0;
}
