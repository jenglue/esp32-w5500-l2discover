#include <SPI.h>
#include <Ethernet3.h>
#include <U8g2lib.h>
#include <utility/w5500.h>
#include <qrcode.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// --- LilyGo W5500-Lite 硬體定義 (參考官方範例) ---
#define W5500_CS    5
#define W5500_RST   4
#define W5500_SCLK  18
#define W5500_MISO  23
#define W5500_MOSI  19
#define OLED_SDA    21
#define OLED_SCL    22
#define BTN_PIN     0

// --- BLE UUIDs ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// --- 前向宣告 ---
void updateBLE();
void drawUI();
void drawMarquee(int x, int y, int maxWidth, const char* text, int &scrollOffset);
bool isMarqueeCycleDone(const char* text, int scrollOffset);
void parseLLDP(uint8_t* d, int l);
void parseCDP(uint8_t* d, int l);

// --- 全域對象 ---
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
BLECharacteristic *pCharacteristic;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x0F };

// --- 狀態變數 ---
char swName[64] = "Searching...";
char swPort[64] = "Waiting LLDP/CDP...";
char swVlan[16] = "N/A";
int currentPage = 0; // 0:L2, 1:IP, 2:Traffic, 3:QR
uint32_t packetCount = 0;
String dashboardURL = "https://your-url-here.io"; // 稍後替換為你的網址

// --- 跑馬燈狀態 ---
int scrollOffsetName = 0;
int scrollOffsetPort = 0;
unsigned long lastScrollTime = 0;
const unsigned long SCROLL_INTERVAL = 80; // 每 80ms 移動 (快速跑馬燈)
const int CHAR_WIDTH = 6; // u8g2_font_6x12_tf 字元寬度
const int DISPLAY_TEXT_WIDTH = 98; // 可顯示寬度 (扣除左側標籤和右側頁碼)

// --- 按鈕消抖狀態 ---
bool lastBtnState = HIGH;
unsigned long lastBtnChangeTime = 0;
const unsigned long BTN_DEBOUNCE_MS = 200;
unsigned long pageEnteredTime = 0;
bool hasDiscoveryData = false;
bool marqueeFirstCycleDone = false;
unsigned long marqueePauseStart = 0;
const unsigned long PAGE_IP_DISPLAY_TIME = 10000;  // 第二頁顯示 10 秒
const unsigned long MARQUEE_PAUSE_TIME = 2000;     // 跑馬燈完成後暫停 2 秒
const unsigned long NO_DATA_SWITCH_TIME = 10000;   // 無資料時 10 秒切換

// --- DHCP ---
bool dhcpObtained = false;

// --- 輔助函式：顯示 QR Code (右對齊，避免與標題重疊) ---
void drawQRCode(const char* url) {
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, 0, url);

    int scale = 2; // 放大兩倍方便掃描
    int qrWidth = qrcode.size * scale;
    int qrHeight = qrcode.size * scale;
    int offset_x = 120 - qrWidth; // 靠右但留空間給頁碼指示 (122-125)
    int offset_y = (64 - qrHeight) / 2; // 垂直置中

    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                u8g2.drawBox(offset_x + x * scale, offset_y + y * scale, scale, scale);
            }
        }
    }
}

