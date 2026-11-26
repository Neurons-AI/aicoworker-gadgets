/* ---- Link: plain WebSocket on the LAN, or the AICW relay with E2EE ----
 *
 * aicw_host = "ip[:port]"          -> ws://ip:port/stick (same LAN as the bridge)
 * aicw_host = "relay:<hostId>"     -> wss://agent.aicoworker.net/g/<hostId>
 * The relay path speaks AICoworker's relay protocol (relay-protocol.ts, e2ee.ts):
 * Ed25519 relay auth, X25519 + HKDF-SHA256 + AES-256-GCM session, and inside it
 * each plaintext is 0x00 + JSON text or 0x01 + binary (PROTOCOL.md frames).
 */
int crypto_sign_keypair(unsigned char *pk, unsigned char *sk);
int crypto_sign(unsigned char *sm, unsigned long long *smlen, const unsigned char *m, unsigned long long n, const unsigned char *sk);
int crypto_sign_open(unsigned char *m, unsigned long long *mlen, const unsigned char *sm, unsigned long long n, const unsigned char *pk);
int crypto_scalarmult(unsigned char *q, const unsigned char *n, const unsigned char *p);
int crypto_scalarmult_base(unsigned char *q, const unsigned char *n);
#define RELAY_HOST "agent.aicoworker.net"
#define ENC_CAP (8 * 1024)
#define LINK_TIMEOUT_US (12 * 1000000LL)

static esp_tls_t *s_tls;
static bool s_relay;
static uint8_t s_hpub[32];
static uint8_t s_sk[64], s_pk[32];
static bool s_have_id;
static psa_key_id_t s_ktx, s_krx;
static uint64_t s_sc, s_rc;
static uint8_t *s_enc, *s_pt, *s_dec;

/* tweetnacl wants randombytes(); a seed can be handed over for keypair-from-seed. */
static const uint8_t *s_seed_once;
void randombytes(unsigned char *x, unsigned long long n)
{
    if (s_seed_once && n == 32) {
        memcpy(x, s_seed_once, 32);
        s_seed_once = NULL;
        return;
    }
    esp_fill_random(x, n);
}

/* ---- aicw_host in its own NVS namespace (dou_settings only accepts Dou hosts) ---- */
void aicw_host_get(char *out)
{
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open("aicw", NVS_READONLY, &h) == ESP_OK) {
        size_t l = DOU_HOST_MAX + 1;
        if (nvs_get_str(h, "host", out, &l) != ESP_OK) out[0] = '\0';
        nvs_close(h);
    }
    if (!out[0]) strlcpy(out, CONFIG_DOU_AICW_DEFAULT_HOST, DOU_HOST_MAX + 1);
}

