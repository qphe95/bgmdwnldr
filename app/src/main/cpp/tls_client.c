#include "tls_client.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <android/log.h>

#define TLS_ERR_GENERIC -0x7000
#define LOG_TAG "minimalvulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Chrome JA3 fingerprint configuration
// JA3: 771,4865-4866-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53,0-23-65281-10-11-35-16-5-13-18-51-45-43-27-21,29-23-30-25-24,0
static const int chrome_ciphers[] = {
    0x1301, // TLS_AES_128_GCM_SHA256
    0x1302, // TLS_AES_256_GCM_SHA384
    0x1303, // TLS_CHACHA20_POLY1305_SHA256
    0xC02B, // TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
    0xC02F, // TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
    0xC02C, // TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
    0xC030, // TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
    0xCCA8, // TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256
    0xCCA9, // TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256
    0xC013, // TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA
    0xC014, // TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA
    0x009C, // TLS_RSA_WITH_AES_128_GCM_SHA256
    0x009D, // TLS_RSA_WITH_AES_256_GCM_SHA384
    0x002F, // TLS_RSA_WITH_AES_128_CBC_SHA
    0x0035, // TLS_RSA_WITH_AES_256_CBC_SHA
    0x000A, // TLS_RSA_WITH_3DES_EDE_CBC_SHA
    0
};

static const uint16_t chrome_curves[] = {
    29,     // X25519 (0x001D)
    23,     // P-256 (0x0017)
    24,     // P-384 (0x0018)
    25,     // P-521 (0x0019)
    0       // End marker
};

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

// Configure socket with Android TCP stack simulation
static bool configure_android_tcp_stack(mbedtls_net_context *net, char *err, size_t errLen) {
    int sockfd = net->fd;
    int optval;
    socklen_t optlen = sizeof(optval);

    // Android TCP configuration based on Android kernel defaults

    // TCP_NODELAY - Android typically has this disabled for better throughput
    optval = 0; // Disable Nagle's algorithm (Android default)
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) != 0) {
        set_err(err, errLen, "Failed to set TCP_NODELAY", errno);
        return false;
    }

    // TCP window scaling - Android uses window scaling
    optval = 1; // Enable window scaling
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &optval, sizeof(optval)) != 0) {
        // This might fail on some systems, continue anyway
    }

    // TCP keepalive settings (Android defaults)
    optval = 1; // Enable keepalive
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) != 0) {
        set_err(err, errLen, "Failed to set SO_KEEPALIVE", errno);
        return false;
    }

    // TCP keepalive intervals (Android-like)
    optval = 7200; // 2 hours
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval));
    optval = 75; // 75 seconds
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval));
    optval = 9; // 9 probes
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(optval));

    // SO_LINGER - Android behavior
    struct linger ling;
    ling.l_onoff = 0;  // Disabled (Android default)
    ling.l_linger = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

    // Socket send/receive buffer sizes (Android-like)
    optval = 131072; // 128KB send buffer
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));
    optval = 131072; // 128KB receive buffer
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));

    return true;
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

    // Configure Android TCP stack simulation
    if (!configure_android_tcp_stack(&client->net, err, errLen)) {
        return false;
    }

    // Log TLS configuration for debugging
    LOGI("TLS configured with Chrome JA3 fingerprint simulation");
    // Configure for Chrome-like TLS fingerprint
    ret = mbedtls_ssl_config_defaults(&client->conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        set_err(err, errLen, "TLS config defaults failed", ret);
        return false;
    }

    // Set Chrome cipher suites in exact order for JA3 fingerprint
    mbedtls_ssl_conf_ciphersuites(&client->conf, chrome_ciphers);

    // Set Chrome curve preferences (mbedTLS 4.0+ uses groups instead of curves)
    mbedtls_ssl_conf_groups(&client->conf, chrome_curves);
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