// --- BLE 初始化 ---
void initBLE() {
    BLEDevice::init("T-Lite-Sniffer");
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
                      );
    pService->start();
    BLEDevice::getAdvertising()->start();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("===== ESP32 W5500-Lite L2 Discover =====");

    // OLED 初始化
    Serial.print("[OLED] Init (SDA=");
    Serial.print(OLED_SDA);
    Serial.print(", SCL=");
    Serial.print(OLED_SCL);
    Serial.println(")...");
    pinMode(BTN_PIN, INPUT_PULLUP);
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, 12, "Booting...");
    u8g2.sendBuffer();
    Serial.println("[OLED] OK");

    // W5500 硬體重置 (參考官方範例)
    Serial.println("[ETH] Resetting W5500...");
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, HIGH);
    delay(250);
    digitalWrite(W5500_RST, LOW);
    delay(50);
    digitalWrite(W5500_RST, HIGH);
    delay(350);

    // 初始化 SPI (依官方範例明確指定腳位)
    Serial.print("[SPI] Begin (SCLK=");
    Serial.print(W5500_SCLK);
    Serial.print(", MISO=");
    Serial.print(W5500_MISO);
    Serial.print(", MOSI=");
    Serial.print(W5500_MOSI);
    Serial.println(")");
    SPI.begin(W5500_SCLK, W5500_MISO, W5500_MOSI);

    // 初始化 Ethernet (僅設定 MAC，不阻塞 DHCP)
    Serial.print("[ETH] Init CS=");
    Serial.println(W5500_CS);
    Ethernet.setCsPin(W5500_CS);
    Ethernet.init(1); // 1 socket = 16K buffer for MACRAW

    // 以靜態 IP 0.0.0.0 初始化，僅寫入 MAC 地址到 W5500
    IPAddress ip(0, 0, 0, 0);
    Ethernet.begin(mac, ip);
    Serial.println("[ETH] W5500 initialized (MAC set)");

    // 嘗試 DHCP (在 setup 中執行，阻塞無妨)
    Serial.println("[DHCP] Attempting...");
    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "DHCP requesting...");
    u8g2.sendBuffer();
    // 重新初始化以啟動 DHCP
    if (Ethernet.begin(mac) != 0) {
        dhcpObtained = true;
        Serial.print("[DHCP] OK  IP: ");
        Serial.println(Ethernet.localIP());
    } else {
        Serial.println("[DHCP] Failed, continuing without IP");
    }

    // 立即開啟 Socket 0 MACRAW 模式，L2 偵測不需要 IP
    Serial.println("[ETH] Opening MACRAW socket...");
    w5500.execCmdSn(0, Sock_CLOSE);
    w5500.writeSnIR(0, 0xFF);
    w5500.writeSnMR(0, SnMR::MACRAW);
    w5500.execCmdSn(0, Sock_OPEN);
    uint8_t sr = w5500.readSnSR(0);
    Serial.print("[ETH] Socket 0 status: 0x");
    Serial.println(sr, HEX);
    Serial.println(sr == SnSR::MACRAW ? "[ETH] MACRAW OK" : "[ETH] MACRAW FAILED");

    // BLE 初始化
    Serial.println("[BLE] Init...");
    initBLE();
    Serial.println("[BLE] OK");

    Serial.println("===== Setup Complete =====");
    Serial.println("[INFO] L2 sniffing active, DHCP will run in background");

    pageEnteredTime = millis(); // 初始化換頁計時基準
}