bool aicw_host_set(const char *v)
{
    if (!v || strlen(v) > DOU_HOST_MAX) return false;
    nvs_handle_t h;
    if (nvs_open("aicw", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = v[0] ? nvs_set_str(h, "host", v) : nvs_erase_key(h, "host");
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

static void load_identity(void)
{
    if (s_have_id) return;
    uint8_t seed[32];
    nvs_handle_t h;
    size_t l = sizeof(seed);
    bool ok = false;
    if (nvs_open("aicw", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_get_blob(h, "seed", seed, &l) == ESP_OK && l == 32) {
            ok = true;
        } else {
            esp_fill_random(seed, sizeof(seed));
            ok = nvs_set_blob(h, "seed", seed, sizeof(seed)) == ESP_OK && nvs_commit(h) == ESP_OK;
        }
        nvs_close(h);
    }
    if (!ok) esp_fill_random(seed, sizeof(seed));
    s_seed_once = seed;
    crypto_sign_keypair(s_pk, s_sk);
    memset(seed, 0, sizeof(seed));
    s_have_id = true;
}


/* ---- small crypto helpers (PSA + tweetnacl) ---- */
static void sha256_parts(uint8_t out[32], const void *const *p, const size_t *n, int count)
{
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    size_t ol = 0;
    psa_hash_setup(&op, PSA_ALG_SHA_256);
    for (int i = 0; i < count; i++) psa_hash_update(&op, p[i], n[i]);
    if (psa_hash_finish(&op, out, 32, &ol) != PSA_SUCCESS) memset(out, 0, 32);
}

static bool hmac_parts(const uint8_t *key, size_t kl, const void *const *p, const size_t *n, int count, uint8_t out[32])
{
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&a, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&a, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&a, kl * 8);
    psa_key_id_t k = 0;
    if (psa_import_key(&a, key, kl, &k) != PSA_SUCCESS) return false;
    psa_mac_operation_t op = PSA_MAC_OPERATION_INIT;
    size_t ol = 0;
    bool ok = psa_mac_sign_setup(&op, k, PSA_ALG_HMAC(PSA_ALG_SHA_256)) == PSA_SUCCESS;
    for (int i = 0; ok && i < count; i++) ok = psa_mac_update(&op, p[i], n[i]) == PSA_SUCCESS;
    ok = ok && psa_mac_sign_finish(&op, out, 32, &ol) == PSA_SUCCESS && ol == 32;
    if (!ok) psa_mac_abort(&op);
    psa_destroy_key(k);
    return ok;
}

static psa_key_id_t aes_key(const uint8_t key[32])
{
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&a, PSA_ALG_GCM);
    psa_set_key_type(&a, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&a, 256);
    psa_key_id_t k = 0;
    if (psa_import_key(&a, key, 32, &k) != PSA_SUCCESS) return 0;
    return k;
}

static void make_nonce(uint8_t n[12], uint64_t ctr, uint8_t dir)
{
    memset(n, 0, 12);
    n[0] = dir;
    for (int i = 11; i >= 1 && ctr; i--, ctr >>= 8) n[i] = ctr & 0xff;
}

static void sess_clear(void)
{
    if (s_ktx) psa_destroy_key(s_ktx);
    if (s_krx) psa_destroy_key(s_krx);
    s_ktx = s_krx = 0;
    s_sc = s_rc = 0;
}

static bool b32_decode32(const char *s, uint8_t out[32])
{
    uint32_t buf = 0;
    int bits = 0;
    size_t o = 0;
    for (; *s && o < 32; s++) {
        char c = *s;
        int v;
        if (c >= 'a' && c <= 'z') v = c - 'a';
        else if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= '2' && c <= '7') v = c - '2' + 26;
        else return false;
        buf = (buf << 5) | v;
        bits += 5;
        if (bits >= 8) {
            out[o++] = (buf >> (bits - 8)) & 0xff;
            bits -= 8;
        }
    }
    return o == 32;
}

/* ---- raw I/O over plain TCP or TLS ---- */
static int io_write(const void *p, size_t n)
{
    if (s_tls) {
        ssize_t r = esp_tls_conn_write(s_tls, p, n);
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        if (r < 0) {
            errno = EIO;
            return -1;
        }
        return r;
    }
    return send(s_sock, p, n, 0);
}

static int io_read(void *p, size_t n)
{
    if (s_tls) {
        ssize_t r = esp_tls_conn_read(s_tls, p, n);
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        if (r < 0) {
            errno = EIO;
            return -1;
        }
        return r;
    }
    return recv(s_sock, p, n, 0);
}

static void ws_close(const char *why)
{
    if (s_tls) {
        ESP_LOGI(TAG, "disconnect: %s", why);
        esp_tls_conn_destroy(s_tls);   /* closes the socket too */
        s_tls = NULL;
    } else if (s_sock >= 0) {
        ESP_LOGI(TAG, "disconnect: %s", why);
        close(s_sock);
    }
    s_sock = -1;
    s_rx_len = 0;
    sess_clear();
    if (s_turn) {
        emit(DOU_HATCH_EV_ERROR, s_turn, "Mất kết nối AICW");
        s_turn = 0;
        s_sending = false;
    }
    dou_hatch_report(DOU_HATCH_UNREACHABLE, why);
}

