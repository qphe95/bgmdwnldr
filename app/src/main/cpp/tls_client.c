#include "tls_client.h"

#include <stdio.h>
#include <string.h>

#define TLS_ERR_GENERIC -0x7000

static void set_err(char *err, size_t errLen, const char *msg, int code) {
    if (!err || errLen == 0) {
        return;
    }
    if (code != 0) {
        snprintf(err, errLen, "%s (err=%d)", msg, code);
    } else {
        snprintf(err, errLen, "%s", msg);
    }
}

static void tls_init(TlsClient *client) {
    mbedtls_net_init(&client->net);
    mbedtls_ssl_init(&client->ssl);
    mbedtls_ssl_config_init(&client->conf);
    mbedtls_x509_crt_init(&client->ca);
    client->connected = false;
}

bool tls_client_connect(TlsClient *client, const char *host, const char *port,
                        char *err, size_t errLen) {
    if (!client || !host || !port) {
        set_err(err, errLen, "TLS invalid params", TLS_ERR_GENERIC);
        return false;
    }
    tls_init(client);
    int ret = psa_crypto_init();
    if (ret != PSA_SUCCESS) {
        set_err(err, errLen, "PSA crypto init failed", ret);
        return false;
    }
    ret = mbedtls_x509_crt_parse_path(&client->ca, "/system/etc/security/cacerts");
    if (ret < 0) {
        set_err(err, errLen, "TLS CA load failed", ret);
        return false;
    }
    ret = mbedtls_net_connect(&client->net, host, port, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        set_err(err, errLen, "TLS connect failed", ret);
        return false;
    }
    ret = mbedtls_ssl_config_defaults(&client->conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        set_err(err, errLen, "TLS config defaults failed", ret);
        return false;
    }
    mbedtls_ssl_conf_authmode(&client->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&client->conf, &client->ca, NULL);
    (void)psa_generate_random;
    ret = mbedtls_ssl_setup(&client->ssl, &client->conf);
    if (ret != 0) {
        set_err(err, errLen, "TLS setup failed", ret);
        return false;
    }
    ret = mbedtls_ssl_set_hostname(&client->ssl, host);
    if (ret != 0) {
        set_err(err, errLen, "TLS set hostname failed", ret);
        return false;
    }
    mbedtls_ssl_set_bio(&client->ssl, &client->net,
                        mbedtls_net_send, mbedtls_net_recv, NULL);
    while ((ret = mbedtls_ssl_handshake(&client->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            set_err(err, errLen, "TLS handshake failed", ret);
            return false;
        }
    }
    ret = (int)mbedtls_ssl_get_verify_result(&client->ssl);
    if (ret != 0) {
        set_err(err, errLen, "TLS verify failed", ret);
        return false;
    }
    client->connected = true;
    return true;
}

ssize_t tls_client_read(TlsClient *client, unsigned char *buf, size_t len) {
    if (!client || !client->connected) {
        return -1;
    }
    int ret = mbedtls_ssl_read(&client->ssl, buf, len);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
        ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
        ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
        return 0;
    }
    return ret;
}

ssize_t tls_client_write(TlsClient *client, const unsigned char *buf, size_t len) {
    if (!client || !client->connected) {
        return -1;
    }
    int ret = mbedtls_ssl_write(&client->ssl, buf, len);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    return ret;
}

void tls_client_close(TlsClient *client) {
    if (!client) {
        return;
    }
    if (client->connected) {
        mbedtls_ssl_close_notify(&client->ssl);
    }
    mbedtls_net_free(&client->net);
    mbedtls_x509_crt_free(&client->ca);
    mbedtls_ssl_free(&client->ssl);
    mbedtls_ssl_config_free(&client->conf);
    client->connected = false;
}