void loop() {
    // 1. 按鈕換頁 (非阻塞消抖)
    bool btnState = digitalRead(BTN_PIN);
    if (btnState == LOW && lastBtnState == HIGH &&
        millis() - lastBtnChangeTime > BTN_DEBOUNCE_MS) {
        currentPage = (currentPage + 1) % 4;
        pageEnteredTime = millis();
        marqueeFirstCycleDone = false;
        scrollOffsetName = 0;
        scrollOffsetPort = 0;
    }
    if (btnState != lastBtnState) {
        lastBtnChangeTime = millis();
        lastBtnState = btnState;
    }

    // 自動換頁：第一頁至少 10 秒，跑馬燈完成後再停 2 秒；第二頁 10 秒回第一頁
    if (currentPage == 0) {
        bool timeReady = (millis() - pageEnteredTime >= NO_DATA_SWITCH_TIME);
        bool marqueeReady = true; // 無資料時預設就緒
        if (hasDiscoveryData) {
            // 偵測跑馬燈是否完成一輪
            if (!marqueeFirstCycleDone) {
                if (isMarqueeCycleDone(swName, scrollOffsetName) &&
                    isMarqueeCycleDone(swPort, scrollOffsetPort)) {
                    marqueeFirstCycleDone = true;
                    marqueePauseStart = millis();
                }
                marqueeReady = false; // 跑馬燈還沒完
            } else {
                // 跑馬燈完成，等待 2 秒暫停
                marqueeReady = (millis() - marqueePauseStart >= MARQUEE_PAUSE_TIME);
            }
        }
        // 兩個條件都滿足才切換
        if (timeReady && marqueeReady) {
            currentPage = 1;
            pageEnteredTime = millis();
        }
    } else if (currentPage == 1) {
        if (millis() - pageEnteredTime >= PAGE_IP_DISPLAY_TIME) {
            currentPage = 0;
            pageEnteredTime = millis();
            marqueeFirstCycleDone = false;
            scrollOffsetName = 0;
            scrollOffsetPort = 0;
        }
    }

    // 2. DHCP 維護 (非阻塞)
    if (dhcpObtained) {
        Ethernet.maintain();
    }

    // 3. 處理網路封包 (MACRAW 被動偵測)
    uint16_t rxSize = w5500.getRXReceivedSize(0);
    if (rxSize > 2) {
        uint8_t buffer[600];
        uint16_t readLen = (rxSize < sizeof(buffer)) ? rxSize : sizeof(buffer);
        w5500.recv_data_processing(0, buffer, readLen);
        w5500.execCmdSn(0, Sock_RECV);

        // MACRAW: 前 2 bytes 為封包長度，跳過取得 Ethernet frame
        uint8_t* frame = buffer + 2;
        int len = readLen - 2;

        packetCount++;
        Serial.print("[PKT] #");
        Serial.print(packetCount);
        Serial.print(" len=");
        Serial.println(len);

        for (int i = 0; i < len - 10; i++) {
            // LLDP (EtherType 0x88CC)
            if (frame[i] == 0x88 && frame[i+1] == 0xCC) {
                Serial.println("[PKT] LLDP detected!");
                parseLLDP(frame + i + 2, len - i - 2);
                Serial.print("[LLDP] Switch: ");
                Serial.print(swName);
                Serial.print(" Port: ");
                Serial.println(swPort);
                updateBLE();
            }
            // CDP (Dst MAC 01:00:0C)
            if (frame[i] == 0x01 && frame[i+1] == 0x00 && frame[i+2] == 0x0C) {
                Serial.println("[PKT] CDP detected!");
                parseCDP(frame + i, len - i);
                Serial.print("[CDP] Switch: ");
                Serial.print(swName);
                Serial.print(" Port: ");
                Serial.println(swPort);
                updateBLE();
            }
        }
    }

    // 4. UI 渲染
    drawUI();
}

void updateBLE() {
    String data = String(swName) + "|" + String(swPort);
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify();
}

// --- 跑馬燈判斷是否完成一輪 ---
bool isMarqueeCycleDone(const char* text, int scrollOffset) {
    int textWidth = strlen(text) * CHAR_WIDTH;
    if (textWidth <= DISPLAY_TEXT_WIDTH) return true; // 不需捲動
    int cycleWidth = textWidth + CHAR_WIDTH * 3; // 文字寬度 + 間隔
    return scrollOffset >= cycleWidth;
}

// --- 跑馬燈繪製輔助函式 (捲動一次後停止) ---
void drawMarquee(int x, int y, int maxWidth, const char* text, int &scrollOffset) {
    int textWidth = strlen(text) * CHAR_WIDTH;
    if (textWidth <= maxWidth) {
        // 文字夠短，直接顯示
        u8g2.setCursor(x, y);
        u8g2.print(text);
        scrollOffset = 0;
    } else {
        // 文字太長，做跑馬燈效果 (單次捲動)
        int gapWidth = CHAR_WIDTH * 3; // 間隔 3 個字元
        int cycleWidth = textWidth + gapWidth;
        int offset = min(scrollOffset, cycleWidth); // 限制不超過一輪
        // 使用 clip window 限制繪製範圍
        u8g2.setClipWindow(x, y - 12, x + maxWidth, y + 2);
        u8g2.setCursor(x - offset, y);
        u8g2.print(text);
        // 第二份副本，實現無縫捲動
        u8g2.setCursor(x - offset + cycleWidth, y);
        u8g2.print(text);
        u8g2.setMaxClipWindow(); // 恢復全螢幕繪製
    }
}