static bool send_all(const void *data, size_t len)
{
    const uint8_t *p = data;
    int64_t deadline = now_us() + 5 * 1000000LL;
    while (len) {
        int n = io_write(p, len);
        if (n > 0) {
            p += n;
            len -= n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && now_us() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        return false;
    }
    return true;
}

/* One masked client frame. */
static bool ws_frame(uint8_t opcode, const void *data, size_t len)
{
    if (s_sock < 0) return false;
    uint8_t h[14];
    size_t hl = 0;
    h[hl++] = 0x80 | opcode;
    if (len < 126) {
        h[hl++] = 0x80 | len;
    } else if (len < 65536) {
        h[hl++] = 0x80 | 126;
        h[hl++] = len >> 8;
        h[hl++] = len & 0xff;
    } else {
        h[hl++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) h[hl++] = (uint64_t)len >> (8 * i);
    }
    uint32_t key = esp_random();
    uint8_t mask[4] = { key, key >> 8, key >> 16, key >> 24 };
    memcpy(h + hl, mask, 4);
    hl += 4;
    if (!send_all(h, hl)) goto fail;
    const uint8_t *src = data;
    for (size_t off = 0; off < len;) {
        size_t n = len - off > TX_CHUNK ? TX_CHUNK : len - off;
        for (size_t i = 0; i < n; i++) s_tx[i] = src[off + i] ^ mask[(off + i) & 3];
        if (!send_all(s_tx, n)) goto fail;
        off += n;
    }
    s_last_tx = now_us();
    return true;
fail:
    ws_close("send failed");
    return false;
}

/* A stick protocol frame: opcode 0x1 = JSON text, 0x2 = binary audio. */
static bool ws_send(uint8_t opcode, const void *data, size_t len)
{
    if (s_sock < 0) return false;
    if (!s_relay || opcode >= 0x8) return ws_frame(opcode, data, len);
    if (len + 1 + 30 > ENC_CAP) {
        ESP_LOGW(TAG, "relay frame too large (%u)", (unsigned)len);
        return false;
    }
    s_pt[0] = opcode == 0x1 ? 0 : 1;
    memcpy(s_pt + 1, data, len);
    uint8_t *f = s_enc;
    f[0] = 0x10;   /* FrameToHost */
    f[1] = 0x03;   /* InnerTransport */
    make_nonce(f + 2, s_sc++, 0);
    size_t ol = 0;
    if (psa_aead_encrypt(s_ktx, PSA_ALG_GCM, f + 2, 12, NULL, 0, s_pt, len + 1, f + 14, ENC_CAP - 14, &ol) != PSA_SUCCESS) {
        ws_close("relay encrypt");
        return false;
    }
    return ws_frame(0x2, f, 14 + ol);
}

/* Blocking: next complete frame in s_rx (handshake phase only). */
static int next_frame(uint8_t **pl, size_t *plen, size_t *used, int64_t deadline)
{
    for (;;) {
        if (s_rx_len >= 2) {
            uint8_t *p = s_rx;
            uint64_t len = p[1] & 0x7f;
            size_t hl = 2;
            if (len == 126 && s_rx_len >= 4) {
                len = ((uint64_t)p[2] << 8) | p[3];
                hl = 4;
            } else if (len == 127 && s_rx_len >= 10) {
                len = 0;
                for (int i = 0; i < 8; i++) len = (len << 8) | p[2 + i];
                hl = 10;
            } else if (len >= 126) {
                goto more;
            }
            if (p[1] & 0x80) hl += 4;
            if (len > RX_CAP - 16) return -1;
            if (s_rx_len >= hl + len) {
                if (p[1] & 0x80) {
                    for (uint64_t i = 0; i < len; i++) p[hl + i] ^= p[hl - 4 + (i & 3)];
                }
                *pl = p + hl;
                *plen = len;
                *used = hl + len;
                return p[0] & 0x0f;
            }
        }
    more:
        if (now_us() > deadline) return -1;
        int r = io_read(s_rx + s_rx_len, RX_CAP - s_rx_len);
        if (r > 0) {
            s_rx_len += r;
        } else if (r == 0) {
            return -1;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
    }
}

static void rx_consume(size_t used)
{
    memmove(s_rx, s_rx + used, s_rx_len - used);
    s_rx_len -= used;
}

static bool ws_upgrade(const char *host_hdr, const char *path)
{
    char req[320];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
                     path, host_hdr);
    if (n <= 0 || n >= (int)sizeof(req) || !send_all(req, n)) {
        ws_close("handshake send");
        return false;
    }
    size_t got = 0;
    char *resp = (char *)s_rx;
    int64_t deadline = now_us() + LINK_TIMEOUT_US;
    while (got < 1023) {
        int r = io_read(resp + got, 1023 - got);
        if (r <= 0) {
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && now_us() < deadline) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ws_close("handshake recv");
            return false;
        }
        got += r;
        resp[got] = '\0';
        char *end = strstr(resp, "\r\n\r\n");
        if (end) {
            if (strncmp(resp, "HTTP/1.1 101", 12) != 0) {
                ESP_LOGW(TAG, "upgrade refused: %.60s", resp);
                ws_close("handshake refused");
                return false;
            }
            size_t extra = got - (end + 4 - resp);
            memmove(s_rx, end + 4, extra);
            s_rx_len = extra;
            return true;
        }
    }
    ws_close("handshake too long");
    return false;
}

