#include "Connection.hpp"
#include "HttpResponse.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <sstream>
#include <map>
#include <cctype>
#include <cstdlib>

Connection::Connection(int fd, const std::string& clientIP,
                       const std::vector<ServerConfig>& allConfigs,
                       const std::vector<int>& cfgIndices)
    : _fd(fd), _clientIP(clientIP),
      _allConfigs(&allConfigs), _cfgIndices(cfgIndices),
      _cfg(&allConfigs[static_cast<size_t>(cfgIndices[0])]),
      _state(CONN_READING), _cgi(NULL),
      _lastActivity(time(NULL)), _cgiStartTime(0), _keepAlive(true) {}

Connection::~Connection() {
    if (_cgi) { delete _cgi; _cgi = NULL; }
    if (_fd >= 0) { close(_fd); _fd = -1; }
}

int                 Connection::getFd()           const { return _fd; }
const std::string&  Connection::getClientIP()     const { return _clientIP; }
ConnectionState     Connection::getState()        const { return _state; }
void                Connection::setState(ConnectionState s) { _state = s; }
time_t              Connection::getLastActivity() const { return _lastActivity; }
time_t              Connection::getCgiStartTime() const { return _cgiStartTime; }
const ServerConfig& Connection::getConfig()       const { return *_cfg; }
HttpRequest&        Connection::getRequest()            { return _request; }
CgiHandler*         Connection::getCgiHandler()         { return _cgi; }
void                Connection::setCgiHandler(CgiHandler* h) { _cgi = h; }

int Connection::getCgiFd() const {
    if (_cgi) return _cgi->getReadFd();
    return -1;
}

bool Connection::shouldClose() const {
    return _state == CONN_CLOSE;
}

// Pick the ServerConfig whose server_name best matches the request's Host header.
// Falls back to the first config (the default) when no name matches.
void Connection::selectConfig() {
    std::string host = _request.getHeader("host");
    size_t colon = host.find(':');
    if (colon != std::string::npos)
        host = host.substr(0, colon);

    for (size_t i = 0; i < _cfgIndices.size(); ++i) {
        const ServerConfig& cfg =
            (*_allConfigs)[static_cast<size_t>(_cfgIndices[i])];
        for (size_t j = 0; j < cfg.server_names.size(); ++j) {
            if (cfg.server_names[j] == host) {
                _cfg = &cfg;
                return;
            }
        }
    }
    _cfg = &(*_allConfigs)[static_cast<size_t>(_cfgIndices[0])];
}

bool Connection::onReadable() {
    char buf[8192];
    ssize_t n = recv(_fd, buf, sizeof(buf), 0);
    if (n <= 0) {
        _state = CONN_CLOSE;
        return false;
    }
    _lastActivity = time(NULL);
    _request.feed(buf, static_cast<size_t>(n));

    if (_request.hasError()) {
        std::string body = "<html><body><h1>400 Bad Request</h1></body></html>";
        std::ostringstream r;
        r << "HTTP/1.1 400 Bad Request\r\n";
        r << "Content-Type: text/html\r\n";
        r << "Content-Length: " << body.size() << "\r\n";
        r << "Connection: close\r\n\r\n";
        r << body;
        _writeBuffer = r.str();
        _state = CONN_WRITING;
        _keepAlive = false;
        return true;
    }

    if (_request.isComplete()) {
        std::string conn = _request.getHeader("connection");
        if (conn == "close" || _request.getVersion() == "HTTP/1.0")
            _keepAlive = false;
        processRequest();
    }
    return true;
}

void Connection::processRequest() {
    selectConfig();
    HttpResponse resp(_request, *_cfg, _clientIP);
    std::string response = resp.build();

    if (resp.needsCgi()) {
        _cgi = resp.getCgiHandler();
        _cgiStartTime = time(NULL);
        _state = CONN_CGI_WAIT;
    } else {
        _writeBuffer = response;
        _state = CONN_WRITING;
    }
}

bool Connection::onWritable() {
    if (_writeBuffer.empty()) {
        if (_keepAlive) {
            _request.reset();
            _state = CONN_READING;
        } else {
            _state = CONN_CLOSE;
        }
        return true;
    }

    ssize_t n = send(_fd, _writeBuffer.c_str(), _writeBuffer.size(), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        _state = CONN_CLOSE;
        return false;
    }
    _writeBuffer = _writeBuffer.substr(static_cast<size_t>(n));
    _lastActivity = time(NULL);

    if (_writeBuffer.empty()) {
        if (_keepAlive) {
            _request.reset();
            _state = CONN_READING;
        } else {
            _state = CONN_CLOSE;
        }
    }
    return true;
}

bool Connection::onCgiReadable() {
    if (!_cgi) {
        _state = CONN_CLOSE;
        return false;
    }

    _cgi->readOutput();

    if (_cgi->isFinished() || _cgi->hasError()) {
        std::string cgiOut = _cgi->getOutput();

        std::string cgiHeaders;
        std::string cgiBody;
        size_t sep = cgiOut.find("\r\n\r\n");
        if (sep != std::string::npos) {
            cgiHeaders = cgiOut.substr(0, sep);
            cgiBody    = cgiOut.substr(sep + 4);
        } else {
            sep = cgiOut.find("\n\n");
            if (sep != std::string::npos) {
                cgiHeaders = cgiOut.substr(0, sep);
                cgiBody    = cgiOut.substr(sep + 2);
            } else {
                cgiBody = cgiOut;
            }
        }

        int statusCode = 200;
        std::string contentType = "text/html";
        std::map<std::string,std::string> extraHeaders;

        std::istringstream iss(cgiHeaders);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line[line.size() - 1] == '\r')
                line = line.substr(0, line.size() - 1);
            if (line.empty()) continue;
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            size_t start = val.find_first_not_of(" \t");
            if (start != std::string::npos) val = val.substr(start);
            std::string keyLow = key;
            for (size_t i = 0; i < keyLow.size(); ++i)
                keyLow[i] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(keyLow[i])));
            if (keyLow == "status") {
                char* endptr;
                long parsed = std::strtol(val.c_str(), &endptr, 10);
                if (endptr != val.c_str() && parsed >= 100 && parsed <= 599)
                    statusCode = static_cast<int>(parsed);
            } else if (keyLow == "content-type") {
                contentType = val;
            } else {
                extraHeaders[key] = val;
            }
        }

        std::ostringstream resp;
        resp << "HTTP/1.1 " << statusCode << " "
             << HttpResponse::statusText(statusCode) << "\r\n";
        resp << "Server: webserv/1.0\r\n";
        resp << "Content-Type: " << contentType << "\r\n";
        resp << "Content-Length: " << cgiBody.size() << "\r\n";
        resp << (_keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");
        for (std::map<std::string,std::string>::iterator it = extraHeaders.begin();
             it != extraHeaders.end(); ++it) {
            resp << it->first << ": " << it->second << "\r\n";
        }
        resp << "\r\n" << cgiBody;

        _writeBuffer = resp.str();

        delete _cgi;
        _cgi = NULL;
        _cgiStartTime = 0;
        _state = CONN_WRITING;
        _lastActivity = time(NULL);
    }
    return true;
}
