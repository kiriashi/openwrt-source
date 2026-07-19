#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <uci.h>

#define RTPRIV_IOCTL_SET (SIOCIWFIRSTPRIV + 0x02)
#define MAX_STA 4
#define RECONNECT_COOLDOWN 45
#define TICK_INTERVAL 5

typedef struct {
    char ifname[16];   // 真实接口名，如 apcli0
    char parent[16];   // 父接口名，如 ra0
    char ssid[64];
    char key[64];
    char auth[32];
    char enc[32];
    time_t next_try;
    int fail_count;    // 错误计数器，用于分级重置
} StaInterface;

int set_if_up(int sock, const char *ifname) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) return -1;
    if (!(ifr.ifr_flags & IFF_UP)) {
        ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
        return ioctl(sock, SIOCSIFFLAGS, &ifr);
    }
    return 0;
}

int mtk_ioctl_set(int sock, const char *ifname, const char *fmt, ...) {
    struct iwreq wrq;
    char buffer[256];
    va_list args;
    
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    memset(&wrq, 0, sizeof(wrq));
    snprintf(wrq.ifr_name, sizeof(wrq.ifr_name), "%s", ifname);
    wrq.u.data.pointer = buffer;
    wrq.u.data.length = strlen(buffer);
    wrq.u.data.flags = 0;

    return ioctl(sock, RTPRIV_IOCTL_SET, &wrq);
}

/* 严格的状态检查：结合 BSSID 与 驱动 Link 状态 */
bool is_connected(int sock, const char *ifname) {
    struct iwreq wrq;
    memset(&wrq, 0, sizeof(wrq));
    snprintf(wrq.ifr_name, sizeof(wrq.ifr_name), "%s", ifname);
    
    if (ioctl(sock, SIOCGIWAP, &wrq) < 0) return false;
    
    unsigned char *mac = (unsigned char *)wrq.u.ap_addr.sa_data;
    int sum = 0;
    for(int i = 0; i < 6; i++) sum += mac[i];
    
    if (sum == 0 || sum == 255 * 6) return false;

    /* 引入第二重防御：检查无线质量结构体，若连通则 Qual 属性应为非零 */
    struct iw_statistics stats;
    memset(&stats, 0, sizeof(stats));
    wrq.u.data.pointer = &stats;
    wrq.u.data.length = sizeof(stats);
    wrq.u.data.flags = 1; // Clear updated flag
    
    if (ioctl(sock, SIOCGIWSTATS, &wrq) >= 0) {
        // 如果链路质量显示为 0，说明驱动层虽有 BSSID 缓存，但并未真正建立通信链路
        if (stats.qual.qual == 0) return false;
    }

    return true;
}

void map_encryption(const char *uci_enc, char *auth, char *enc) {
    if (!uci_enc) {
        strcpy(auth, "WPA2PSK"); strcpy(enc, "AES");
        return;
    }
    if (strstr(uci_enc, "psk2") || strstr(uci_enc, "ccmp")) {
        strcpy(auth, "WPA2PSK"); strcpy(enc, "AES");
    } else if (strstr(uci_enc, "psk")) {
        strcpy(auth, "WPAPSK"); strcpy(enc, "TKIP");
    } else {
        strcpy(auth, "OPEN"); strcpy(enc, "NONE");
    }
}