static bool relay_handshake(void)
{
    static const char RA[] = "aicw-relay-auth-v1", CI[] = "aicw-e2ee-v1/init", CR[] = "aicw-e2ee-v1/resp",
                      INFO[] = "aicw-e2ee-v1";
    const uint8_t zero = 0, role = 2;
    uint8_t eph_sk[32], eph_pk[32], th[32], sm[96];
    unsigned long long sml = 0;
    esp_fill_random(eph_sk, sizeof(eph_sk));
    crypto_scalarmult_base(eph_pk, eph_sk);
    int64_t deadline = now_us() + LINK_TIMEOUT_US;
    bool authed = false;
    for (;;) {
        uint8_t *p;
        size_t n, used;
        int op = next_frame(&p, &n, &used, deadline);
        if (op < 0) {
            ws_close("relay timeout");
            return false;
        }
        if (op == 0x8) {
            ws_close("relay closed");
            return false;
        }
        if (op != 0x2 || n == 0) {
            rx_consume(used);
            continue;
        }
        uint8_t t = p[0];
        if (t == 0x01 && n >= 34 && !authed) {          /* hello + challenge */
            const char *rh = RELAY_HOST;
            const void *parts[] = { RA, &zero, rh, &zero, p + 2, &role, s_pk, s_hpub };
            const size_t lens[] = { strlen(RA), 1, strlen(rh), 1, 32, 1, 32, 32 };
            sha256_parts(th, parts, lens, 8);
            crypto_sign(sm, &sml, th, 32, s_sk);
            uint8_t auth[2 + 32 + 32 + 64];
            auth[0] = 0x02;
            auth[1] = role;
            memcpy(auth + 2, s_pk, 32);
            memcpy(auth + 34, s_hpub, 32);
            memcpy(auth + 66, sm, 64);
            rx_consume(used);
            if (!ws_frame(0x2, auth, sizeof(auth))) return false;
        } else if (t == 0x03) {                        /* auth ok -> E2EE init */
            const void *parts[] = { CI, s_hpub, s_pk, eph_pk };
            const size_t lens[] = { strlen(CI), 32, 32, 32 };
            sha256_parts(th, parts, lens, 4);
            crypto_sign(sm, &sml, th, 32, s_sk);
            uint8_t init[1 + 1 + 1 + 32 + 32 + 64];
            init[0] = 0x10;
            init[1] = 0x01;
            init[2] = 1;
            memcpy(init + 3, s_pk, 32);
            memcpy(init + 35, eph_pk, 32);
            memcpy(init + 67, sm, 64);
            rx_consume(used);
            authed = true;
            if (!ws_frame(0x2, init, sizeof(init))) return false;
        } else if (t == 0x04 || t == 0x22) {
            ESP_LOGW(TAG, "relay refused: %.*s", (int)(n > 80 ? 80 : n - 1), (const char *)p + 1);
            rx_consume(used);
            ws_close(t == 0x22 ? "Máy AICW đang tắt" : "Relay từ chối");
            return false;
        } else if (t == 0x40) {                        /* ping */
            uint8_t pong[64];
            size_t bl = n > sizeof(pong) ? sizeof(pong) : n;
            memcpy(pong, p, bl);
            pong[0] = 0x41;
            rx_consume(used);
            if (!ws_frame(0x2, pong, bl)) return false;
        } else if (t == 0x13 && authed && n >= 2) {    /* host handshake answer */
            if (p[1] != 0x02 || n != 1 + 98) {
                rx_consume(used);
                ws_close("Máy AICW từ chối");
                return false;
            }
            const uint8_t *heph = p + 3, *hsig = p + 35;
            const void *parts[] = { CR, s_hpub, s_pk, eph_pk, heph };
            const size_t lens[] = { strlen(CR), 32, 32, 32, 32 };
            sha256_parts(th, parts, lens, 5);
            uint8_t chk[96], mo[96];
            unsigned long long mol = 0;
            memcpy(chk, hsig, 64);
            memcpy(chk + 64, th, 32);
            if (crypto_sign_open(mo, &mol, chk, 96, s_hpub) != 0) {
                rx_consume(used);
                ws_close("Chữ ký máy AICW sai");
                return false;
            }
            uint8_t ss[32], salt[64], prk[32], t1[32], t2[32];
            crypto_scalarmult(ss, eph_sk, heph);
            memcpy(salt, eph_pk, 32);
            memcpy(salt + 32, heph, 32);
            const uint8_t c1 = 1, c2 = 2;
            const void *ex[] = { ss };
            const size_t exl[] = { 32 };
            const void *e1[] = { INFO, &c1 };
            const size_t e1l[] = { strlen(INFO), 1 };
            const void *e2[] = { t1, INFO, &c2 };
            const size_t e2l[] = { 32, strlen(INFO), 1 };
            bool ok = hmac_parts(salt, 64, ex, exl, 1, prk) && hmac_parts(prk, 32, e1, e1l, 2, t1) &&
                      hmac_parts(prk, 32, e2, e2l, 3, t2);
            rx_consume(used);
            sess_clear();
            if (ok) {
                s_ktx = aes_key(t1);   /* guest -> host */
                s_krx = aes_key(t2);   /* host -> guest */
            }
            memset(ss, 0, sizeof(ss));
            memset(prk, 0, sizeof(prk));
            memset(t1, 0, sizeof(t1));
            memset(t2, 0, sizeof(t2));
            memset(eph_sk, 0, sizeof(eph_sk));
            if (!s_ktx || !s_krx) {
                ws_close("relay keys");
                return false;
            }
            return true;
        } else {
            rx_consume(used);
        }
    }
}

