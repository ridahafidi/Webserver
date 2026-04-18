#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#include "ServerConfig.hpp"
#include "Connection.hpp"
#include <vector>
#include <map>
#include <poll.h>

class Webserv {
public:
    Webserv(const std::vector<ServerConfig>& configs);
    ~Webserv();

    void run();

private:
    std::vector<ServerConfig>   _configs;
    std::vector<int>            _serverFds;
    std::map<int,int>           _fdToConfig;
    std::map<int,Connection*>   _connections;
    std::map<int,int>           _cgiToClient;
    std::vector<pollfd>         _pollfds;

    void setupServers();
    int  createServerSocket(const ServerConfig& cfg);
    void acceptConnection(int serverFd);
    void handleClientRead(int fd);
    void handleClientWrite(int fd);
    void handleCgiRead(int fd);
    void closeConnection(int fd);
    void checkTimeouts();
    void rebuildPollFds();
    bool isServerFd(int fd) const;
    const ServerConfig& getConfigForClient(int clientFd) const;
};

#endif
