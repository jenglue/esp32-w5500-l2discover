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
void parseLLDP(uint8_t* d, int l);
void parseCDP(uint8_t* d, int l);

// --- 全域對象 ---
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
BLECharacteristic *pCharacteristic;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x0F };

// --- 狀態變數 ---
char swName[32] = "Searching...";
char swPort[32] = "Waiting LLDP/CDP...";
int currentPage = 0; // 0:L2, 1:IP, 2:Traffic, 3:QR
uint32_t packetCount = 0;
String dashboardURL = "https://your-url-here.io"; // 稍後替換為你的網址

// --- DHCP 背景處理 ---
bool dhcpObtained = false;
unsigned long dhcpLastAttempt = 0;
const unsigned long DHCP_RETRY_INTERVAL = 5000; // 5 秒重試

// --- 輔助函式：顯示 QR Code ---
void drawQRCode(const char* url) {
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, 0, url);

    int scale = 2; // 放大兩倍方便掃描
    int offset_x = (128 - qrcode.size * scale) / 2;
    int offset_y = (64 - qrcode.size * scale) / 2;

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
    Serial.println("[ETH] W5500 initialized (MAC only, DHCP deferred)");

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
}

void loop() {
    // 1. 按鈕換頁
    if (digitalRead(BTN_PIN) == LOW) {
        currentPage = (currentPage + 1) % 4;
        delay(250); 
    }

    // 2. 背景 DHCP 處理
    if (!dhcpObtained && millis() - dhcpLastAttempt > DHCP_RETRY_INTERVAL) {
        dhcpLastAttempt = millis();
        Serial.println("[DHCP] Attempting...");

        // 暫時關閉 MACRAW socket 以進行 DHCP
        w5500.execCmdSn(0, Sock_CLOSE);
        w5500.writeSnIR(0, 0xFF);

        if (Ethernet.begin(mac) != 0) {
            dhcpObtained = true;
            Serial.print("[DHCP] OK  IP: ");
            Serial.println(Ethernet.localIP());
            Serial.print("[DHCP] GW: ");
            Serial.println(Ethernet.gatewayIP());
        } else {
            Serial.println("[DHCP] Failed, will retry...");
        }

        // 重新開啟 MACRAW socket
        w5500.execCmdSn(0, Sock_CLOSE);
        w5500.writeSnIR(0, 0xFF);
        w5500.writeSnMR(0, SnMR::MACRAW);
        w5500.execCmdSn(0, Sock_OPEN);
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

void drawUI() {
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
        u8g2.drawStr(0, 28, "SW:"); u8g2.setCursor(20, 40); u8g2.print(swName);
        u8g2.drawStr(0, 52, "PT:"); u8g2.setCursor(20, 64); u8g2.print(swPort);
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
        u8g2.drawStr(0, 8, "WEB DASHBOARD");
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
        if (type == 5) { memcpy(swName, d + p + 2, min(len, 30)); swName[min(len, 30)] = '\0'; }
        if (type == 7) { memcpy(swPort, d + p + 2, min(len, 30)); swPort[min(len, 30)] = '\0'; }
        p += (len + 2);
    }
}
void parseCDP(uint8_t* d, int l) {
    int p = 22;
    while (p < l - 4) {
        uint16_t type = (d[p] << 8) | d[p+1]; uint16_t len = (d[p+2] << 8) | d[p+3];
        if (type == 0x0001) { memcpy(swName, d + p + 4, min(len-4, 30)); swName[min(len-4, 30)] = '\0'; }
        if (type == 0x0003) { memcpy(swPort, d + p + 4, min(len-4, 30)); swPort[min(len-4, 30)] = '\0'; }
        p += len;
    }
}