#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <uci.h>

/* MTK 驱动私有定义 */
#define RTPRIV_IOCTL_SET (SIOCIWFIRSTPRIV + 0x02)
#define MAX_STA 4
#define RECONNECT_COOLDOWN 45
#define TICK_INTERVAL 5

typedef struct {
    char device[32];
    char ifname[16];   // apcli0 / apclix0
    char parent[16];   // ra0 / rax0
    char ssid[64];
    char key[64];
    char auth[32];     // WPA2PSK, etc.
    char enc[32];      // AES, etc.
    time_t next_try;
    bool active;
} StaInterface;

/* 1. 工具函数：强制拉起接口状态 (IFF_UP) */
int set_if_up(int sock, const char *ifname) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) return -1;
    if (!(ifr.ifr_flags & IFF_UP)) {
        ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
        return ioctl(sock, SIOCSIFFLAGS, &ifr);
    }
    return 0;
}

/* 2. 工具函数：发送 MTK 私有 IOCTL 指令 */
int mtk_ioctl_set(int sock, const char *ifname, const char *fmt, ...) {
    struct iwreq wrq;
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    memset(&wrq, 0, sizeof(wrq));
    strncpy(wrq.ifr_name, ifname, IFNAMSIZ);
    wrq.u.data.pointer = buffer;
    wrq.u.data.length = strlen(buffer);
    wrq.u.data.flags = 0;

    return ioctl(sock, RTPRIV_IOCTL_SET, &wrq);
}

/* 3. 状态检查：通过 ioctl 获取 BSSID 判定连接 */
bool is_connected(int sock, const char *ifname) {
    struct iwreq wrq;
    memset(&wrq, 0, sizeof(wrq));
    strncpy(wrq.ifr_name, ifname, IFNAMSIZ);
    if (ioctl(sock, SIOCGIWAP, &wrq) < 0) return false;
    
    unsigned char *mac = (unsigned char *)wrq.u.ap_addr.sa_data;
    int sum = 0;
    for(int i=0; i<6; i++) sum += mac[i];
    return (sum != 0 && sum != 255 * 6); // 排除全0或全F
}

/* 4. 配置解析：将 OpenWrt 加密格式映射为 MTK 格式 */
void map_encryption(const char *uci_enc, char *auth, char *enc) {
    if (!uci_enc || strstr(uci_enc, "psk2") || strstr(uci_enc, "ccmp")) {
        strcpy(auth, "WPA2PSK"); strcpy(enc, "AES");
    } else if (strstr(uci_enc, "psk")) {
        strcpy(auth, "WPAPSK"); strcpy(enc, "TKIP");
    } else {
        strcpy(auth, "OPEN"); strcpy(enc, "NONE");
    }
}

/* 5. 加载 UCI 配置 */
int load_sta_configs(struct uci_context *ctx, StaInterface *list) {
    struct uci_package *pkg = NULL;
    int count = 0;
    if (uci_load(ctx, "wireless", &pkg) != UCI_OK) return 0;

    struct uci_element *e;
    uci_foreach_element(&pkg->sections, e) {
        struct uci_section *s = uci_to_section(e);
        if (strcmp(s->type, "wifi-iface") != 0) continue;

        struct uci_option *m = uci_lookup_option(ctx, s, "mode");
        if (m && strcmp(m->v.string, "sta") == 0) {
            struct uci_option *d = uci_lookup_option(ctx, s, "device");
            struct uci_option *s_id = uci_lookup_option(ctx, s, "ssid");
            struct uci_option *key = uci_lookup_option(ctx, s, "key");
            struct uci_option *enc = uci_lookup_option(ctx, s, "encryption");

            if (d && s_id && count < MAX_STA) {
                strncpy(list[count].device, d->v.string, 31);
                strncpy(list[count].ssid, s_id->v.string, 63);
                strncpy(list[count].key, key ? key->v.string : "", 63);
                map_encryption(enc ? enc->v.string : NULL, list[count].auth, list[count].enc);
                
                // MT7981 映射逻辑
                if (strstr(list[count].device, "MT7981_1_2")) {
                    strcpy(list[count].ifname, "apclix0"); strcpy(list[count].parent, "rax0");
                } else {
                    strcpy(list[count].ifname, "apcli0"); strcpy(list[count].parent, "ra0");
                }
                list[count].next_try = 0;
                list[count].active = true;
                count++;
            }
        }
    }
    uci_unload(ctx, pkg);
    return count;
}

/* 6. 执行连接事务 */
void do_connect_transaction(int sock, StaInterface *iface) {
    printf("[MTK] Starting Transaction: %s -> %s\n", iface->ifname, iface->ssid);

    // 解决 Network is down：强制拉起接口
    set_if_up(sock, iface->parent);
    set_if_up(sock, iface->ifname);
    usleep(200000); 

    // 设置参数序列
    mtk_ioctl_set(sock, iface->ifname, "ApCliEnable=0");
    mtk_ioctl_set(sock, iface->ifname, "ApCliAutoConnect=0");
    mtk_ioctl_set(sock, iface->ifname, "ScanStop=1");
    sleep(1);

    mtk_ioctl_set(sock, iface->ifname, "ApCliAuthMode=%s", iface->auth);
    mtk_ioctl_set(sock, iface->ifname, "ApCliEncrypType=%s", iface->enc);
    if (strlen(iface->key) > 0) {
        mtk_ioctl_set(sock, iface->ifname, "ApCliWPAPSK=%s", iface->key);
    }
    mtk_ioctl_set(sock, iface->ifname, "ApCliSsid=%s", iface->ssid);
    
    mtk_ioctl_set(sock, iface->parent, "AutoChannelSel=1");
    mtk_ioctl_set(sock, iface->ifname, "ApCliAutoConnect=1");
    mtk_ioctl_set(sock, iface->ifname, "ApCliEnable=1");

    iface->next_try = time(NULL) + RECONNECT_COOLDOWN;
}

int main() {
    struct uci_context *ctx = uci_alloc_context();
    StaInterface ifaces[MAX_STA];
    int count = load_sta_configs(ctx, ifaces);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    printf("[Daemon] MTK WiFi Monitor started. Interfaces: %d\n", count);

    while (1) {
        time_t now = time(NULL);
        for (int i = 0; i < count; i++) {
            if (!is_connected(sock, ifaces[i].ifname)) {
                if (now >= ifaces[i].next_try) {
                    do_connect_transaction(sock, &ifaces[i]);
                }
            } else {
                ifaces[i].next_try = 0; // 已连接则重置冷却
            }
        }
        sleep(TICK_INTERVAL);
    }

    close(sock);
    uci_free_context(ctx);
    return 0;
}