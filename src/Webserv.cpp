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
#include <sstream>
#include <sys/wait.h>

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

// Group configs by "host:port" so that virtual-hosted server blocks (same
// port, different server_name) share a single listening socket.
void Webserv::setupServers() {
    // ordered list of (host:port key -> vector of config indices)
    std::vector<std::pair<std::string, std::vector<int> > > groups;

    for (size_t i = 0; i < _configs.size(); ++i) {
        std::ostringstream key;
        key << _configs[i].host << ":" << _configs[i].port;
        std::string k = key.str();

        bool found = false;
        for (size_t g = 0; g < groups.size(); ++g) {
            if (groups[g].first == k) {
                groups[g].second.push_back(static_cast<int>(i));
                found = true;
                break;
            }
        }
        if (!found) {
            std::vector<int> v;
            v.push_back(static_cast<int>(i));
            groups.push_back(std::make_pair(k, v));
        }
    }

    for (size_t g = 0; g < groups.size(); ++g) {
        const std::vector<int>& indices = groups[g].second;
        const ServerConfig& primary = _configs[static_cast<size_t>(indices[0])];

        int fd = createServerSocket(primary);
        if (fd < 0) {
            std::cerr << "Failed to create server socket for "
                      << primary.host << ":" << primary.port << std::endl;
            continue;
        }
        _serverFds.push_back(fd);
        _serverFdToConfigs[fd] = indices;

        std::cout << "Listening on " << primary.host
                  << ":" << primary.port;
        if (indices.size() > 1)
            std::cout << " (" << indices.size() << " virtual hosts)";
        std::cout << std::endl;
    }
}

int Webserv::createServerSocket(const ServerConfig& cfg) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(fd); return -1;
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd); return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(cfg.port));
    if (cfg.host == "0.0.0.0" || cfg.host.empty())
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else
        inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr);

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed for " << cfg.host << ":" << cfg.port
                  << " – " << strerror(errno) << std::endl;
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
        pfd.fd      = _serverFds[i];
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
            pfd.fd      = conn->getFd();
            pfd.events  = POLLIN;
            pfd.revents = 0;
            _pollfds.push_back(pfd);
        } else if (state == CONN_WRITING) {
            pollfd pfd;
            pfd.fd      = conn->getFd();
            pfd.events  = POLLOUT;
            pfd.revents = 0;
            _pollfds.push_back(pfd);
        } else if (state == CONN_CGI_WAIT) {
            int cgiFd = conn->getCgiFd();
            if (cgiFd >= 0) {
                pollfd pfd;
                pfd.fd      = cgiFd;
                pfd.events  = POLLIN;
                pfd.revents = 0;
                _pollfds.push_back(pfd);
                _cgiToClient[cgiFd] = conn->getFd();
            } else {
                // CGI fd already closed – collect remaining output
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

    const std::vector<int>& cfgIndices = _serverFdToConfigs[serverFd];
    Connection* conn = new Connection(clientFd, clientIP, _configs, cfgIndices);
    _connections[clientFd] = conn;
    _fdToConfig[clientFd]  = cfgIndices[0];
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
        Connection* conn = it->second;

        // Kill idle connections that have been silent for 60 seconds
        if (now - conn->getLastActivity() > 60) {
            toClose.push_back(it->first);
            continue;
        }

        // Kill CGI scripts that have been running for more than 10 seconds
        if (conn->getState() == CONN_CGI_WAIT
                && conn->getCgiStartTime() > 0
                && now - conn->getCgiStartTime() > 10) {
            toClose.push_back(it->first);
        }
    }

    for (size_t i = 0; i < toClose.size(); ++i)
        closeConnection(toClose[i]);
}

void Webserv::run() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);
    // Reap zombie CGI children automatically
    signal(SIGCHLD, SIG_IGN); // auto-reap CGI children, no zombies

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
