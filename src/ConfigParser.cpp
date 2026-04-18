#include "ConfigParser.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>

LocationConfig::LocationConfig()
    : autoindex(false), redirect_code(0), client_max_body_size(0) {}

ServerConfig::ServerConfig()
    : host("0.0.0.0"), port(80), client_max_body_size(1 * 1024 * 1024) {}

static size_t parseSize(const std::string& s) {
    if (s.empty()) return 0;
    size_t i = 0;
    size_t value = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        value = value * 10 + static_cast<size_t>(s[i] - '0');
        ++i;
    }
    if (i < s.size()) {
        char unit = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
        if (unit == 'K') value *= 1024;
        else if (unit == 'M') value *= 1024 * 1024;
        else if (unit == 'G') value *= 1024 * 1024 * 1024;
    }
    return value;
}

ConfigParser::ConfigParser(const std::string& filename) : _pos(0) {
    std::ifstream file(filename.c_str());
    if (!file.is_open())
        throw std::runtime_error("Cannot open config file: " + filename);
    std::ostringstream oss;
    oss << file.rdbuf();
    tokenize(oss.str());
}

void ConfigParser::tokenize(const std::string& content) {
    size_t i = 0;
    while (i < content.size()) {
        if (std::isspace(static_cast<unsigned char>(content[i]))) {
            ++i;
            continue;
        }
        if (content[i] == '#') {
            while (i < content.size() && content[i] != '\n')
                ++i;
            continue;
        }
        if (content[i] == '{' || content[i] == '}' || content[i] == ';') {
            _tokens.push_back(std::string(1, content[i]));
            ++i;
            continue;
        }
        size_t start = i;
        while (i < content.size()
               && !std::isspace(static_cast<unsigned char>(content[i]))
               && content[i] != '{' && content[i] != '}'
               && content[i] != ';' && content[i] != '#')
            ++i;
        if (i > start)
            _tokens.push_back(content.substr(start, i - start));
    }
}

bool ConfigParser::atEnd() const {
    return _pos >= _tokens.size();
}

const std::string& ConfigParser::peek() const {
    static const std::string empty;
    if (_pos >= _tokens.size()) return empty;
    return _tokens[_pos];
}

std::string ConfigParser::consume() {
    if (_pos >= _tokens.size())
        throw std::runtime_error("Unexpected end of config");
    return _tokens[_pos++];
}

std::vector<ServerConfig> ConfigParser::parse() {
    std::vector<ServerConfig> configs;
    while (!atEnd()) {
        std::string tok = consume();
        if (tok == "server") {
            if (consume() != "{")
                throw std::runtime_error("Expected '{' after 'server'");
            configs.push_back(parseServerBlock());
        } else {
            throw std::runtime_error("Unexpected token: " + tok);
        }
    }
    validate(configs);
    return configs;
}

ServerConfig ConfigParser::parseServerBlock() {
    ServerConfig cfg;
    while (!atEnd() && peek() != "}") {
        std::string key = consume();
        if (key == "listen") {
            std::string val = consume();
            size_t colon = val.find(':');
            if (colon != std::string::npos) {
                cfg.host = val.substr(0, colon);
                cfg.port = std::atoi(val.substr(colon + 1).c_str());
            } else {
                cfg.port = std::atoi(val.c_str());
            }
            consume(); // ;
        } else if (key == "server_name") {
            while (!atEnd() && peek() != ";") {
                cfg.server_names.push_back(consume());
            }
            consume(); // ;
        } else if (key == "root") {
            cfg.root = consume();
            consume(); // ;
        } else if (key == "index") {
            cfg.index = consume();
            consume(); // ;
        } else if (key == "client_max_body_size") {
            cfg.client_max_body_size = parseSize(consume());
            consume(); // ;
        } else if (key == "error_page") {
            int code = std::atoi(consume().c_str());
            std::string page = consume();
            cfg.error_pages[code] = page;
            consume(); // ;
        } else if (key == "location") {
            std::string path = consume();
            if (consume() != "{")
                throw std::runtime_error("Expected '{' after location path");
            LocationConfig loc = parseLocationBlock(cfg);
            loc.path = path;
            cfg.locations.push_back(loc);
        } else if (key == "}") {
            break;
        } else {
            while (!atEnd() && peek() != ";")
                consume();
            if (!atEnd()) consume(); // ;
        }
    }
    if (!atEnd()) consume(); // }
    return cfg;
}

LocationConfig ConfigParser::parseLocationBlock(const ServerConfig& parent) {
    LocationConfig loc;
    loc.root = parent.root;
    loc.index = parent.index;
    loc.client_max_body_size = parent.client_max_body_size;

    while (!atEnd() && peek() != "}") {
        std::string key = consume();
        if (key == "allow_methods") {
            while (!atEnd() && peek() != ";") {
                loc.methods.push_back(consume());
            }
            consume(); // ;
        } else if (key == "root") {
            loc.root = consume();
            consume(); // ;
        } else if (key == "index") {
            loc.index = consume();
            consume(); // ;
        } else if (key == "autoindex") {
            std::string val = consume();
            loc.autoindex = (val == "on");
            consume(); // ;
        } else if (key == "return") {
            loc.redirect_code = std::atoi(consume().c_str());
            if (!atEnd() && peek() != ";")
                loc.redirect = consume();
            consume(); // ;
        } else if (key == "cgi_pass") {
            loc.cgi_pass = consume();
            consume(); // ;
        } else if (key == "cgi_ext") {
            loc.cgi_ext = consume();
            consume(); // ;
        } else if (key == "upload_dir") {
            loc.upload_dir = consume();
            consume(); // ;
        } else if (key == "client_max_body_size") {
            loc.client_max_body_size = parseSize(consume());
            consume(); // ;
        } else if (key == "}") {
            break;
        } else {
            while (!atEnd() && peek() != ";")
                consume();
            if (!atEnd()) consume(); // ;
        }
    }
    if (!atEnd()) consume(); // }
    return loc;
}

void ConfigParser::validate(const std::vector<ServerConfig>& configs) {
    if (configs.empty())
        throw std::runtime_error("No server blocks found in config");
    for (size_t i = 0; i < configs.size(); ++i) {
        if (configs[i].port <= 0 || configs[i].port > 65535)
            throw std::runtime_error("Invalid port number");
    }
}
