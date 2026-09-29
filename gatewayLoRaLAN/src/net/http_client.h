#pragma once
// Cliente HTTP minimo sobre sockets POSIX.
//
// El gateway habla con un unico servidor conocido y solo necesita POST y GET
// con cuerpo JSON. Para ese alcance, un socket TCP y una peticion HTTP/1.1
// escrita a mano son ~200 lineas sin dependencias, frente a arrastrar libcurl.
//
// Limitaciones asumidas a proposito:
//   - Sin TLS. En despliegue real el servidor debe estar en la misma red o
//     detras de un tunel; anadir TLS implicaria enlazar OpenSSL.
//   - Sin redirecciones ni chunked encoding: se espera Content-Length.
//   - Una conexion por peticion (sin keep-alive).

#include <string>
#include <cstdint>

namespace net {

struct Response {
    bool        ok = false;      // hubo respuesta HTTP valida (no error de red)
    int         status = 0;      // codigo HTTP
    std::string body;
    std::string error;           // motivo si ok == false
};

class HttpClient {
public:
    HttpClient(std::string host, uint16_t port, int timeoutMs = 3000);

    // Cabecera Authorization: Bearer <token>. Vacio = sin cabecera.
    void setBearer(std::string token);

    Response post(const std::string& path, const std::string& jsonBody);
    Response get(const std::string& path);

private:
    Response request(const char* method, const std::string& path, const std::string& body);

    std::string host_;
    uint16_t    port_;
    int         timeoutMs_;
    std::string bearer_;
};

} // namespace net
