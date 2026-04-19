#include "HttpResponse.hpp"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <vector>
#include <limits.h>
#include <cstdlib>

namespace {
bool hasPathPrefix(const std::string& path, const std::string& root) {
    if (root == "/")
        return true;
    if (path.size() < root.size())
        return false;
    if (path.compare(0, root.size(), root) != 0)
        return false;
    return path.size() == root.size() || path[root.size()] == '/';
}

bool resolveRealPath(const std::string& in, std::string& out) {
    char buf[PATH_MAX];
    if (realpath(in.c_str(), buf) == NULL)
        return false;
    out = buf;
    return true;
}

bool isExistingPathUnderRoot(const std::string& root, const std::string& path) {
    std::string realRoot;
    std::string realPath;
    if (!resolveRealPath(root, realRoot) || !resolveRealPath(path, realPath))
        return false;
    return hasPathPrefix(realPath, realRoot);
}

bool isParentUnderRoot(const std::string& root, const std::string& path) {
    size_t slash = path.rfind('/');
    std::string parent = (slash == std::string::npos) ? "." : path.substr(0, slash);
    std::string realRoot;
    std::string realParent;
    if (!resolveRealPath(root, realRoot) || !resolveRealPath(parent, realParent))
        return false;
    return hasPathPrefix(realParent, realRoot);
}

bool normalizeRelativePath(const std::string& relPath, std::string& normalized) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= relPath.size()) {
        size_t j = relPath.find('/', i);
        if (j == std::string::npos)
            j = relPath.size();
        std::string part = relPath.substr(i, j - i);

        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (parts.empty())
                    return false;
                parts.pop_back();
            } else {
                parts.push_back(part);
            }
        }

        i = j + 1;
        if (j == relPath.size())
            break;
    }

    normalized = "/";
    for (size_t k = 0; k < parts.size(); ++k) {
        normalized += parts[k];
        if (k + 1 < parts.size())
            normalized += "/";
    }
    return true;
}

std::string escapeHtml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c == '\'') out += "&#39;";
        else out += c;
    }
    return out;
}
}

HttpResponse::HttpResponse(const HttpRequest& req,
                           const ServerConfig& config,
                           const std::string& clientIP)
    : _req(req), _cfg(config), _clientIP(clientIP),
      _isCgi(false), _cgiHandler(NULL) {}

HttpResponse::~HttpResponse() {
    // CgiHandler ownership transferred to Connection
}

bool        HttpResponse::needsCgi()      const { return _isCgi; }
CgiHandler* HttpResponse::getCgiHandler()       { return _cgiHandler; }

std::string HttpResponse::statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Request Entity Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

std::string HttpResponse::mimeType(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css")  return "text/css";
    if (ext == ".js")   return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".xml")  return "text/xml";
    if (ext == ".txt")  return "text/plain";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png")  return "image/png";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".ico")  return "image/x-icon";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".pdf")  return "application/pdf";
    if (ext == ".py")   return "text/plain";
    if (ext == ".php")  return "text/plain";
    return "application/octet-stream";
}

const LocationConfig* HttpResponse::findLocation(const std::string& path) const {
    const LocationConfig* best = NULL;
    size_t bestLen = 0;
    for (size_t i = 0; i < _cfg.locations.size(); ++i) {
        const LocationConfig& loc = _cfg.locations[i];
        if (path.size() >= loc.path.size()
            && path.substr(0, loc.path.size()) == loc.path
            && (loc.path == "/"
                || path.size() == loc.path.size()
                || path[loc.path.size()] == '/')) {
            if (loc.path.size() >= bestLen) {
                bestLen = loc.path.size();
                best = &_cfg.locations[i];
            }
        }
    }
    return best;
}

bool HttpResponse::methodAllowed(const LocationConfig& loc) const {
    if (loc.methods.empty()) return true;
    const std::string& m = _req.getMethod();
    for (size_t i = 0; i < loc.methods.size(); ++i) {
        if (loc.methods[i] == m) return true;
    }
    return false;
}

