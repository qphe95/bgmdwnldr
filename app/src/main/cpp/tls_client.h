#ifndef TLS_CLIENT_H
#define TLS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TlsClient {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    bool connected;
} TlsClient;

bool tls_client_connect(TlsClient *client, const char *host, const char *port,
                        char *err, size_t errLen);
ssize_t tls_client_read(TlsClient *client, unsigned char *buf, size_t len);
ssize_t tls_client_write(TlsClient *client, const unsigned char *buf, size_t len);
void tls_client_close(TlsClient *client);

#ifdef __cplusplus
}
#endif

#endif
