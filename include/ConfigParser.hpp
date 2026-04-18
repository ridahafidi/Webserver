#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "ServerConfig.hpp"
#include <string>
#include <vector>
#include <stdexcept>

class ConfigParser {
public:
    ConfigParser(const std::string& filename);
    std::vector<ServerConfig> parse();

private:
    std::vector<std::string> _tokens;
    size_t _pos;

    void tokenize(const std::string& content);
    const std::string& peek() const;
    std::string consume();
    bool atEnd() const;

    ServerConfig   parseServerBlock();
    LocationConfig parseLocationBlock(const ServerConfig& parent);

    void validate(const std::vector<ServerConfig>& configs);
};

#endif
