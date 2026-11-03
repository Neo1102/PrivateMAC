// PrivateMAC — iOS 14.8 專用 Wi-Fi 位址：關閉／固定／輪替（像新版 iOS），點「Wi-Fi 位址」可編輯
// 同一個 dylib，依處理程序各做一件事：
//   wifid       hook __WiFiManagerUpdateNetworkWithPrivateMacInfo（18H17 的固定位址，位元組不符就不裝）。
//               原版只在舊值是硬體位址時才收下 UI 傳來的新位址（舊值已是專用位址就丟掉，MacSpoof 沒效就是這個原因），
//               這裡先把舊值換成全 0，新位址才會被收下。
//   SpringBoard -[SBWiFiManager _linkDidChange]：離開「輪替」的網路時，先換好下一次連線要用的位址。
//   設定 App    WiFi 詳情頁：開關那列改成「關閉／固定／輪替」選單頁；點「Wi-Fi 位址」可編輯或隨機產生。
// 換位址一律走 WiFiManagerClientSetPrivateMacPrefForScanRecord(client, SSID, 位址, 開啟)。
// 「輪替」的網路存在 CFPreferences com.neo.privatemac（rotate = SSID 陣列）。
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void *dlopen(const char *, int);
void *dlsym(void *, const char *);
int snprintf(char *, size_t, const char *, ...);
int strcmp(const char *, const char *);
size_t strlen(const char *);
long time(void *);
extern struct dispatch_queue_s _dispatch_main_q;
uint64_t dispatch_time(uint64_t, int64_t);
void dispatch_after_f(uint64_t, void *, void *, void (*)(void *));
#define RTLD_DEFAULT ((void *)-2)
#define RTLD_NOW 2
#define SYM(v, n) (*(void **)&(v) = dlsym(RTLD_DEFAULT, n))

typedef void *id, *SEL;
static id (*getClass)(const char *);
static SEL (*sel)(const char *);
static void *msgSend;
#define MSG(R, ...) ((R (*)(id, SEL, ##__VA_ARGS__))msgSend)
static void (*hookMsg)(id, SEL, void *, void **);
static void (*hookFn)(void *, void *, void **);

static id nsstr(const char *s) { return MSG(id, const char *)(getClass("NSString"), sel("stringWithUTF8String:"), s); }
static bool streq(id a, const char *b) { return a && MSG(bool, id)(a, sel("isEqualToString:"), nsstr(b)); }

