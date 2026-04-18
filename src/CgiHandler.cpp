#include "CgiHandler.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <cctype>

CgiHandler::CgiHandler(const HttpRequest& req,
                       const LocationConfig& loc,
                       const std::string& scriptPath,
                       const std::string& clientIP)
    : _req(req), _loc(loc), _scriptPath(scriptPath), _clientIP(clientIP),
      _pid(-1), _readFd(-1), _writeFd(-1), _finished(false), _error(false) {}

CgiHandler::~CgiHandler() {
    if (_readFd >= 0) { close(_readFd); _readFd = -1; }
    if (_writeFd >= 0) { close(_writeFd); _writeFd = -1; }
    if (_pid > 0) {
        int status;
        waitpid(_pid, &status, WNOHANG);
    }
}

int   CgiHandler::getReadFd()  const { return _readFd; }
int   CgiHandler::getWriteFd() const { return _writeFd; }
pid_t CgiHandler::getPid()     const { return _pid; }
bool  CgiHandler::hasError()   const { return _error; }
const std::string& CgiHandler::getOutput() const { return _output; }

std::vector<std::string> CgiHandler::buildEnv() const {
    std::vector<std::string> env;

    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("SERVER_PROTOCOL=HTTP/1.1");
    env.push_back("REDIRECT_STATUS=200");
    env.push_back("REQUEST_METHOD=" + _req.getMethod());
    env.push_back("SCRIPT_NAME=" + _req.getPath());
    env.push_back("SCRIPT_FILENAME=" + _scriptPath);
    env.push_back("PATH_INFO=" + _req.getPath());
    env.push_back("QUERY_STRING=" + _req.getQuery());

    std::string host = _req.getHeader("host");
    if (host.empty()) host = "localhost";
    std::string serverName = host;
    std::string serverPort = "80";
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        serverName = host.substr(0, colon);
        serverPort = host.substr(colon + 1);
    }
    env.push_back("SERVER_NAME=" + serverName);
    env.push_back("SERVER_PORT=" + serverPort);
    env.push_back("SERVER_SOFTWARE=webserv/1.0");
    env.push_back("REMOTE_ADDR=" + _clientIP);

    std::string ct = _req.getHeader("content-type");
    if (!ct.empty()) env.push_back("CONTENT_TYPE=" + ct);

    std::ostringstream oss;
    oss << _req.getBody().size();
    env.push_back("CONTENT_LENGTH=" + oss.str());

    const std::map<std::string,std::string>& hdrs = _req.getHeaders();
    for (std::map<std::string,std::string>::const_iterator it = hdrs.begin();
         it != hdrs.end(); ++it) {
        std::string key = "HTTP_";
        for (size_t i = 0; i < it->first.size(); ++i) {
            char c = it->first[i];
            if (c == '-') key += '_';
            else key += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        env.push_back(key + "=" + it->second);
    }

    return env;
}

bool CgiHandler::launch() {
    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0) {
        _error = true;
        return false;
    }
    if (pipe(stdout_pipe) < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        _error = true;
        return false;
    }

    std::vector<std::string> envVec = buildEnv();
    std::vector<char*> envp;
    for (size_t i = 0; i < envVec.size(); ++i)
        envp.push_back(const_cast<char*>(envVec[i].c_str()));
    envp.push_back(NULL);

    std::vector<char*> args;
    if (!_loc.cgi_pass.empty())
        args.push_back(const_cast<char*>(_loc.cgi_pass.c_str()));
    args.push_back(const_cast<char*>(_scriptPath.c_str()));
    args.push_back(NULL);

    _pid = fork();
    if (_pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        _error = true;
        return false;
    }

    if (_pid == 0) {
        // child
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        std::string dir = _scriptPath;
        size_t slash = dir.rfind('/');
        if (slash != std::string::npos)
            chdir(dir.substr(0, slash).c_str());

        execve(args[0], &args[0], &envp[0]);
        _exit(1);
    }

    // parent
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    _readFd  = stdout_pipe[0];
    _writeFd = stdin_pipe[1];

    fcntl(_readFd, F_SETFL, O_NONBLOCK);

    const std::string& body = _req.getBody();
    if (!body.empty()) {
        ssize_t written = write(_writeFd, body.c_str(), body.size());
        (void)written;
    }
    close(_writeFd);
    _writeFd = -1;

    return true;
}

std::string CgiHandler::readOutput() {
    char buf[4096];
    ssize_t n;
    while ((n = read(_readFd, buf, sizeof(buf))) > 0) {
        _output.append(buf, static_cast<size_t>(n));
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(_readFd);
        _readFd = -1;
        _finished = true;
    }
    return _output;
}

bool CgiHandler::isFinished() {
    if (_finished) return true;
    if (_pid > 0) {
        int status;
        pid_t result = waitpid(_pid, &status, WNOHANG);
        if (result == _pid) {
            _pid = -1;
            if (_readFd >= 0) {
                char buf[4096];
                ssize_t n;
                while ((n = read(_readFd, buf, sizeof(buf))) > 0)
                    _output.append(buf, static_cast<size_t>(n));
                close(_readFd);
                _readFd = -1;
            }
            _finished = true;
        }
    }
    return _finished;
}