std::string HttpResponse::buildResponse(int code,
                                         const std::string& contentType,
                                         const std::string& body,
                                         const std::map<std::string,std::string>& extraHeaders) const {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << code << " " << statusText(code) << "\r\n";

    time_t now = time(NULL);
    char dateBuf[128];
    struct tm* gmt = gmtime(&now);
    if (gmt != NULL)
        strftime(dateBuf, sizeof(dateBuf), "%a, %d %b %Y %H:%M:%S GMT", gmt);
    else
        dateBuf[0] = '\0';
    resp << "Date: " << dateBuf << "\r\n";
    resp << "Server: webserv/1.0\r\n";

    std::string conn = _req.getHeader("connection");
    bool keepAlive = (conn == "keep-alive" || _req.getVersion() == "HTTP/1.1");
    if (conn == "close") keepAlive = false;
    resp << (keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");

    if (!contentType.empty())
        resp << "Content-Type: " << contentType << "\r\n";
    resp << "Content-Length: " << body.size() << "\r\n";

    for (std::map<std::string,std::string>::const_iterator it = extraHeaders.begin();
         it != extraHeaders.end(); ++it) {
        resp << it->first << ": " << it->second << "\r\n";
    }

    resp << "\r\n";
    if (_req.getMethod() != "HEAD")
        resp << body;
    return resp.str();
}

std::string HttpResponse::buildErrorPage(int code) const {
    std::map<int,std::string>::const_iterator it = _cfg.error_pages.find(code);
    if (it != _cfg.error_pages.end()) {
        std::ifstream f(it->second.c_str());
        if (f.is_open()) {
            std::ostringstream oss;
            oss << f.rdbuf();
            std::map<std::string,std::string> extra;
            return buildResponse(code, "text/html", oss.str(), extra);
        }
    }
    std::ostringstream body;
    body << "<!DOCTYPE html><html><head><title>"
         << code << " " << statusText(code)
         << "</title></head><body><h1>"
         << code << " " << statusText(code)
         << "</h1></body></html>";
    std::map<std::string,std::string> extra;
    return buildResponse(code, "text/html", body.str(), extra);
}

std::string HttpResponse::serveStatic(const std::string& filepath, const LocationConfig& loc) {
    (void)loc;
    std::ifstream f(filepath.c_str(), std::ios::binary);
    if (!f.is_open())
        return buildErrorPage(404);
    std::ostringstream oss;
    oss << f.rdbuf();
    std::string body = oss.str();
    std::map<std::string,std::string> extra;
    return buildResponse(200, mimeType(filepath), body, extra);
}

std::string HttpResponse::serveDirectory(const std::string& dirpath,
                                          const std::string& uriPath,
                                          const LocationConfig& loc) {
    std::string idx = loc.index.empty() ? "index.html" : loc.index;
    std::string idxPath = dirpath + "/" + idx;
    struct stat st;
    if (stat(idxPath.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        return serveStatic(idxPath, loc);

    if (!loc.autoindex)
        return buildErrorPage(403);

    DIR* dir = opendir(dirpath.c_str());
    if (!dir) return buildErrorPage(403);

    std::ostringstream html;
        std::string escapedUri = escapeHtml(uriPath);
        html << "<!DOCTYPE html><html><head><title>Index of " << escapedUri << "</title></head>"
            << "<body><h1>Index of " << escapedUri << "</h1><hr><ul>";

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == ".") continue;
        std::string escapedName = escapeHtml(name);
        html << "<li><a href=\"" << uriPath;
        if (uriPath.empty() || uriPath[uriPath.size() - 1] != '/')
            html << "/";
        html << escapedName << "\">" << escapedName << "</a></li>";
    }
    closedir(dir);

    html << "</ul><hr></body></html>";
    std::map<std::string,std::string> extra;
    return buildResponse(200, "text/html", html.str(), extra);
}

std::string HttpResponse::handleDelete(const std::string& filepath) {
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0)
        return buildErrorPage(404);
    if (unlink(filepath.c_str()) != 0)
        return buildErrorPage(403);
    std::map<std::string,std::string> extra;
    return buildResponse(204, "", "", extra);
}