// ---------- wifid ----------
static unsigned long (*CFGetTypeID)(void *), (*CFDataGetTypeID)(void), (*CFDictionaryGetTypeID)(void);
static void *(*CFDictionaryGetValue)(void *, const void *), *(*CFDictionaryCreateMutableCopy)(void *, long, void *);
static void *(*CFDataCreate)(void *, const uint8_t *, long), *(*CFStringCreateWithCString)(void *, const char *, uint32_t);
static void (*CFDictionarySetValue)(void *, const void *, const void *), (*CFRelease)(void *);
static bool (*CFEqual)(void *, void *);
static void *kPM, *kPMValue;
struct pref { bool on; void *mac; }; // wifid 內部的「UI 設定」：開關＋位址（CFData）
static void (*origUpdate)(void *, void *, struct pref *);
static void hookUpdate(void *mgr, void *net, struct pref *p) {
    void *dict = net ? *(void **)((char *)net + 0x10) : 0; // WiFiNetwork 的屬性字典（可變）
    if (dict && p && p->on && p->mac && CFGetTypeID(p->mac) == CFDataGetTypeID()) {
        void *d = CFDictionaryGetValue(dict, kPM);
        void *v = d && CFGetTypeID(d) == CFDictionaryGetTypeID() ? CFDictionaryGetValue(d, kPMValue) : 0;
        if (v && !CFEqual(v, p->mac)) {
            static const uint8_t zero[6];
            void *md = CFDictionaryCreateMutableCopy(0, 0, d), *z = CFDataCreate(0, zero, 6);
            CFDictionarySetValue(md, kPMValue, z);
            CFDictionarySetValue(dict, kPM, md);
            CFRelease(z);
            CFRelease(md);
        }
    }
    origUpdate(mgr, net, p);
}
static void initWifid(void) {
    // wifid 18H17 的 __WiFiManagerUpdateNetworkWithPrivateMacInfo 開頭 0x60 位元組
    static const uint8_t expect[0x60] = {
        0x7f, 0x23, 0x03, 0xd5, 0xff, 0x43, 0x02, 0xd1, 0xfc, 0x6f, 0x03, 0xa9, 0xfa, 0x67, 0x04, 0xa9, 0xf8, 0x5f, 0x05, 0xa9,
        0xf6, 0x57, 0x06, 0xa9, 0xf4, 0x4f, 0x07, 0xa9, 0xfd, 0x7b, 0x08, 0xa9, 0xfd, 0x03, 0x02, 0x91, 0xf7, 0x03, 0x02, 0xaa,
        0xf5, 0x03, 0x01, 0xaa, 0xfc, 0x03, 0x00, 0xaa, 0x6a, 0x43, 0x04, 0x94, 0xf4, 0x03, 0x00, 0xaa, 0xa8, 0x0c, 0x00, 0x90,
        0x08, 0x15, 0x42, 0xf9, 0x18, 0x01, 0x40, 0xf9, 0xbd, 0x3c, 0x04, 0x94, 0xe0, 0x03, 0x18, 0xaa, 0x6b, 0x3d, 0x04, 0x94,
        0xfb, 0x03, 0x00, 0xaa, 0x13, 0x00, 0x80, 0x52, 0xb5, 0x26, 0x00, 0xb4, 0x97, 0x26, 0x00, 0xb4 };
    intptr_t (*slide)(uint32_t);
    if (!SYM(slide, "_dyld_get_image_vmaddr_slide") || !hookFn) return;
    uint8_t *fn = (uint8_t *)(0x10008dd10 + slide(0));
    for (int i = 0; i < 0x60; i++)
        if (fn[i] != expect[i]) return; // 不是 14.8 (18H17) 的 wifid，不裝
    SYM(CFGetTypeID, "CFGetTypeID"); SYM(CFDataGetTypeID, "CFDataGetTypeID"); SYM(CFDictionaryGetTypeID, "CFDictionaryGetTypeID");
    SYM(CFDictionaryGetValue, "CFDictionaryGetValue"); SYM(CFDictionaryCreateMutableCopy, "CFDictionaryCreateMutableCopy");
    SYM(CFDataCreate, "CFDataCreate"); SYM(CFStringCreateWithCString, "CFStringCreateWithCString");
    SYM(CFDictionarySetValue, "CFDictionarySetValue"); SYM(CFRelease, "CFRelease"); SYM(CFEqual, "CFEqual");
    kPM = CFStringCreateWithCString(0, "PRIVATE_MAC_ADDRESS", 0x08000100);
    kPMValue = CFStringCreateWithCString(0, "PRIVATE_MAC_ADDRESS_VALUE", 0x08000100);
    hookFn(fn, (void *)hookUpdate, (void **)&origUpdate);
#ifndef __arm64e__
    *(uintptr_t *)&origUpdate &= 0x7fffffffffULL; // arm64 版：去掉 PAC 位元（呼叫時不驗證）
#endif
}

