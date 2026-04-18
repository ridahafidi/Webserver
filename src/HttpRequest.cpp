#include "HttpRequest.hpp"
#include <sstream>
#include <cctype>
#include <cstdlib>

HttpRequest::HttpRequest()
    : _state(REQ_REQUEST_LINE), _contentLength(0), _chunked(false) {}

void HttpRequest::reset() {
    _state = REQ_REQUEST_LINE;
    _raw.clear();
    _method.clear();
    _uri.clear();
    _path.clear();
    _query.clear();
    _version.clear();
    _headers.clear();
    _body.clear();
    _contentLength = 0;
    _chunked = false;
}

bool HttpRequest::isComplete() const { return _state == REQ_COMPLETE; }
bool HttpRequest::hasError()   const { return _state == REQ_ERROR; }

const std::string& HttpRequest::getMethod()  const { return _method; }
const std::string& HttpRequest::getUri()     const { return _uri; }
const std::string& HttpRequest::getVersion() const { return _version; }
const std::string& HttpRequest::getBody()    const { return _body; }
const std::string& HttpRequest::getPath()    const { return _path; }
const std::string& HttpRequest::getQuery()   const { return _query; }
const std::map<std::string,std::string>& HttpRequest::getHeaders() const { return _headers; }

const std::string& HttpRequest::getHeader(const std::string& name) const {
    std::string lower = name;
    for (size_t i = 0; i < lower.size(); ++i)
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
    std::map<std::string,std::string>::const_iterator it = _headers.find(lower);
    if (it != _headers.end()) return it->second;
    return _empty;
}

size_t HttpRequest::getContentLength() const { return _contentLength; }

bool HttpRequest::parseLine(std::string& line) {
    size_t pos = _raw.find("\r\n");
    if (pos == std::string::npos) return false;
    line = _raw.substr(0, pos);
    _raw = _raw.substr(pos + 2);
    return true;
}

bool HttpRequest::parseRequestLine(const std::string& line) {
    std::istringstream iss(line);
    if (!(iss >> _method >> _uri >> _version))
        return false;
    size_t q = _uri.find('?');
    if (q != std::string::npos) {
        _path = _uri.substr(0, q);
        _query = _uri.substr(q + 1);
    } else {
        _path = _uri;
        _query = "";
    }
    return true;
}

bool HttpRequest::parseHeaderLine(const std::string& line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    std::string key = line.substr(0, colon);
    std::string val = line.substr(colon + 1);
    for (size_t i = 0; i < key.size(); ++i)
        key[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(key[i])));
    size_t start = val.find_first_not_of(" \t");
    if (start != std::string::npos) val = val.substr(start);
    size_t end = val.find_last_not_of(" \t\r\n");
    if (end != std::string::npos) val = val.substr(0, end + 1);
    _headers[key] = val;
    return true;
}

bool HttpRequest::parseChunkedBody() {
    while (true) {
        size_t pos = _raw.find("\r\n");
        if (pos == std::string::npos) return false;

        std::string sizeLine = _raw.substr(0, pos);
        size_t semi = sizeLine.find(';');
        if (semi != std::string::npos) sizeLine = sizeLine.substr(0, semi);

        size_t chunkSize = 0;
        std::istringstream iss(sizeLine);
        iss >> std::hex >> chunkSize;

        if (chunkSize == 0) {
            _state = REQ_COMPLETE;
            return true;
        }
        if (_raw.size() < pos + 2 + chunkSize + 2)
            return false;

        _body += _raw.substr(pos + 2, chunkSize);
        _raw = _raw.substr(pos + 2 + chunkSize + 2);
    }
}

bool HttpRequest::feed(const char* data, size_t len) {
    _raw.append(data, len);

    while (_state != REQ_COMPLETE && _state != REQ_ERROR) {
        if (_state == REQ_REQUEST_LINE) {
            std::string line;
            if (!parseLine(line)) break;
            if (!parseRequestLine(line)) {
                _state = REQ_ERROR;
                return false;
            }
            _state = REQ_HEADERS;
        } else if (_state == REQ_HEADERS) {
            std::string line;
            if (!parseLine(line)) break;
            if (line.empty()) {
                std::string cl = getHeader("content-length");
                if (!cl.empty())
                    _contentLength = static_cast<size_t>(std::atol(cl.c_str()));
                std::string te = getHeader("transfer-encoding");
                if (te.find("chunked") != std::string::npos)
                    _chunked = true;
                if (_chunked || _contentLength > 0)
                    _state = REQ_BODY;
                else
                    _state = REQ_COMPLETE;
            } else {
                parseHeaderLine(line);
            }
        } else if (_state == REQ_BODY) {
            if (_chunked) {
                if (!parseChunkedBody()) break;
            } else {
                if (_raw.size() >= _contentLength) {
                    _body = _raw.substr(0, _contentLength);
                    _raw = _raw.substr(_contentLength);
                    _state = REQ_COMPLETE;
                } else {
                    break;
                }
            }
        }
    }
    return true;
}
