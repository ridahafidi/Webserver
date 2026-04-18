#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include "ServerConfig.hpp"
#include "HttpRequest.hpp"
#include "CgiHandler.hpp"
#include <string>
#include <map>

class HttpResponse {
public:
    HttpResponse(const HttpRequest& req,
                 const ServerConfig& config,
                 const std::string& clientIP);
    ~HttpResponse();

    std::string build();
    bool        needsCgi() const;
    CgiHandler* getCgiHandler();

    static std::string statusText(int code);
    static std::string mimeType(const std::string& path);

private:
    const HttpRequest&  _req;
    const ServerConfig& _cfg;
    std::string         _clientIP;
    bool                _isCgi;
    CgiHandler*         _cgiHandler;

    const LocationConfig* findLocation(const std::string& path) const;
    std::string serveStatic(const std::string& filepath, const LocationConfig& loc);
    std::string serveDirectory(const std::string& dirpath, const std::string& uriPath, const LocationConfig& loc);
    std::string buildErrorPage(int code) const;
    std::string buildResponse(int code,
                              const std::string& contentType,
                              const std::string& body,
                              const std::map<std::string,std::string>& extraHeaders) const;
    bool methodAllowed(const LocationConfig& loc) const;
    std::string handleDelete(const std::string& filepath);
    std::string handlePost(const LocationConfig& loc, const std::string& filepath);
};

#endif