// ---------- SpringBoard／設定 App 共用 ----------
static void *(*wifiCreate)(void *, int);
static int (*setPref)(void *, id, id, bool);
static void (*randomBuf)(void *, size_t);
static void setMac(id ssid, const uint8_t m[6]) {
    static void *client;
    if (!client) client = wifiCreate(0, 0);
    setPref(client, ssid, MSG(id, const void *, unsigned long)(getClass("NSData"), sel("dataWithBytes:length:"), m, 6), true);
}
static void randomMac(uint8_t m[6]) { randomBuf(m, 6); m[0] = (m[0] & 0xfc) | 0x02; } // 本機管理、單播
static id prefs(void) {
    static id d;
    if (!d) d = MSG(id, id)(MSG(id)(getClass("NSUserDefaults"), sel("alloc")), sel("initWithSuiteName:"), nsstr("com.neo.privatemac"));
    MSG(bool)(d, sel("synchronize")); // 另一個處理程序改過時重新讀
    return d;
}
static id rotateList(void) {
    id a = MSG(id, id)(prefs(), sel("arrayForKey:"), nsstr("rotate"));
    return a ? a : MSG(id)(getClass("NSArray"), sel("array"));
}
static bool rotates(id ssid) { return ssid && MSG(bool, id)(rotateList(), sel("containsObject:"), ssid); }
static void setRotate(id ssid, bool on) {
    if (!ssid || rotates(ssid) == on) return;
    id a = MSG(id)(MSG(id)(rotateList(), sel("mutableCopy")), sel("autorelease"));
    if (on) MSG(void, id)(a, sel("addObject:"), ssid);
    else MSG(void, id)(a, sel("removeObject:"), ssid);
    MSG(void, id, id)(prefs(), sel("setObject:forKey:"), a, nsstr("rotate"));
    MSG(bool)(prefs(), sel("synchronize"));
}

// ---------- SpringBoard：離開輪替的網路就換好下一次的位址 ----------
#define ROTATE_MIN_INTERVAL 1800 // 同一個網路最多每 30 分鐘換一次，避免一直掉線重連時狂觸發同步 IPC 卡死主執行緒
static id lastSSID;
static id rotateTimes; // SSID -> NSNumber(上次換位址的 unix 秒數)
static void (*origLink)(id, SEL);
static void hookLink(id self, SEL _cmd) {
    origLink(self, _cmd);
    id now = MSG(bool)(self, sel("isAssociated")) ? MSG(id)(self, sel("currentNetworkName")) : 0;
    if (lastSSID && !(now && MSG(bool, id)(lastSSID, sel("isEqualToString:"), now))) {
        if (rotates(lastSSID)) {
            if (!rotateTimes) rotateTimes = MSG(id)(getClass("NSMutableDictionary"), sel("new"));
            long t = time(0);
            id lv = MSG(id, id)(rotateTimes, sel("objectForKey:"), lastSSID);
            if (!lv || t - MSG(long)(lv, sel("longValue")) >= ROTATE_MIN_INTERVAL) {
                uint8_t m[6];
                randomMac(m);
                setMac(lastSSID, m);
                MSG(void, id, id)(rotateTimes, sel("setObject:forKey:"),
                    MSG(id, long)(getClass("NSNumber"), sel("numberWithLong:"), t), lastSSID);
            }
        }
        MSG(void)(lastSSID, sel("release"));
        lastSSID = 0;
    }
    if (now && !lastSSID) lastSSID = MSG(id)(now, sel("copy"));
}

// ---------- 設定 App：WiFi 詳情頁 ----------
static const char *modeName[] = { "關閉", "固定", "輪替" };
static id curVC, curAlert;
static int pendingMode;
static id ssidOf(id vc) { return MSG(id)(MSG(id)(vc, sel("config")), sel("ssid")); }
static int modeOf(id vc) { return !MSG(bool)(vc, sel("randomMACSwitchOn")) ? 0 : rotates(ssidOf(vc)) ? 2 : 1; }
static void reload(id vc) { MSG(void)(MSG(id)(vc, sel("tableView")), sel("reloadData")); }
static id idxPath(long row) { return MSG(id, long, long)(getClass("NSIndexPath"), sel("indexPathForRow:inSection:"), row, 0); }

static void applyMode(void *unused) {
    id vc = curVC;
    int m = pendingMode;
    setRotate(ssidOf(vc), m == 2);
    if ((m != 0) != MSG(bool)(vc, sel("randomMACSwitchOn"))) { // 開／關照原版開關的流程走（含中斷連線的提示）
        id sw = MSG(id)(vc, sel("randomMACSwitch"));
        MSG(void, bool)(sw, sel("setOn:"), m != 0);
        MSG(void, id)(vc, sel("_usingRandomMACSwitchChanged:"), sw);
    }
    reload(vc);
}

