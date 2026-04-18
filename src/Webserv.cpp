#include "Webserv.hpp"
#include "HttpResponse.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <csignal>

static volatile bool g_running = true;

static void signalHandler(int) {
    g_running = false;
}

Webserv::Webserv(const std::vector<ServerConfig>& configs) : _configs(configs) {}

Webserv::~Webserv() {
    for (std::map<int,Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        delete it->second;
    }
    for (size_t i = 0; i < _serverFds.size(); ++i)
        close(_serverFds[i]);
}

void Webserv::setupServers() {
    for (size_t i = 0; i < _configs.size(); ++i) {
        int fd = createServerSocket(_configs[i]);
        if (fd < 0) {
            std::cerr << "Failed to create server socket for port "
                      << _configs[i].port << std::endl;
            continue;
        }
        _serverFds.push_back(fd);
        _fdToConfig[fd] = static_cast<int>(i);
        std::cout << "Listening on " << _configs[i].host
                  << ":" << _configs[i].port << std::endl;
    }
}

int Webserv::createServerSocket(const ServerConfig& cfg) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(cfg.port));
    if (cfg.host == "0.0.0.0" || cfg.host.empty())
        addr.sin_addr.s_addr = INADDR_ANY;
    else
        inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr);

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool Webserv::isServerFd(int fd) const {
    for (size_t i = 0; i < _serverFds.size(); ++i)
        if (_serverFds[i] == fd) return true;
    return false;
}

void Webserv::rebuildPollFds() {
    _pollfds.clear();
    _cgiToClient.clear();

    for (size_t i = 0; i < _serverFds.size(); ++i) {
        pollfd pfd;
        pfd.fd = _serverFds[i];
        pfd.events  = POLLIN;
        pfd.revents = 0;
        _pollfds.push_back(pfd);
    }

    for (std::map<int,Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        Connection* conn = it->second;
        ConnectionState state = conn->getState();

        if (state == CONN_READING) {
            pollfd pfd;
            pfd.fd = conn->getFd();
            pfd.events  = POLLIN;
            pfd.revents = 0;
            _pollfds.push_back(pfd);
        } else if (state == CONN_WRITING) {
            pollfd pfd;
            pfd.fd = conn->getFd();
            pfd.events  = POLLOUT;
            pfd.revents = 0;
            _pollfds.push_back(pfd);
        } else if (state == CONN_CGI_WAIT) {
            int cgiFd = conn->getCgiFd();
            if (cgiFd >= 0) {
                pollfd pfd;
                pfd.fd = cgiFd;
                pfd.events  = POLLIN;
                pfd.revents = 0;
                _pollfds.push_back(pfd);
                _cgiToClient[cgiFd] = conn->getFd();
            } else {
                conn->onCgiReadable();
            }
        }
    }
}

void Webserv::acceptConnection(int serverFd) {
    struct sockaddr_in clientAddr;
    socklen_t len = sizeof(clientAddr);
    int clientFd = accept(serverFd,
                          reinterpret_cast<struct sockaddr*>(&clientAddr), &len);
    if (clientFd < 0) return;

    fcntl(clientFd, F_SETFL, O_NONBLOCK);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
    std::string clientIP(ip);

    int cfgIdx = _fdToConfig[serverFd];
    Connection* conn = new Connection(clientFd, clientIP,
                                      _configs[static_cast<size_t>(cfgIdx)]);
    _connections[clientFd] = conn;
    _fdToConfig[clientFd]  = cfgIdx;
}

void Webserv::handleClientRead(int fd) {
    std::map<int,Connection*>::iterator it = _connections.find(fd);
    if (it == _connections.end()) return;
    if (!it->second->onReadable() || it->second->shouldClose())
        closeConnection(fd);
}

void Webserv::handleClientWrite(int fd) {
    std::map<int,Connection*>::iterator it = _connections.find(fd);
    if (it == _connections.end()) return;
    if (!it->second->onWritable() || it->second->shouldClose())
        closeConnection(fd);
}

void Webserv::handleCgiRead(int cgiFd) {
    std::map<int,int>::iterator cit = _cgiToClient.find(cgiFd);
    if (cit == _cgiToClient.end()) return;
    int clientFd = cit->second;
    _cgiToClient.erase(cit);

    std::map<int,Connection*>::iterator it = _connections.find(clientFd);
    if (it == _connections.end()) return;
    it->second->onCgiReadable();
    if (it->second->shouldClose())
        closeConnection(clientFd);
}

void Webserv::closeConnection(int fd) {
    std::map<int,Connection*>::iterator it = _connections.find(fd);
    if (it == _connections.end()) return;

    int cgiFd = it->second->getCgiFd();
    if (cgiFd >= 0) _cgiToClient.erase(cgiFd);

    delete it->second;
    _connections.erase(it);
    _fdToConfig.erase(fd);
}

void Webserv::checkTimeouts() {
    time_t now = time(NULL);
    std::vector<int> toClose;
    for (std::map<int,Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        if (now - it->second->getLastActivity() > 60)
            toClose.push_back(it->first);
    }
    for (size_t i = 0; i < toClose.size(); ++i)
        closeConnection(toClose[i]);
}

const ServerConfig& Webserv::getConfigForClient(int clientFd) const {
    std::map<int,int>::const_iterator it = _fdToConfig.find(clientFd);
    if (it != _fdToConfig.end())
        return _configs[static_cast<size_t>(it->second)];
    return _configs[0];
}

void Webserv::run() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    setupServers();
    if (_serverFds.empty()) {
        std::cerr << "No servers started." << std::endl;
        return;
    }

    while (g_running) {
        rebuildPollFds();

        int ret = poll(&_pollfds[0], static_cast<nfds_t>(_pollfds.size()), 5000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) {
            checkTimeouts();
            continue;
        }

        std::vector<pollfd> snapshot = _pollfds;

        for (size_t i = 0; i < snapshot.size(); ++i) {
            if (snapshot[i].revents == 0) continue;
            int fd = snapshot[i].fd;

            if (snapshot[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (!isServerFd(fd)) {
                    if (_cgiToClient.count(fd))
                        handleCgiRead(fd);
                    else
                        closeConnection(fd);
                }
                continue;
            }

            if (snapshot[i].revents & POLLIN) {
                if (isServerFd(fd))
                    acceptConnection(fd);
                else if (_cgiToClient.count(fd))
                    handleCgiRead(fd);
                else
                    handleClientRead(fd);
            } else if (snapshot[i].revents & POLLOUT) {
                if (!isServerFd(fd) && !_cgiToClient.count(fd))
                    handleClientWrite(fd);
            }
        }

        checkTimeouts();
    }
    std::cout << "\nServer shutting down." << std::endl;
}
