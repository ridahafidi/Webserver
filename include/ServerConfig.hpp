#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <cstddef>

struct LocationConfig {
    std::string path;
    std::string root;
    std::string index;
    std::vector<std::string> methods;
    bool        autoindex;
    std::string redirect;
    int         redirect_code;
    std::string cgi_pass;
    std::string cgi_ext;
    std::string upload_dir;
    size_t      client_max_body_size;

    LocationConfig();
};

struct ServerConfig {
    std::string              host;
    int                      port;
    std::vector<std::string> server_names;
    std::string              root;
    std::string              index;
    std::map<int,std::string> error_pages;
    size_t                   client_max_body_size;
    std::vector<LocationConfig> locations;

    ServerConfig();
};

#endif