// 選單頁（關閉／固定／輪替）
static long modeRows(id self, SEL _cmd, id tv, long s) { return 3; }
static id modeCell(id self, SEL _cmd, id tv, id ip) {
    long row = MSG(long)(ip, sel("row"));
    id c = MSG(id)(MSG(id, long, id)(MSG(id)(getClass("UITableViewCell"), sel("alloc")), sel("initWithStyle:reuseIdentifier:"), 0, 0), sel("autorelease"));
    MSG(void, id)(MSG(id)(c, sel("textLabel")), sel("setText:"), nsstr(modeName[row]));
    MSG(void, long)(c, sel("setAccessoryType:"), row == modeOf(curVC) ? 3 : 0); // 3 = 打勾
    return c;
}
static id modeFooter(id self, SEL _cmd, id tv, long s) {
    return nsstr("關閉：使用此裝置的硬體 Wi-Fi 位址。\n固定：此網路一直使用同一個專用位址。\n輪替：每次連上此網路都換一個新的專用位址（離開網路時更換）。");
}
static void modeSelect(id self, SEL _cmd, id tv, id ip) {
    pendingMode = (int)MSG(long)(ip, sel("row"));
    MSG(id, bool)(MSG(id)(self, sel("navigationController")), sel("popViewControllerAnimated:"), true);
    dispatch_after_f(dispatch_time(0, 400000000), &_dispatch_main_q, 0, applyMode); // 回到詳情頁後再切，提示框才看得到
}
static void pushModes(id vc) {
    id m = MSG(id, long)(MSG(id)(getClass("PMModeController"), sel("alloc")), sel("initWithStyle:"), 2); // insetGrouped
    MSG(void, id)(m, sel("setTitle:"), nsstr("專用 Wi-Fi 位址"));
    MSG(void, id, bool)(MSG(id)(vc, sel("navigationController")), sel("pushViewController:animated:"), m, true);
    MSG(void)(m, sel("release"));
}