static bool link_open(void)
{
    char host[DOU_HOST_MAX + 1];
    aicw_host_get(host);
    if (!host[0]) {
        dou_hatch_report(DOU_HATCH_NOT_SET, "Chưa đặt aicw_host");
        return false;
    }
    dou_hatch_report(DOU_HATCH_TESTING, "Đang kết nối AICW...");
    s_relay = !strncmp(host, "relay:", 6);
    if (s_relay) {
        if (!b32_decode32(host + 6, s_hpub)) {
            dou_hatch_report(DOU_HATCH_NOT_SET, "hostId sai");
            return false;
        }
        load_identity();
        psa_crypto_init();
        s_tls = esp_tls_init();
        esp_tls_cfg_t cfg = { .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 10000 };
        if (!s_tls || esp_tls_conn_new_sync(RELAY_HOST, strlen(RELAY_HOST), 443, &cfg, s_tls) != 1) {
            if (s_tls) esp_tls_conn_destroy(s_tls);
            s_tls = NULL;
            dou_hatch_report(DOU_HATCH_UNREACHABLE, "Không tới được relay");
            return false;
        }
        int fd = -1;
        esp_tls_get_conn_sockfd(s_tls, &fd);
        s_sock = fd;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);   /* io_* map WANT_READ to EAGAIN */
        char path[96];
        snprintf(path, sizeof(path), "/g/%s", host + 6);
        if (!ws_upgrade(RELAY_HOST, path) || !relay_handshake()) return false;
        ESP_LOGI(TAG, "relay E2EE session up (%s)", host + 6);
    } else {
        char *port = (char *)"8765", *colon = strrchr(host, ':');
        if (colon) {
            *colon = '\0';
            port = colon + 1;
        }
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
        struct addrinfo *res = NULL;
        if (getaddrinfo(host, port, &hints, &res) != 0 || !res) {
            dou_hatch_report(DOU_HATCH_UNREACHABLE, "Không tìm thấy host");
            return false;
        }
        int fd = socket(res->ai_family, res->ai_socktype, 0);
        if (fd < 0) {
            freeaddrinfo(res);
            return false;
        }
        struct timeval tv = { .tv_sec = 5 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
            freeaddrinfo(res);
            close(fd);
            dou_hatch_report(DOU_HATCH_UNREACHABLE, "Bridge không trả lời");
            return false;
        }
        freeaddrinfo(res);
        s_sock = fd;
        char hh[DOU_HOST_MAX + 8];
        snprintf(hh, sizeof(hh), "%s:%s", host, port);
        if (!ws_upgrade(hh, "/stick")) return false;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    return true;
}

/* An inbound relay frame (binary WebSocket message from agent.aicoworker.net). */
static void on_text(const char *s, size_t len);
static void on_binary(const uint8_t *data, size_t len);
static void relay_frame(const uint8_t *p, size_t n)
{
    if (!n) return;
    if (p[0] == 0x40) {
        uint8_t pong[64];
        size_t bl = n > sizeof(pong) ? sizeof(pong) : n;
        memcpy(pong, p, bl);
        pong[0] = 0x41;
        ws_frame(0x2, pong, bl);
        return;
    }
    if (p[0] == 0x22) {
        ws_close("Máy AICW đang tắt");
        return;
    }
    if (p[0] != 0x13) return;
    p++;
    n--;
    if (n < 1 + 12 + 16 || p[0] != 0x03) return;
    uint8_t exp[12];
    make_nonce(exp, s_rc, 1);
    if (memcmp(p + 1, exp, 12) != 0) {
        ws_close("relay out of order");
        return;
    }
    size_t ol = 0;
    if (psa_aead_decrypt(s_krx, PSA_ALG_GCM, p + 1, 12, NULL, 0, p + 13, n - 13, s_dec, RX_CAP, &ol) != PSA_SUCCESS || !ol) {
        ws_close("relay decrypt");
        return;
    }
    s_rc++;
    if (s_dec[0] == 0) on_text((const char *)s_dec + 1, ol - 1);
    else on_binary(s_dec + 1, ol - 1);
}
