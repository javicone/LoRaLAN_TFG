#include "http_client.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstring>
#include <cstdio>
#include <cerrno>

namespace net {

namespace {

// Escribe el buffer completo: send() puede consumir menos de lo pedido.
bool sendAll(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

// Lee hasta que el otro extremo cierra o vence el timeout del socket.
std::string recvAll(int fd) {
    std::string out;
    char buf[2048];
    for (;;) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            out.append(buf, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;   // 0 = cierre limpio; <0 = error o timeout
    }
    return out;
}

} // namespace

HttpClient::HttpClient(std::string host, uint16_t port, int timeoutMs)
    : host_(std::move(host)), port_(port), timeoutMs_(timeoutMs) {}

void HttpClient::setBearer(std::string token) { bearer_ = std::move(token); }

Response HttpClient::post(const std::string& path, const std::string& jsonBody) {
    return request("POST", path, jsonBody);
}

Response HttpClient::get(const std::string& path) {
    return request("GET", path, std::string());
}

Response HttpClient::request(const char* method, const std::string& path, const std::string& body) {
    Response res;

    // --- Resolucion de nombre ---
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;      // IPv4 o IPv6
    hints.ai_socktype = SOCK_STREAM;

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", (unsigned)port_);

    struct addrinfo* info = nullptr;
    int rc = ::getaddrinfo(host_.c_str(), portStr, &hints, &info);
    if (rc != 0) {
        res.error = std::string("getaddrinfo: ") + gai_strerror(rc);
        return res;
    }

    // --- Conexion (se prueban todas las direcciones devueltas) ---
    int fd = -1;
    for (struct addrinfo* ai = info; ai != nullptr; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv;
        tv.tv_sec  = timeoutMs_ / 1000;
        tv.tv_usec = (timeoutMs_ % 1000) * 1000;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;

        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(info);

    if (fd < 0) {
        res.error = "no se pudo conectar con " + host_ + ":" + portStr;
        return res;
    }

    // --- Peticion ---
    std::string req;
    req.reserve(512 + body.size());
    req += method; req += " "; req += path; req += " HTTP/1.1\r\n";
    req += "Host: " + host_ + ":" + portStr + "\r\n";
    req += "User-Agent: gatewayLoRaLAN/1.0\r\n";
    req += "Connection: close\r\n";
    if (!bearer_.empty()) {
        req += "Authorization: Bearer " + bearer_ + "\r\n";
    }
    if (!body.empty()) {
        req += "Content-Type: application/json\r\n";
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    req += "\r\n";
    req += body;

    if (!sendAll(fd, req.data(), req.size())) {
        res.error = "fallo al enviar la peticion";
        ::close(fd);
        return res;
    }

    const std::string raw = recvAll(fd);
    ::close(fd);

    if (raw.empty()) {
        res.error = "sin respuesta (timeout o cierre del servidor)";
        return res;
    }

    // --- Respuesta: linea de estado y separacion de cuerpo ---
    if (raw.compare(0, 5, "HTTP/") != 0) {
        res.error = "respuesta no HTTP";
        return res;
    }
    const size_t sp = raw.find(' ');
    if (sp == std::string::npos) {
        res.error = "linea de estado malformada";
        return res;
    }
    res.status = std::atoi(raw.c_str() + sp + 1);

    const size_t sep = raw.find("\r\n\r\n");
    res.body = (sep == std::string::npos) ? std::string() : raw.substr(sep + 4);
    res.ok = true;
    return res;
}

} // namespace net