std::string HttpResponse::handlePost(const LocationConfig& loc, const std::string& filepath) {
    if (!loc.upload_dir.empty()) {
        std::string filename = filepath;
        size_t slash = filename.rfind('/');
        if (slash != std::string::npos)
            filename = filename.substr(slash + 1);
        if (filename.empty()) filename = "upload";
        if (filename == "." || filename == ".."
            || filename.find('/') != std::string::npos
            || filename.find('\\') != std::string::npos)
            return buildErrorPage(403);
        std::string dest = loc.upload_dir + "/" + filename;
        if (!isParentUnderRoot(loc.upload_dir, dest))
            return buildErrorPage(403);

        std::ofstream out(dest.c_str(), std::ios::binary);
        if (!out.is_open())
            return buildErrorPage(500);
        out << _req.getBody();
        out.close();
        std::map<std::string,std::string> extra;
        extra["Location"] = "/" + filename;
        return buildResponse(201, "text/html",
            "<html><body>File uploaded successfully.</body></html>", extra);
    }
    std::map<std::string,std::string> extra;
    return buildResponse(200, "text/plain", _req.getBody(), extra);
}

std::string HttpResponse::build() {
    const std::string& method = _req.getMethod();

    if (method != "GET" && method != "POST" && method != "DELETE" && method != "HEAD")
        return buildErrorPage(501);

    const LocationConfig* loc = findLocation(_req.getPath());
    if (!loc)
        return buildErrorPage(404);

    if (!methodAllowed(*loc))
        return buildErrorPage(405);

    // Check body size
    size_t maxBody = loc->client_max_body_size;
    if (maxBody == 0) maxBody = _cfg.client_max_body_size;
    if (maxBody > 0 && _req.getBody().size() > maxBody)
        return buildErrorPage(413);

    // Handle redirect
    if (loc->redirect_code != 0) {
        std::map<std::string,std::string> extra;
        if (!loc->redirect.empty())
            extra["Location"] = loc->redirect;
        return buildResponse(loc->redirect_code, "text/html",
            "<html><body>Redirect</body></html>", extra);
    }

    std::string root = loc->root.empty() ? _cfg.root : loc->root;
    std::string uriPath = _req.getPath();

    std::string relPath = uriPath;
    if (loc->path != "/" && uriPath.size() >= loc->path.size()
        && uriPath.substr(0, loc->path.size()) == loc->path)
        relPath = uriPath.substr(loc->path.size());
    if (relPath.empty() || relPath[0] != '/')
        relPath = "/" + relPath;

    std::string normalizedRel;
    if (!normalizeRelativePath(relPath, normalizedRel))
        return buildErrorPage(403);

    std::string filepath = root + normalizedRel;

    // CGI check
    if (!loc->cgi_ext.empty()) {
        size_t dot = uriPath.rfind('.');
        if (dot != std::string::npos && uriPath.substr(dot) == loc->cgi_ext) {
            if (loc->cgi_pass.empty())
                return buildErrorPage(500);
            struct stat st;
            if (stat(filepath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                if (!isExistingPathUnderRoot(root, filepath))
                    return buildErrorPage(403);
                _cgiHandler = new CgiHandler(_req, *loc, filepath, _clientIP);
                if (_cgiHandler->launch()) {
                    _isCgi = true;
                    return "";
                }
                delete _cgiHandler;
                _cgiHandler = NULL;
                return buildErrorPage(500);
            }
        }
    }

    if (method == "DELETE") {
        return handleDelete(filepath);
    }

    if (method == "POST")
        return handlePost(*loc, filepath);

    // GET / HEAD
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0)
        return buildErrorPage(404);
    if (!isExistingPathUnderRoot(root, filepath))
        return buildErrorPage(403);

    if (S_ISDIR(st.st_mode))
        return serveDirectory(filepath, uriPath, *loc);

    return serveStatic(filepath, *loc);
}