// 編輯位址
static int hexv(char ch) {
    int c = ch | 32;
    return ch >= '0' && ch <= '9' ? ch - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
// 輸入框每次修改後重排：只留 16 進位、轉大寫、最多 12 位，每兩位補「:」。
// old 從 loc 起 len 個字被換成 repl；只刪掉「:」時連前一位一起刪（不然 backspace 會卡在冒號）
static void formatMac(const char *old, size_t loc, size_t len, const char *repl, char out[18]) {
    static const char H[] = "0123456789ABCDEF";
    size_t ol = strlen(old);
    if (loc > ol) loc = ol;
    if (len > ol - loc) len = ol - loc;
    bool onlyColon = !*repl && len > 0;
    for (size_t i = loc; i < loc + len; i++)
        if (hexv(old[i]) >= 0) onlyColon = false;
    char d[12];
    int n = 0, before = 0;
    const char *part[3] = { old, repl, old + loc + len };
    size_t plen[3] = { loc, strlen(repl), ol - loc - len };
    for (int p = 0; p < 3; p++)
        for (size_t i = 0; i < plen[p]; i++) {
            int v = hexv(part[p][i]);
            if (v < 0 || n == 12) continue; // 非法字元、超過 12 位都丟掉
            d[n++] = H[v];
            if (p == 0) before = n;
        }
    if (onlyColon && before > 0) {
        for (int i = before - 1; i < n - 1; i++) d[i] = d[i + 1];
        n--;
    }
    int o = 0;
    for (int i = 0; i < n; i++) {
        if (i && i % 2 == 0) out[o++] = ':';
        out[o++] = d[i];
    }
    if (n && n % 2 == 0 && n < 12) out[o++] = ':';
    out[o] = 0;
}
typedef struct { unsigned long loc, len; } NSRange;
static bool macShouldChange(id self, SEL _cmd, id tf, NSRange r, id s) {
    const char *old = MSG(const char *)(MSG(id)(tf, sel("text")), sel("UTF8String"));
    const char *rep = MSG(const char *)(s, sel("UTF8String"));
    char out[18];
    formatMac(old ? old : "", r.loc, r.len, rep ? rep : "", out); // 內容只會是 ASCII，UTF-16 位置＝位元組位置
    MSG(void, id)(tf, sel("setText:"), nsstr(out));
    return false;
}
static bool parseMac(const char *s, uint8_t m[6]) {
    int n = 0, any = 0;
    for (; *s; s++) {
        int v = hexv(*s);
        if (v < 0) {
            if (*s == ':' || *s == '-' || *s == ' ') continue;
            return false;
        }
        if (n == 12) return false;
        m[n / 2] = n % 2 ? m[n / 2] | v : v << 4;
        any |= v;
        n++;
    }
    return n == 12 && any && !(m[0] & 1); // 不能是全 0 或群播位址
}
static void alertOK(const char *title, const char *msg) {
    id a = MSG(id, id, id, long)(getClass("UIAlertController"), sel("alertControllerWithTitle:message:preferredStyle:"), nsstr(title), nsstr(msg), 1);
    MSG(void, id)(a, sel("addAction:"), MSG(id, id, long, void *)(getClass("UIAlertAction"), sel("actionWithTitle:style:handler:"), nsstr("好"), 1, 0));
    MSG(void, id, bool, void *)(curVC, sel("presentViewController:animated:completion:"), a, true, 0);
}
static void applyMac(const uint8_t m[6]) {
    id vc = curVC, ssid = ssidOf(vc);
    setRotate(ssid, false); // 手動指定的位址＝固定
    setMac(ssid, m);
    char s[18];
    snprintf(s, sizeof s, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    MSG(void, id)(vc, sel("setRandomMACAddress:"), nsstr(s));
    reload(vc);
    if (MSG(bool)(MSG(id)(vc, sel("config")), sel("current"))) // 正在用這個網路：斷線讓它用新位址自動重連
        MSG(void)(MSG(id)(MSG(id)(getClass("WFClient"), sel("sharedInstance")), sel("interface")), sel("disassociateFromCurrentNetwork"));
}
static void editSave(id action) {
    id tf = MSG(id)(MSG(id)(curAlert, sel("textFields")), sel("firstObject"));
    const char *t = MSG(const char *)(MSG(id)(tf, sel("text")), sel("UTF8String"));
    uint8_t m[6];
    if (t && parseMac(t, m)) applyMac(m);
    else alertOK("位址格式錯誤", "請輸入 12 個 16 進位數字，例如 02:11:22:33:44:55（第一組不能是奇數）。");
}
static void editRandom(id action) {
    uint8_t m[6];
    randomMac(m);
    applyMac(m);
}
static void editField(id tf) {
    MSG(void, id)(tf, sel("setText:"), MSG(id)(curVC, sel("randomMACAddress")));
    MSG(void, id)(tf, sel("setPlaceholder:"), nsstr("02:11:22:33:44:55"));
    MSG(void, long)(tf, sel("setAutocapitalizationType:"), 3); // 全大寫
    MSG(void, long)(tf, sel("setAutocorrectionType:"), 1);     // 不自動修正
    MSG(void, long)(tf, sel("setClearButtonMode:"), 3);
    MSG(void, long)(tf, sel("setKeyboardType:"), 1);           // 英數鍵盤
    static id fieldDelegate;
    if (!fieldDelegate) fieldDelegate = MSG(id)(MSG(id)(getClass("PMMacFieldDelegate"), sel("alloc")), sel("init"));
    MSG(void, id)(tf, sel("setDelegate:"), fieldDelegate);
}
static void showEdit(id vc) {
    bool cur = MSG(bool)(MSG(id)(vc, sel("config")), sel("current"));
    id a = MSG(id, id, id, long)(getClass("UIAlertController"), sel("alertControllerWithTitle:message:preferredStyle:"), nsstr("Wi-Fi 位址"),
        nsstr(cur ? "儲存後此網路會改成「固定」，並重新連線一次。" : "儲存後此網路會改成「固定」。"), 1);
    MSG(void, void *)(a, sel("addTextFieldWithConfigurationHandler:"), (void *)^(id tf) { editField(tf); });
    id (*act)(id, SEL, id, long, void *) = (id (*)(id, SEL, id, long, void *))msgSend;
    id cls = getClass("UIAlertAction");
    MSG(void, id)(a, sel("addAction:"), act(cls, sel("actionWithTitle:style:handler:"), nsstr("儲存"), 0, (void *)^(id x) { editSave(x); }));
    MSG(void, id)(a, sel("addAction:"), act(cls, sel("actionWithTitle:style:handler:"), nsstr("隨機產生"), 0, (void *)^(id x) { editRandom(x); }));
    MSG(void, id)(a, sel("addAction:"), act(cls, sel("actionWithTitle:style:handler:"), nsstr("取消"), 1, 0));
    curAlert = a;
    MSG(void, id, bool, void *)(vc, sel("presentViewController:animated:completion:"), a, true, 0);
}

// 詳情頁 hook
static id (*origCell)(id, SEL, id, id);
static id hookCell(id self, SEL _cmd, id tv, id ip) {
    id c = origCell(self, _cmd, tv, ip);
    id rid = MSG(id)(c, sel("reuseIdentifier"));
    if (streq(rid, "kWFRandomMACSwitchCellIdentifier")) { // 開關列 → 「專用 Wi-Fi 位址　輪替 >」
        id title = MSG(id)(MSG(id)(c, sel("textLabel")), sel("text"));
        c = MSG(id)(MSG(id, long, id)(MSG(id)(getClass("UITableViewCell"), sel("alloc")), sel("initWithStyle:reuseIdentifier:"), 1, nsstr("PMModeCell")), sel("autorelease"));
        MSG(void, id)(MSG(id)(c, sel("textLabel")), sel("setText:"), title ? title : nsstr("專用 Wi-Fi 位址"));
        MSG(void, id)(MSG(id)(c, sel("detailTextLabel")), sel("setText:"), nsstr(modeName[modeOf(self)]));
        MSG(void, long)(c, sel("setAccessoryType:"), 1); // >
    } else if (streq(rid, "kWFRandomMACInfoCellIdentifier") && MSG(bool)(self, sel("randomMACSwitchOn")))
        MSG(void, long)(c, sel("setSelectionStyle:"), 3); // 可以點
    return c;
}
static void (*origSelect)(id, SEL, id, id);
static void hookSelect(id self, SEL _cmd, id tv, id ip) {
    id rid = MSG(id)(MSG(id, id)(tv, sel("cellForRowAtIndexPath:"), ip), sel("reuseIdentifier"));
    bool mode = streq(rid, "PMModeCell"), edit = streq(rid, "kWFRandomMACInfoCellIdentifier") && MSG(bool)(self, sel("randomMACSwitchOn"));
    if (!mode && !edit) {
        origSelect(self, _cmd, tv, ip);
        return;
    }
    MSG(void, id, bool)(tv, sel("deselectRowAtIndexPath:animated:"), ip, true);
    curVC = self;
    if (mode) pushModes(self);
    else showEdit(self);
}
static id (*origFooter)(id, SEL);
static id hookFooter(id self, SEL _cmd) {
    id t = origFooter(self, _cmd);
    if (!rotates(ssidOf(self)) || !MSG(bool)(self, sel("randomMACSwitchOn"))) return t;
    return MSG(id, id)(t ? t : nsstr(""), sel("stringByAppendingString:"), nsstr("\n輪替專用位址會在每次連上此網路時更換此裝置的 Wi-Fi 位址來減少追蹤。"));
}

static void initSettings(void) {
    dlopen("/System/Library/PrivateFrameworks/WiFiKit.framework/WiFiKit", RTLD_NOW);
    id vc = getClass("WFNetworkSettingsViewController");
    if (!vc) return;
    id (*alloc)(id, const char *, size_t); void (*reg)(id); bool (*add)(id, SEL, void *, const char *);
    SYM(alloc, "objc_allocateClassPair"); SYM(reg, "objc_registerClassPair"); SYM(add, "class_addMethod");
    id m = alloc(getClass("UITableViewController"), "PMModeController", 0);
    add(m, sel("tableView:numberOfRowsInSection:"), (void *)modeRows, "q@:@q");
    add(m, sel("tableView:cellForRowAtIndexPath:"), (void *)modeCell, "@@:@@");
    add(m, sel("tableView:titleForFooterInSection:"), (void *)modeFooter, "@@:@q");
    add(m, sel("tableView:didSelectRowAtIndexPath:"), (void *)modeSelect, "v@:@@");
    reg(m);
    id fd = alloc(getClass("NSObject"), "PMMacFieldDelegate", 0);
    add(fd, sel("textField:shouldChangeCharactersInRange:replacementString:"), (void *)macShouldChange, "B@:@{_NSRange=QQ}@");
    reg(fd);
    hookMsg(vc, sel("tableView:cellForRowAtIndexPath:"), (void *)hookCell, (void **)&origCell);
    hookMsg(vc, sel("tableView:didSelectRowAtIndexPath:"), (void *)hookSelect, (void **)&origSelect);
    hookMsg(vc, sel("_randomMACFooterTextToDisplay"), (void *)hookFooter, (void **)&origFooter);
}

__attribute__((constructor)) static void init(void) {
    SYM(getClass, "objc_getClass");
    SYM(sel, "sel_registerName");
    msgSend = dlsym(RTLD_DEFAULT, "objc_msgSend");
    // Substitute 越獄：MSHook* 在 libsubstrate.dylib（相容層），要先 dlopen
    const char *libs[] = { "/usr/lib/libsubstrate.dylib", "/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate" };
    for (int i = 0; i < 2 && !hookFn; i++)
        if (dlopen(libs[i], RTLD_NOW)) { SYM(hookFn, "MSHookFunction"); SYM(hookMsg, "MSHookMessageEx"); }
    const char *(*prog)(void);
    if (!SYM(prog, "getprogname") || !hookMsg) return;
    const char *p = prog();
    if (!strcmp(p, "wifid")) return initWifid();
    dlopen("/System/Library/PrivateFrameworks/MobileWiFi.framework/MobileWiFi", RTLD_NOW);
    if (!SYM(wifiCreate, "WiFiManagerClientCreate") || !SYM(setPref, "WiFiManagerClientSetPrivateMacPrefForScanRecord") ||
        !SYM(randomBuf, "arc4random_buf"))
        return;
    if (!strcmp(p, "SpringBoard")) hookMsg(getClass("SBWiFiManager"), sel("_linkDidChange"), (void *)hookLink, (void **)&origLink);
    else if (!strcmp(p, "Preferences")) initSettings();
}

#ifdef PM_TEST // build.sh 先在手機上跑這個自我檢查
int main(void) {
    uint8_t m[6];
    int ok = parseMac("02:11:22:33:44:5f", m) && m[0] == 0x02 && m[5] == 0x5f && parseMac("0A-11-22-33-44-55", m) && m[0] == 0x0a &&
             parseMac("021122334455", m) && !parseMac("01:11:22:33:44:55", m) && !parseMac("00:00:00:00:00:00", m) &&
             !parseMac("02:11:22:33:44", m) && !parseMac("02:11:22:33:44:55:66", m) && !parseMac("02:11:22:33:44:5g", m);
    char o[18];
#define F(old, loc, len, rep, want) (formatMac(old, loc, len, rep, o), !strcmp(o, want))
    ok = ok && F("", 0, 0, "a", "A") && F("A", 1, 0, "b", "AB:") && F("AB:", 2, 1, "", "A") && F("AB:C", 3, 1, "", "AB:") &&
         F("AB:CD:EF:01:23:45", 17, 0, "6", "AB:CD:EF:01:23:45") && F("", 0, 0, "zz-0a:1b", "0A:1B:") && F("AB:", 3, 0, "g", "AB:") &&
         F("AB:CD:EF", 0, 8, "", "") && F("AB:CD", 2, 1, "", "AC:D");
    return ok ? 0 : 1;
}
#endif
