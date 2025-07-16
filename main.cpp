#include <iostream>
#include <nlohmann/json.hpp>

int main()
{
    std::cout << "JSON: "
              << NLOHMANN_JSON_VERSION_MAJOR << '.'
              << NLOHMANN_JSON_VERSION_MINOR << '.'
              << NLOHMANN_JSON_VERSION_PATCH << '\n';

    return 0;
}
