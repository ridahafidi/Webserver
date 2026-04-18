#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "HttpRequest.hpp"
#include "CgiHandler.hpp"
#include "ServerConfig.hpp"
#include <string>
#include <ctime>
#include <vector>

enum ConnectionState {
    CONN_READING,
    CONN_CGI_WAIT,
    CONN_WRITING,
    CONN_CLOSE
};

class Connection {
public:
    Connection(int fd, const std::string& clientIP,
               const std::vector<ServerConfig>& allConfigs,
               const std::vector<int>& cfgIndices);
    ~Connection();

    int  getFd() const;
    int  getCgiFd() const;
    const std::string& getClientIP() const;

    ConnectionState getState() const;
    void setState(ConnectionState s);

    bool onReadable();
    bool onWritable();
    bool onCgiReadable();

    bool   shouldClose()     const;
    time_t getLastActivity() const;
    time_t getCgiStartTime() const;

    const ServerConfig& getConfig() const;

    HttpRequest& getRequest();
    CgiHandler*  getCgiHandler();
    void         setCgiHandler(CgiHandler* h);

private:
    int                              _fd;
    std::string                      _clientIP;
    const std::vector<ServerConfig>* _allConfigs;
    std::vector<int>                 _cfgIndices;
    const ServerConfig*              _cfg;
    ConnectionState                  _state;
    HttpRequest                      _request;
    std::string                      _writeBuffer;
    CgiHandler*                      _cgi;
    time_t                           _lastActivity;
    time_t                           _cgiStartTime;
    bool                             _keepAlive;

    void selectConfig();
    void processRequest();
};

#endif
