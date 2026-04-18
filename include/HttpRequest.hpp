#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include <map>
#include <cstddef>

enum RequestState {
    REQ_REQUEST_LINE,
    REQ_HEADERS,
    REQ_BODY,
    REQ_COMPLETE,
    REQ_ERROR
};

class HttpRequest {
public:
    HttpRequest();
    void reset();

    bool feed(const char* data, size_t len);

    bool isComplete() const;
    bool hasError() const;

    const std::string& getMethod()  const;
    const std::string& getUri()     const;
    const std::string& getVersion() const;
    const std::string& getHeader(const std::string& name) const;
    const std::map<std::string,std::string>& getHeaders() const;
    const std::string& getBody()    const;
    const std::string& getPath()    const;
    const std::string& getQuery()   const;

    size_t getContentLength() const;

private:
    RequestState _state;
    std::string  _raw;
    std::string  _method;
    std::string  _uri;
    std::string  _path;
    std::string  _query;
    std::string  _version;
    std::map<std::string,std::string> _headers;
    std::string  _body;
    size_t       _contentLength;
    bool         _chunked;
    std::string  _empty;

    bool parseRequestLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    bool parseLine(std::string& line);
    bool parseChunkedBody();
};

#endif
