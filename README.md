<div align="center">

# 🌐 webserv

**A lightweight, RFC-compliant HTTP/1.1 web server written in C++98**

*42 School — Webserv project*

![Language](https://img.shields.io/badge/language-C%2B%2B98-blue)
![Standard](https://img.shields.io/badge/standard-HTTP%2F1.1-green)
![Build](https://img.shields.io/badge/build-make-orange)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

</div>

---

## 📖 Table of Contents

- [About](#-about)
- [Features](#-features)
- [Architecture](#-architecture)
- [Requirements](#-requirements)
- [Getting Started](#-getting-started)
- [Configuration](#-configuration)
- [Supported Directives](#-supported-directives)
- [CGI Support](#-cgi-support)
- [Project Structure](#-project-structure)
- [Authors](#-authors)

---

## 📌 About

`webserv` is a fully functional HTTP/1.1 web server built from scratch in C++98 as part of the 42 school curriculum. It handles concurrent connections via a single-threaded `poll()`-based event loop, parses an Nginx-inspired configuration file, and supports virtual hosting, CGI execution, file uploads, directory listing, and custom error pages.

---

## ✨ Features

| Feature | Description |
|---|---|
| **HTTP/1.1** | GET, POST, and DELETE methods |
| **Non-blocking I/O** | Single-threaded event loop using `poll()` |
| **Virtual hosting** | Multiple server blocks on the same port, dispatched by `Host` header |
| **CGI** | Execute scripts (Python, etc.) via the CGI/1.1 interface |
| **File uploads** | Receive and store multipart or raw uploads |
| **Autoindex** | Auto-generated directory listings |
| **Redirects** | Configurable HTTP redirects (e.g. 301) |
| **Custom error pages** | Per-status-code HTML error pages |
| **Chunked transfer** | Handles chunked request bodies |
| **Configurable body size** | Per-server and per-location `client_max_body_size` |
| **Multiple ports** | Listen on several ports simultaneously |

---

## 🏗 Architecture

```
┌──────────────┐       ┌──────────────────┐       ┌──────────────┐
│  Config File │──────▶│  ConfigParser    │──────▶│ ServerConfig │
└──────────────┘       └──────────────────┘       └──────┬───────┘
                                                          │
                                                   ┌──────▼───────┐
                                                   │   Webserv    │  ← poll() event loop
                                                   └──────┬───────┘
                       ┌──────────────┐                   │
                       │  CgiHandler  │◀──────────────────┤
                       └──────────────┘                   │
                       ┌──────────────┐                   │
                       │  Connection  │◀──────────────────┤
                       └──────┬───────┘                   │
                ┌─────────────┼─────────────┐             │
         ┌──────▼──────┐ ┌────▼─────────┐  └─────────────┘
         │ HttpRequest │ │ HttpResponse │
         └─────────────┘ └─────────────┘
```

- **`Webserv`** — Core event loop. Manages listening sockets, accepts new connections, and dispatches read/write events via `poll()`.
- **`ConfigParser`** — Parses the Nginx-style configuration file into `ServerConfig`/`LocationConfig` structures.
- **`Connection`** — Tracks per-client state: partial reads, request parsing, response buffering, and CGI lifecycle.
- **`HttpRequest`** — State-machine parser for HTTP/1.1 request lines, headers, and bodies (including chunked encoding).
- **`HttpResponse`** — Builds and serialises HTTP responses, serves static files, handles uploads, and constructs autoindex pages.
- **`CgiHandler`** — Forks a child process, sets up the CGI environment, and pipes data between the server and the script.

---

## ⚙️ Requirements

- **OS**: Linux or macOS
- **Compiler**: `g++` or `clang++` with C++98 support
- **Build tool**: `make`
- **Python 3** *(optional — required only for Python CGI scripts)*

---

## 🚀 Getting Started

### 1. Clone the repository

```bash
git clone https://github.com/ridahafidi/Webserver.git
cd Webserver
```

### 2. Build

```bash
make
```

This produces the `webserv` binary in the project root.

### 3. Run

```bash
# Using the default configuration
./webserv config/default.conf

# Using a custom configuration
./webserv path/to/your.conf
```

### 4. Test

Open your browser or use `curl`:

```bash
# Basic GET request
curl http://localhost:8080/

# Upload a file
curl -X POST --data-binary @myfile.txt http://localhost:8080/upload

# Delete a resource
curl -X DELETE http://localhost:8080/upload/myfile.txt
```

### Makefile targets

| Target | Description |
|---|---|
| `make` / `make all` | Build the binary |
| `make clean` | Remove object files |
| `make fclean` | Remove object files and the binary |
| `make re` | Full rebuild (`fclean` + `all`) |

---

## 🔧 Configuration

The configuration syntax is inspired by Nginx. You can define one or more `server` blocks, each with its own `location` blocks.

**Minimal example:**

```nginx
server {
    listen       8080;
    server_name  localhost;
    root         ./www/html;
    index        index.html;
    client_max_body_size 10M;

    location / {
        allow_methods GET POST DELETE;
        autoindex off;
    }
}
```

**Full example** — see [`config/default.conf`](config/default.conf).

---

## 📋 Supported Directives

### Server block

| Directive | Description | Example |
|---|---|---|
| `listen` | Port to listen on | `listen 8080;` |
| `server_name` | Virtual host name(s) | `server_name localhost api.local;` |
| `root` | Document root | `root ./www/html;` |
| `index` | Default index file | `index index.html;` |
| `client_max_body_size` | Max request body size | `client_max_body_size 10M;` |
| `error_page` | Custom error page | `error_page 404 ./www/errors/404.html;` |

### Location block

| Directive | Description | Example |
|---|---|---|
| `allow_methods` | Permitted HTTP methods | `allow_methods GET POST;` |
| `root` | Override document root | `root ./www/static;` |
| `autoindex` | Enable directory listing | `autoindex on;` |
| `return` | HTTP redirect | `return 301 /new-path;` |
| `upload_dir` | Directory for uploaded files | `upload_dir ./www/uploads;` |
| `client_max_body_size` | Override body size limit | `client_max_body_size 50M;` |
| `cgi_pass` | CGI interpreter path | `cgi_pass /usr/bin/python3;` |
| `cgi_ext` | Script file extension | `cgi_ext .py;` |

---

## 🐍 CGI Support

Scripts placed under the configured CGI location are executed by the server. Standard CGI/1.1 environment variables are set automatically (`REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_LENGTH`, `REMOTE_ADDR`, etc.).

**Configuration example:**

```nginx
location /cgi-bin {
    allow_methods GET POST;
    cgi_pass      /usr/bin/python3;
    cgi_ext       .py;
    root          ./www/cgi-bin;
}
```

**Minimal Python script (`www/cgi-bin/hello.py`):**

```python
#!/usr/bin/env python3
print("Content-Type: text/html\r\n\r\n")
print("<h1>Hello from CGI!</h1>")
```

Access it at: `http://localhost:8080/cgi-bin/hello.py`

---

## 📁 Project Structure

```
.
├── Makefile
├── config/
│   └── default.conf          # Default server configuration
├── include/
│   ├── CgiHandler.hpp
│   ├── ConfigParser.hpp
│   ├── Connection.hpp
│   ├── HttpRequest.hpp
│   ├── HttpResponse.hpp
│   ├── ServerConfig.hpp
│   └── Webserv.hpp
├── src/
│   ├── main.cpp              # Entry point
│   ├── CgiHandler.cpp        # CGI process management
│   ├── ConfigParser.cpp      # Configuration file parser
│   ├── Connection.cpp        # Per-client connection state
│   ├── HttpRequest.cpp       # HTTP request parser
│   ├── HttpResponse.cpp      # HTTP response builder
│   └── Webserv.cpp           # Core event loop (poll)
└── www/
    ├── api/                  # Virtual host root (api.local)
    ├── cgi-bin/              # CGI scripts
    ├── errors/               # Custom error pages
    ├── html/                 # Default document root
    └── uploads/              # File upload destination
```

---

## 👤 Authors

- **ridahafidi** — [github.com/ridahafidi](https://github.com/ridahafidi)

---

<div align="center">
  <sub>Built as part of the 42 school curriculum · C++98 · HTTP/1.1</sub>
</div>