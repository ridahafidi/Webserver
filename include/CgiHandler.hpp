#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "HttpRequest.hpp"
#include "ServerConfig.hpp"
#include <string>
#include <vector>
#include <sys/types.h>

class CgiHandler {
public:
    CgiHandler(const HttpRequest& req,
               const LocationConfig& loc,
               const std::string& scriptPath,
               const std::string& clientIP);
    ~CgiHandler();

    bool launch();
    int  getReadFd() const;
    int  getWriteFd() const;
    pid_t getPid() const;

    std::string readOutput();
    bool isFinished();
    bool hasError() const;

    const std::string& getOutput() const;

private:
    const HttpRequest&    _req;
    const LocationConfig& _loc;
    std::string           _scriptPath;
    std::string           _clientIP;

    pid_t  _pid;
    int    _readFd;
    int    _writeFd;
    bool   _finished;
    bool   _error;
    std::string _output;

    std::vector<std::string> buildEnv() const;
};

#endif
