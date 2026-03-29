#include <SPI.h>
#include <Ethernet3.h>
#include <U8g2lib.h>
#include <utility/w5500.h>
#include <qrcode.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// --- T-ETH-Lite 硬體定義 ---
#define W5500_CS    2
#define W5500_RST   4
#define W5500_EN    12
#define OLED_SDA    13
#define OLED_SCL    15
#define BTN_PIN     0

// --- BLE UUIDs ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

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
    pinMode(BTN_PIN, INPUT_PULLUP);
    u8g2.begin();

    // 啟動 W5500 電源與重置
    pinMode(W5500_EN, OUTPUT);
    digitalWrite(W5500_EN, HIGH);
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, LOW); delay(100);
    digitalWrite(W5500_RST, HIGH); delay(500);

    // 初始化網路與藍牙
    Ethernet.init(W5500_CS);
    Ethernet.begin(mac, 1500); // 1.5秒 DHCP
    initBLE();
}

void loop() {
    // 1. 按鈕換頁
    if (digitalRead(BTN_PIN) == LOW) {
        currentPage = (currentPage + 1) % 4;
        delay(250); 
    }

    // 2. 處理網路封包 (被動偵測)
    int packetSize = Ethernet.parsePacket();
    if (packetSize > 0) {
        packetCount++;
        uint8_t buffer[512];
        int len = Ethernet.read(buffer, 512);
        
        for (int i = 0; i < len - 10; i++) {
            // LLDP (0x88CC)
            if (buffer[i] == 0x88 && buffer[i+1] == 0xCC) {
                parseLLDP(buffer + i + 2, len - i - 2);
                updateBLE();
            }
            // CDP (0x01000C)
            if (buffer[i] == 0x01 && buffer[i+1] == 0x00 && buffer[i+2] == 0x0C) {
                parseCDP(buffer + i, len - i);
                updateBLE();
            }
        }
    }

    // 3. UI 渲染
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
        u8g2.setCursor(0, 32); u8g2.print("IP: "); u8g2.print(Ethernet.localIP().toString());
        u8g2.setCursor(0, 48); u8g2.print("GW: "); u8g2.print(Ethernet.gatewayIP().toString());
        byte phy = w5500.getPHYCFGR();
        u8g2.setCursor(0, 64); u8g2.print("PHY: "); u8g2.print((phy & 0x08) ? "100M-F" : "Link-Down");
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