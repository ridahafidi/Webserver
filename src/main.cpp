#include "ConfigParser.hpp"
#include "Webserv.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    std::string configFile = "config/default.conf";
    if (argc > 1)
        configFile = argv[1];

    try {
        ConfigParser parser(configFile);
        std::vector<ServerConfig> configs = parser.parse();
        if (configs.empty()) {
            std::cerr << "No server configurations found." << std::endl;
            return 1;
        }
        Webserv server(configs);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