/* 严格配置解析：支持从 UCI 动态获取真实 ifname */
int load_sta_configs(struct uci_context *ctx, StaInterface *list) {
    struct uci_package *pkg = NULL;
    int count = 0;
    
    memset(list, 0, sizeof(StaInterface) * MAX_STA);
    if (uci_load(ctx, "wireless", &pkg) != UCI_OK) return 0;

    struct uci_element *e;
    uci_foreach_element(&pkg->sections, e) {
        struct uci_section *s = uci_to_section(e);
        if (strcmp(s->type, "wifi-iface") != 0) continue;

        struct uci_option *m = uci_lookup_option(ctx, s, "mode");
        if (m && strcmp(m->v.string, "sta") == 0) {
            struct uci_option *ifn = uci_lookup_option(ctx, s, "ifname");
            struct uci_option *dev = uci_lookup_option(ctx, s, "device");
            struct uci_option *s_id = uci_lookup_option(ctx, s, "ssid");
            struct uci_option *key = uci_lookup_option(ctx, s, "key");
            struct uci_option *enc = uci_lookup_option(ctx, s, "encryption");

            if (dev && s_id && count < MAX_STA) {
                // 如果 UCI 中明确指定了 ifname（如 apcli0），以真实配置为准；否则退入智能推导
                if (ifn && ifn->v.string) {
                    snprintf(list[count].ifname, sizeof(list[count].ifname), "%s", ifn->v.string);
                } else {
                    // 备份推导逻辑
                    if (strstr(dev->v.string, "1") || strstr(dev->v.string, "rax")) {
                        strcpy(list[count].ifname, "apclix0");
                    } else {
                        strcpy(list[count].ifname, "apcli0");
                    }
                }

                // 推导 Parent 接口
                if (strcmp(list[count].ifname, "apclix0") == 0) {
                    strcpy(list[count].parent, "rax0");
                } else {
                    strcpy(list[count].parent, "ra0");
                }

                snprintf(list[count].ssid, sizeof(list[count].ssid), "%s", s_id->v.string);
                snprintf(list[count].key, sizeof(list[count].key), "%s", key ? key->v.string : "");
                map_encryption(enc ? enc->v.string : NULL, list[count].auth, list[count].enc);
                
                list[count].next_try = 0;
                list[count].fail_count = 0;
                count++;
            }
        }
    }
    uci_unload(ctx, pkg);
    return count;
}

void do_connect_transaction(int sock, StaInterface *iface) {
    printf("[MTK-WiFi] Execution Transaction on %s -> SSID [%s] (Fail Count: %d)\n", 
           iface->ifname, iface->ssid, iface->fail_count);

    set_if_up(sock, iface->parent);
    set_if_up(sock, iface->ifname);

    /* 优化点：分级重置策略 */
    if (iface->fail_count >= 1) {
        // 连续连接失败 1 次以上，认定驱动底层死锁，执行深度的接口射频完全重置
        mtk_ioctl_set(sock, iface->ifname, "ApCliEnable=0");
        usleep(100000);
    }
    
    // 停止底层可能正在冲突的扫描任务
    mtk_ioctl_set(sock, iface->ifname, "ScanStop=1");

    // 严禁在此处下发 AutoChannelSel=1 
    mtk_ioctl_set(sock, iface->ifname, "ApCliAuthMode=%s", iface->auth);
    mtk_ioctl_set(sock, iface->ifname, "ApCliEncrypType=%s", iface->enc);
    if (strlen(iface->key) > 0) {
        mtk_ioctl_set(sock, iface->ifname, "ApCliWPAPSK=%s", iface->key);
    }
    
    // 最终核心：下发目标 SSID 并触发联发科底层芯片连接机制
    mtk_ioctl_set(sock, iface->ifname, "ApCliSsid=%s", iface->ssid);
    
    if (iface->fail_count >= 1) {
        mtk_ioctl_set(sock, iface->ifname, "ApCliAutoConnect=1");
        mtk_ioctl_set(sock, iface->ifname, "ApCliEnable=1");
    }

    iface->fail_count++;
    iface->next_try = time(NULL) + RECONNECT_COOLDOWN;
}

int main() {
    struct uci_context *ctx = uci_alloc_context();
    if (!ctx) return 1;

    StaInterface ifaces[MAX_STA];
    int count = load_sta_configs(ctx, ifaces);
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        uci_free_context(ctx);
        return 1;
    }

    printf("[Daemon] Industrial MTK WiFi Monitor started. Active profiles: %d\n", count);

    while (1) {
        time_t now = time(NULL);
        for (int i = 0; i < count; i++) {
            if (!is_connected(sock, ifaces[i].ifname)) {
                if (now >= ifaces[i].next_try) {
                    do_connect_transaction(sock, &ifaces[i]);
                }
            } else {
                /* 连接健康时，彻底归零错误计数 */
                ifaces[i].next_try = 0;
                ifaces[i].fail_count = 0;
            }
        }
        sleep(TICK_INTERVAL);
    }

    close(sock);
    uci_free_context(ctx);
    return 0;
}
