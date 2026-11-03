# PrivateMAC

為 iOS 14.8 加入新版 iOS 的「專用 Wi-Fi 位址」設定：每個網路可選擇**關閉／固定／輪替**，並可自訂 Wi-Fi 位址（MAC）。

## 功能

位於「設定 → Wi-Fi → 網路詳情 (i)」頁面：

- **專用 Wi-Fi 位址**：原本的開關改為選單。
  - **關閉**：使用硬體位址。
  - **固定**：此網路固定使用同一個專用位址，與 iOS 14 原本的行為相同。
  - **輪替**：每次連上此網路都使用新的專用位址。
- **Wi-Fi 位址**：模式為「固定」或「輪替」時，點選此列可以：
  - 輸入位址：只接受 16 進位字元，每兩位自動補上 `:`，最多 12 位。
  - 隨機產生位址：產生本機管理（locally administered）的單播位址。
  - 修改位址後，此網路會切換為「固定」。若目前正連著此網路，會先中斷，再以新位址自動重新連線。

## 系統需求

- iOS 14.8（18H17）。wifid 的 hook 位址只適用這個版本；版本不符時，wifid 這部分不會安裝。
- 只在 iPhone XS Max（iOS 14.8，unc0ver 越獄）上測試過。

## 運作方式

同一個 dylib 注入三個處理程序，分別負責：

| 處理程序 | 負責 |
|---|---|
| `wifid` | hook `__WiFiManagerUpdateNetworkWithPrivateMacInfo`，讓 wifid 接受新的專用位址 |
| SpringBoard | hook `-[SBWiFiManager _linkDidChange]`，離開設為「輪替」的網路時，預先換好下次連線的位址 |
| 設定 App | 修改 `WFNetworkSettingsViewController` 的表格：模式選單、位址編輯、輸入檢查 |

位址一律透過 MobileWiFi 的 `WiFiManagerClientSetPrivateMacPrefForScanRecord(client, SSID, 位址, 啟用)` 設定。

### wifid 的限制

wifid 收到 UI 傳來的新位址時，只有在該網路目前記錄的是硬體位址（第一個位元組的 bit1 為 0）才會接受；如果已經是專用位址，新位址會被直接捨棄。因此單靠上述 API，只能在專用位址從「關閉」改為「開啟」時指定位址。

本 tweak 在 wifid 處理前，先把該網路記錄中的舊位址暫時改為全 0，讓 wifid 接受新位址。之後由 wifid 依原本的流程，寫回快取和 `/var/preferences/SystemConfiguration/com.apple.wifi-private-mac-networks.plist`。

### API 參數

`WiFiManagerClientSetPrivateMacPrefForScanRecord` 的第二個參數必須是 SSID 字串（CFString），不能傳 `WiFiNetworkRef`。傳入 `WiFiNetworkRef` 時序列化會失敗，wifid 會直接忽略這個請求。

### 輪替時機

新位址在離開網路時預先產生，所以每次連線使用的位址都不同。這和新版 iOS 定期更換位址的方式不一樣。

wifid 本身也有輪替機制（`macRotationTimeout` 和各網路的 `ValidityInterval`），但 iOS 14 的預設週期是 10 年，而且是由定時清理觸發，不是在連線時觸發，所以本 tweak 沒有使用。

## 設定檔

設為「輪替」的網路記錄在 `com.neo.privatemac` 的 `rotate` 鍵，值是 SSID 陣列，檔案位於 `/var/mobile/Library/Preferences/com.neo.privatemac.plist`。

## 建置

直接在裝置上建置，不需要 iOS SDK：

```sh
sh build.sh
```

需要 `clang`、`ldid`、`dpkg-deb`、`lipo`。`build.sh` 會先編譯並執行位址解析和輸入格式的自我檢查，通過後才產生 `com.neo.privatemac_<版本>_iphoneos-arm.deb`。

- 程式是純 C，透過 Objective-C runtime 呼叫系統 API，其他函式在執行時以 `dlsym` 取得。
- 連結時使用 `libSystem.tbd`，只列出需要的符號；新增用到的 libSystem 符號時，要一併補進這個檔案。
- 輸出的 dylib 是 arm64＋arm64e 雙架構，因為目標處理程序都是 arm64e。

## 已知限制

- wifid 的 hook 只支援 iOS 14.8（18H17）。其他版本無法修改已啟用專用位址的網路。
- 在輸入框中間編輯時，游標會移到最後面。
- 「輪替」依賴 SpringBoard 收到斷線事件；SpringBoard 重新啟動時正在使用的那次連線，不會觸發更換。

## 致謝

- [MacSpoof](https://github.com/elihwyma/MacSpoof)：提供 WiFiKit 相關 API 的線索。本專案沒有使用它的程式碼。