void drawUI() {
    // 更新跑馬燈偏移 (一輪完成後停止)
    if (!marqueeFirstCycleDone && millis() - lastScrollTime > SCROLL_INTERVAL) {
        lastScrollTime = millis();
        scrollOffsetName += 2; // 每次移動 2 像素
        scrollOffsetPort += 2;
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    
    // 側邊頁碼指示
    for(int i=0; i<4; i++) {
        if(i == currentPage) u8g2.drawBox(122, 10+(i*12), 4, 8);
        else u8g2.drawFrame(122, 10+(i*12), 4, 8);
    }

    if (currentPage == 0) { // L2 Topology
        u8g2.drawStr(0, 10, "1. TOPOLOGY");
        u8g2.drawHLine(0, 13, 118);
        u8g2.drawStr(0, 26, "SW:");
        drawMarquee(20, 26, DISPLAY_TEXT_WIDTH, swName, scrollOffsetName);
        u8g2.setFont(u8g2_font_6x12_tf);
        u8g2.drawStr(0, 40, "PT:");
        drawMarquee(20, 40, DISPLAY_TEXT_WIDTH, swPort, scrollOffsetPort);
        u8g2.setFont(u8g2_font_6x12_tf);
        u8g2.setCursor(0, 54); u8g2.print("VL: "); u8g2.print(swVlan);
    } 
    else if (currentPage == 1) { // IP Info
        u8g2.drawStr(0, 10, "2. IP STATUS");
        u8g2.drawHLine(0, 13, 118);
        if (dhcpObtained) {
            u8g2.setCursor(0, 28); u8g2.print("IP: "); u8g2.print(Ethernet.localIP().toString());
            u8g2.setCursor(0, 42); u8g2.print("GW: "); u8g2.print(Ethernet.gatewayIP().toString());
        } else {
            u8g2.drawStr(0, 28, "IP: DHCP pending...");
        }
        uint8_t phy = Ethernet.phyState();
        u8g2.setCursor(0, 58); u8g2.print("PHY: "); u8g2.print(Ethernet.link() ? "Link-Up" : "Link-Down");
        if (Ethernet.link()) { u8g2.print(" "); u8g2.print(Ethernet.speedReport()); }
    }
    else if (currentPage == 2) { // Traffic
        u8g2.drawStr(0, 10, "3. ANALYTICS");
        u8g2.drawHLine(0, 13, 118);
        u8g2.setCursor(0, 35); u8g2.print("Packets: "); u8g2.print(packetCount);
        u8g2.drawFrame(0, 50, 110, 10);
        u8g2.drawBox(2, 52, (packetCount % 106), 6);
    }
    else if (currentPage == 3) { // QR Code
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawStr(0, 8, "DASHBOARD");
        drawQRCode(dashboardURL.c_str());
    }

    u8g2.sendBuffer();
}

// --- 解析邏輯 ---
void parseLLDP(uint8_t* d, int l) {
    int p = 0;
    while (p < l - 2) {
        uint16_t tlv = (d[p] << 8) | d[p+1];
        int type = tlv >> 9; int len = tlv & 0x01FF;
        if (type == 5) { int n = min(len, 62); memcpy(swName, d + p + 2, n); swName[n] = '\0'; hasDiscoveryData = true; }
        if (type == 7) { int n = min(len, 62); memcpy(swPort, d + p + 2, n); swPort[n] = '\0'; }
        // IEEE 802.1 Organizationally Specific TLV: Port VLAN ID
        if (type == 127 && len >= 7) {
            // OUI: 00:80:C2, Subtype: 1 = Port VLAN ID
            if (d[p+2] == 0x00 && d[p+3] == 0x80 && d[p+4] == 0xC2 && d[p+5] == 0x01) {
                uint16_t vlanId = (d[p+6] << 8) | d[p+7];
                snprintf(swVlan, sizeof(swVlan), "%u", vlanId);
            }
        }
        p += (len + 2);
    }
}
void parseCDP(uint8_t* d, int l) {
    int p = 22;
    while (p < l - 4) {
        uint16_t type = (d[p] << 8) | d[p+1]; uint16_t len = (d[p+2] << 8) | d[p+3];
        if (len < 4) break; // 防止無限迴圈
        int dataLen = len - 4;
        if (type == 0x0001) { int n = min(dataLen, 62); memcpy(swName, d + p + 4, n); swName[n] = '\0'; hasDiscoveryData = true; }
        if (type == 0x0003) { int n = min(dataLen, 62); memcpy(swPort, d + p + 4, n); swPort[n] = '\0'; }
        if (type == 0x000A && dataLen >= 2) { // Native VLAN
            uint16_t vlanId = (d[p+4] << 8) | d[p+5];
            snprintf(swVlan, sizeof(swVlan), "%u", vlanId);
        }
        p += len;
    }
}