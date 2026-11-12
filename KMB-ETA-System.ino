#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>           // 💡 必須放在 LovyanGFX.hpp 之前
#include <LittleFS.h>     // 💡 必須放在 LovyanGFX.hpp 之前
#include <LovyanGFX.hpp>  // 💡 移到 LittleFS 之後
#include <SPI.h>
#include <time.h>
#include <vector>

using namespace fs;
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Preferences.h>
#include "icons.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
XPowersPMU power;

// ==========================================
// 💡 LovyanGFX 硬件配置封裝
// ==========================================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Touch_FT5x06 _touch_instance;

public:
  LGFX(void) {
    {  // SPI 顯示匯流排設定
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 5;   // SCLK
      cfg.pin_mosi = 1;   // MOSI
      cfg.pin_miso = -1;  // 無 MISO
      cfg.pin_dc = 3;     // DC
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {  // 面板設定
      auto cfg = _panel_instance.config();
      cfg.pin_cs = -1;   // 無 CS (硬體接地)
      cfg.pin_rst = -1;  // 無 RST (硬體接EN/延遲復位)
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }
    {  // I2C 觸控設定 (FT6336)
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = 4;  // INT
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = 8;  // SDA
      cfg.pin_scl = 7;  // SCL
      cfg.freq = 400000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
// ==========================================

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
const String AP_IP_STRING = "192.168.10.1";

String savedSSID = "";
String savedPass = "";

#define LCD_BL_PIN 6
const int PWM_FREQUENCY = 5000;

Preferences preferences;
int currentBrightness = 204;
unsigned long DATA_INTERVAL = 30000;

const String FIRMWARE_VERSION = "v1.02";

#define I2C_SDA 8
#define I2C_SCL 7
#define FT6336_I2C_ADDR 0x38
#define TOUCH_INT_PIN 4

bool lastTouchState = false;
bool apModeFromSettings = false;
bool showOfflineWarning = false;

unsigned long lastInteractionTime = 0;
unsigned int sleepMinutes = 30;
unsigned long currentSleepTime = 1800000;

int currentScreen = 6;

struct BusRoute {
  String route;
  String dest;
  String url;
  String dir;
  String jsonCache;
};
BusRoute busRoutes[4];

int layout_mode = 3;
int temp_layout_mode = 3;
String weatherLoc = "香港天文台";

uint8_t* font16_buffer = nullptr;
uint8_t* font20_buffer = nullptr;
uint8_t* font24_buffer = nullptr;
uint8_t* font28_buffer = nullptr;
const String weatherApiUrl = "https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=rhrread&lang=tc";
String currentTemp = "--";
bool ntpSynced = false;

unsigned long lastDataFetch = 0;
unsigned long lastClockUpdate = 0;
const unsigned long CLOCK_UPDATE_INTERVAL = 15000;
unsigned long lastWeatherUpdate = 0;
volatile bool isFetching = false;
volatile bool newDataReady = false;

#define ROW_BG_LIGHT tft.color565(235, 235, 235)
#define COLOR_MINS_NORMAL tft.color565(0, 150, 0)
#define COLOR_MINS_URGENT TFT_RED
#define COLOR_TITLE tft.color565(0, 100, 240)

// 💡 觸控讀取 (原生指令)
bool readTouch(int& screenX, int& screenY) {
  uint16_t x, y;
  if (tft.getTouch(&x, &y)) {
    screenX = x;
    screenY = y;
    return true;
  }
  return false;
}

void enterDeepSleepMode() {
  Serial.println(">>> 準備進入 Deep Sleep...");
  ledcWrite(LCD_BL_PIN, 0);
  tft.fillScreen(TFT_BLACK);
  tft.writeCommand(0x10);
  delay(120);
  ledcDetach(LCD_BL_PIN);
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);
  int dummyX, dummyY;
  readTouch(dummyX, dummyY);
  Wire.beginTransmission(0x20);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(0x20, 1);
  while (Wire.available()) { Wire.read(); }
  power.disableDC2();
  power.disableDC3();
  power.disableDC4();
  power.disableALDO2();
  power.disableALDO3();
  power.disableALDO4();
  power.disableBLDO1();
  power.disableBLDO2();
  power.disableCPUSLDO();
  power.disableDLDO1();
  power.disableDLDO2();
  delay(200);
  pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_INT_PIN, 0);
  Serial.flush();
  esp_deep_sleep_start();
}

void turnOnPMU() {
  if (!power.isEnableDC2()) power.enableDC2();
  if (!power.isEnableDC3()) power.enableDC3();
  if (!power.isEnableDC4()) power.enableDC4();
  if (!power.isEnableALDO2()) power.enableALDO2();
  if (!power.isEnableALDO3()) power.enableALDO3();
  if (!power.isEnableALDO4()) power.enableALDO4();
  if (!power.isEnableBLDO1()) power.enableBLDO1();
  if (!power.isEnableBLDO2()) power.enableBLDO2();
  if (!power.isEnableCPUSLDO()) power.enableCPUSLDO();
  if (!power.isEnableDLDO1()) power.enableDLDO1();
  if (!power.isEnableDLDO2()) power.enableDLDO2();
}

void fetchAllBusData();
void renderCachedBusData();
void fetchWeather();
void drawUIFramework();
void drawSettingsMenu();
void drawLayoutMenu();
void drawWebPortalWaitScreen();
void drawBrightnessMenu();
void drawSleepMenu();
void drawRefreshMenu();
void drawSystemInfoPage();
void drawFactoryResetConfirmPage();

void drawSplashScreen(std::vector<String> msgs, bool isError = false) {
  currentScreen = 6;
  tft.fillScreen(TFT_WHITE);

  // Title
  if (font24_buffer != nullptr) tft.loadFont((const uint8_t*)font24_buffer);
  else tft.loadFont(LittleFS, "/Noto24.vlw");
  tft.setTextColor(TFT_RED, TFT_WHITE);
  String title = "KMB ETA System";
  tft.setCursor((480 - tft.textWidth(title)) / 2, 70);
  tft.print(title);
  tft.unloadFont();

  // Author Nickname
  tft.setTextFont(2);
  tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
  String author = "by KC427";
  tft.setCursor((480 - tft.textWidth(author)) / 2, 100);
  tft.print(author);

  // Messages
  if (font16_buffer != nullptr) tft.loadFont((const uint8_t*)font16_buffer);
  else tft.loadFont(LittleFS, "/Noto16.vlw");
  int startY = 135;
  int spacing = 32;
  for (size_t i = 0; i < msgs.size(); i++) {
    bool isLast = (i == msgs.size() - 1);
    bool currentIsError = (isLast && isError);
    if (currentIsError) tft.setTextColor(TFT_RED, TFT_WHITE);
    else if (isLast) tft.setTextColor(TFT_BLACK, TFT_WHITE);
    else tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
    int x = (480 - tft.textWidth(msgs[i])) / 2;
    tft.setCursor(x, startY + i * spacing);
    tft.print(msgs[i]);
    if (currentIsError) {
      tft.setCursor(x + 1, startY + i * spacing);
      tft.print(msgs[i]);
    }
  }
  tft.unloadFont();
}

void setupUnifiedWebServer() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    if (currentScreen == 7) {
      String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif;padding:20px;background:#f4f4f9;} input[type='text'], input[type='password']{width:100%;padding:12px;margin:10px 0;font-size:16px;border:1px solid #ccc;border-radius:5px;} button{width:100%;padding:15px;background-color:#d32f2f;color:white;border:none;font-size:18px;border-radius:5px;} #overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:9999;color:#fff;font-size:24px;text-align:center;padding-top:40%;}</style></head><body><div id='overlay'>儲存中，請稍候...<br><br><span style='font-size:16px;'>(Saving, please wait...)</span></div>";
      html += "<h2 style='color:#d32f2f;text-align:center;margin-bottom:20px;'>KMB ETA System<br><span style='font-size:18px;color:#555;'>Connecting to Your Wi-Fi：</span></h2>";
      html += "<form action='/save' method='POST' onsubmit='document.getElementById(\"overlay\").style.display=\"block\"; document.getElementById(\"btn\").style.background=\"#999\"; document.getElementById(\"btn\").innerText=\"Saving...\";'><label>Wi-Fi Name (SSID):</label><br><input type='text' name='ssid' value='" + savedSSID + "'><br><label>Password:</label><br>";
      html += "<div style='display:flex;align-items:center;margin-bottom:15px;'><input type='password' id='pwd' name='pass' value='" + savedPass + "' style='margin:0;flex-grow:1;'>";
      html += "<label style='margin-left:10px;font-size:15px;'><input type='checkbox' onclick='document.getElementById(\"pwd\").type=this.checked?\"text\":\"password\"'> Show</label></div><button type='submit' id='btn'>Save & Reboot</button></form></body></html>";
      server.send(200, "text/html", html);
    } else if (currentScreen == 10) {
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server.send(200, "text/html", "");
      server.sendContent(R"=====(<!DOCTYPE html><html lang="zh-HK"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>KMB ETA Config</title><style>body{font-family:sans-serif;background-color:#f4f4f9;color:#333;margin:0;padding:20px;}.container{max-width:600px;margin:0 auto;background:#fff;padding:25px;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.1);}h2{color:#d32f2f;text-align:center;margin-bottom:25px;}.layout-preview{background:#fafafa;border:1px solid #ddd;border-radius:8px;padding:15px;margin-bottom:25px;text-align:center;}.layout-preview h4{margin:0 0 10px 0;color:#444;font-size:15px;}.mini-screen{width:240px;height:120px;border:2px solid #333;margin:0 auto;background:#fff;position:relative;display:flex;}.mini-left{width:50%;height:100%;border-right:2px solid #333;display:flex;align-items:center;justify-content:center;font-size:24px;font-weight:bold;color:#d32f2f;}.mini-right{width:50%;height:100%;display:flex;flex-direction:column;}.mini-cell{flex:1;display:flex;align-items:center;justify-content:center;font-size:16px;font-weight:bold;color:#555;border-bottom:1px solid #ccc;}.mini-cell:last-child{border-bottom:none;}.route-card{background:#fff;border:1px solid #e0e0e0;border-left:5px solid #d32f2f;border-radius:8px;padding:15px;margin-bottom:20px;}.route-card h3{margin-top:0;color:#d32f2f;font-size:18px;border-bottom:1px solid #eee;padding-bottom:8px;}.form-group{margin-bottom:12px;}label{display:block;font-weight:bold;font-size:13px;margin-bottom:5px;color:#555;}select{width:100%;padding:10px;font-size:15px;border:1px solid #ccc;border-radius:6px;background:#fff;}button.save-btn{width:100%;padding:15px;background-color:#d32f2f;color:white;border:none;font-size:18px;font-weight:bold;border-radius:8px;cursor:pointer;} #overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:9999;color:#fff;font-size:24px;text-align:center;padding-top:50%;}</style></head><body><div id="overlay">儲存中，請稍候...<br><br><span style="font-size:16px;">(Saving, please wait...)</span></div><div class="container"><h2>KMB ETA Configuration</h2><div class="layout-preview"><h4>當前畫面佈局對應</h4>)=====");

      if (temp_layout_mode == 1) {
        server.sendContent("<div class='mini-screen'><div class='mini-left'>1</div><div class='mini-right'><div class='mini-cell' style='height:100%;'>2</div></div></div></div>");
      } else if (temp_layout_mode == 2) {
        server.sendContent("<div class='mini-screen'><div class='mini-left'>1</div><div class='mini-right'><div class='mini-cell'>2</div><div class='mini-cell'>3</div></div></div></div>");
      } else {
        server.sendContent("<div class='mini-screen'><div class='mini-left'>1</div><div class='mini-right'><div class='mini-cell'>2</div><div class='mini-cell'>3</div><div class='mini-cell'>4</div></div></div></div>");
      }

      server.sendContent("<form action='/save_config' method='POST' onsubmit='document.getElementById(\"overlay\").style.display=\"block\"; document.getElementById(\"btn\").style.background=\"#999\"; document.getElementById(\"btn\").innerText=\"Saving... Please wait...\";'><div class='route-card'><h3>氣溫 - 你的位置</h3><div class='form-group'><label>選擇天文台氣溫分區:</label><select id='weather_loc' name='weather_loc'>");
      String districts[] = { "香港天文台", "京士柏", "黃竹坑", "打鼓嶺", "屯門", "沙田", "青衣", "荃灣城門谷", "將軍澳", "西貢", "大埔", "赤鱲角" };
      for (String d : districts) {
        if (d == weatherLoc) server.sendContent("<option value='" + d + "' selected>" + d + "</option>");
        else server.sendContent("<option value='" + d + "'>" + d + "</option>");
      }
      server.sendContent("</select></div></div>");

      int numRoutes = (temp_layout_mode == 1) ? 2 : ((temp_layout_mode == 2) ? 3 : 4);
      String labels[] = { "路線 1 (左側主畫面)", "路線 2 (右上方)", "路線 3 (右中間)", "路線 4 (右下方)" };
      if (temp_layout_mode == 1) labels[1] = "路線 2 (右側主畫面)";
      if (temp_layout_mode == 2) {
        labels[1] = "路線 2 (右上方)";
        labels[2] = "路線 3 (右下方)";
      }

      for (int i = 0; i < numRoutes; i++) {
        String card = "<div class='route-card'><h3>" + labels[i] + "</h3>";
        card += "<div class='form-group'><label>1. 選擇路線:</label><select id='route_" + String(i) + "' onchange='loadBound(" + String(i) + ")' required><option value=''>-- 載入中... --</option></select></div>";
        card += "<div class='form-group'><label>2. 選擇方向:</label><select id='bound_" + String(i) + "' onchange='loadStop(" + String(i) + ")' disabled required><option value=''>-- 請先選擇路線 --</option></select></div>";
        card += "<div class='form-group'><label>3. 選擇車站:</label><select id='stop_" + String(i) + "' onchange='updateUrl(" + String(i) + ")' disabled required><option value=''>-- 請先選擇方向 --</option></select></div>";
        card += "<input type='hidden' id='r" + String(i) + "_route_val' name='r" + String(i) + "_route'><input type='hidden' id='r" + String(i) + "_dest_val' name='r" + String(i) + "_dest'><input type='hidden' id='r" + String(i) + "_url_val' name='r" + String(i) + "_url'><input type='hidden' id='r" + String(i) + "_dir_val' name='r" + String(i) + "_dir'></div>";
        server.sendContent(card);
      }

      server.sendContent(R"=====(<button type="submit" class="save-btn" id="btn">Save & Apply to ESP32</button></form></div><script>let allRoutesData=[]; window.onload=function(){loadRoutes();}; async function loadRoutes(){try{let routeRes=await fetch('https://data.etabus.gov.hk/v1/transport/kmb/route/'); let routeJson=await routeRes.json(); allRoutesData=routeJson.data; let uniqueRoutes=[...new Set(allRoutesData.map(item=>item.route))]; for(let i=0;i<4;i++){let select=document.getElementById(`route_${i}`); if(!select)continue; select.innerHTML='<option value="">-- 請選擇路線 --</option>'; uniqueRoutes.forEach(route=>{let opt=document.createElement('option'); opt.value=route; opt.textContent=route; select.appendChild(opt);});}}catch(e){}} function loadBound(index){let routeVal=document.getElementById(`route_${index}`).value; let boundSelect=document.getElementById(`bound_${index}`); let stopSelect=document.getElementById(`stop_${index}`); boundSelect.innerHTML='<option value="">-- 請選擇行車方向 --</option>'; stopSelect.innerHTML='<option value="">-- 請先選擇方向 --</option>'; stopSelect.disabled=true; if(!routeVal){boundSelect.disabled=true; return;} try{let matchedRoutes=allRoutesData.filter(item=>item.route===routeVal); matchedRoutes.forEach(item=>{let opt=document.createElement('option'); opt.value=`${item.bound}|${item.service_type}`; let serviceText=item.service_type==="1"?"常規":`特別班 ${item.service_type}`; opt.textContent=`往 ${item.dest_tc} (${item.orig_tc} 開出) [${serviceText}]`; opt.dataset.destName=item.dest_tc; opt.dataset.bound=item.bound; opt.dataset.serviceType=item.service_type; boundSelect.appendChild(opt);}); boundSelect.disabled=false;}catch(e){}} async function loadStop(index){let routeVal=document.getElementById(`route_${index}`).value; let boundSelect=document.getElementById(`bound_${index}`); let stopSelect=document.getElementById(`stop_${index}`); let selectedOpt=boundSelect.options[boundSelect.selectedIndex]; if(!selectedOpt.value)return; document.getElementById(`r${index}_dest_val`).value=selectedOpt.dataset.destName; document.getElementById(`r${index}_route_val`).value=routeVal; document.getElementById(`r${index}_dir_val`).value=selectedOpt.dataset.bound; let bound=selectedOpt.dataset.bound; let serviceType=selectedOpt.dataset.serviceType; stopSelect.innerHTML='<option value="">-- 即時載入車站中... (需時數秒) --</option>'; stopSelect.disabled=true; try{let direction=bound==='O'?'outbound':'inbound'; let res=await fetch(`https://data.etabus.gov.hk/v1/transport/kmb/route-stop/${routeVal}/${direction}/${serviceType}`); let json=await res.json(); let stopPromises=json.data.map(rs=>fetch('https://data.etabus.gov.hk/v1/transport/kmb/stop/'+rs.stop).then(r=>r.json()).then(d=>({seq:rs.seq,stop:rs.stop,name:d.data.name_tc})).catch(e=>({seq:rs.seq,stop:rs.stop,name:"未知車站"}))); let stops=await Promise.all(stopPromises); stopSelect.innerHTML='<option value="">-- 請選擇車站 --</option>'; stops.forEach(s=>{let opt=document.createElement('option'); opt.value=s.stop; opt.textContent=`${s.seq}. ${s.name}`; stopSelect.appendChild(opt);}); stopSelect.disabled=false;}catch(e){stopSelect.innerHTML='<option value="">-- 載入失敗，請重試 --</option>';}} function updateUrl(index){let routeVal=document.getElementById(`r${index}_route_val`).value; let boundSelect=document.getElementById(`bound_${index}`); let stopId=document.getElementById(`stop_${index}`).value; let selectedOpt=boundSelect.options[boundSelect.selectedIndex]; let serviceType=selectedOpt.dataset.serviceType||"1"; if(stopId&&routeVal){let etaUrl=`https://data.etabus.gov.hk/v1/transport/kmb/eta/${stopId}/${routeVal}/${serviceType}`; document.getElementById(`r${index}_url_val`).value=etaUrl;}}</script></body></html>)=====");
      server.sendContent("");
    } else {
      server.send(403, "text/plain", "Please enter a setup mode on the device screen.");
    }
  });

  server.on("/save", HTTP_POST, []() {
    if (currentScreen == 7) {
      String newSSID = server.arg("ssid");
      String newPass = server.arg("pass");
      preferences.begin("bus_eta", false);
      preferences.putString("ssid", newSSID);
      preferences.putString("pass", newPass);
      preferences.end();
      server.send(200, "text/html", "<html><body style='font-family:sans-serif;text-align:center;padding:50px;'><h2 style='color:#d32f2f;font-size:50px;margin-top:100px;'>Settings Saved!</h2><p style='font-size:30px;color:#555;'>Device is rebooting...</p></body></html>");
      delay(1000);
      ESP.restart();
    }
  });

  server.on("/save_config", HTTP_POST, []() {
    if (currentScreen == 10) {
      preferences.begin("bus_eta", false);
      preferences.putInt("layout", temp_layout_mode);
      preferences.putString("weatherLoc", server.arg("weather_loc"));
      int numRoutes = (temp_layout_mode == 1) ? 2 : ((temp_layout_mode == 2) ? 3 : 4);
      for (int i = 0; i < numRoutes; i++) {
        String prefix = "r" + String(i) + "_";
        preferences.putString((prefix + "route").c_str(), server.arg(prefix + "route"));
        preferences.putString((prefix + "dest").c_str(), server.arg(prefix + "dest"));
        preferences.putString((prefix + "url").c_str(), server.arg(prefix + "url"));
        preferences.putString((prefix + "dir").c_str(), server.arg(prefix + "dir"));
      }
      preferences.end();
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.send(200, "text/html", "<html><body style='font-family:sans-serif;text-align:center;padding:50px;'><h2 style='color:#d32f2f;font-size:50px;margin-top:100px;'>Configuration Saved!</h2><p style='font-size:30px;color:#555;'>Device is rebooting with new layout...</p></body></html>");
      delay(1500);
      ESP.restart();
    }
  });

  server.onNotFound([]() {
    if (currentScreen == 7) {
      server.sendHeader("Location", "http://" + AP_IP_STRING + "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });
}

void startAPMode(bool fromSettings = false) {
  currentScreen = 7;
  apModeFromSettings = fromSettings;
  WiFi.mode(WIFI_AP_STA);
  IPAddress apIP(192, 168, 10, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("KMB_ETA_Setup");
  dnsServer.start(DNS_PORT, "*", apIP);

  server.begin();

  tft.fillScreen(TFT_WHITE);
  tft.fillRect(0, 0, 480, 36, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextFont(4);
  tft.setCursor(10, 6);
  tft.print("Setting up WiFi...");
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(35, 60);
  tft.print("1. Connect Your Phone Wi-Fi to:");
  tft.setTextColor(TFT_RED, TFT_WHITE);
  tft.setCursor(35, 90);
  tft.print("KMB_ETA_Setup");
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(35, 130);
  tft.print("2. Page pops up OR go to:");
  tft.setTextColor(TFT_RED, TFT_WHITE);
  tft.setCursor(35, 160);
  tft.print("http://" + AP_IP_STRING);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(35, 200);
  tft.print("3. When finish, tap below to reboot.");

  int btnY = 240;
  int boxW = 140;
  int boxH = 60;
  if (apModeFromSettings) {
    int backX = 80;
    tft.fillRoundRect(backX, btnY, boxW, boxH, 8, tft.color565(220, 220, 220));
    tft.drawRoundRect(backX, btnY, boxW, boxH, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_BLACK, tft.color565(220, 220, 220));
    tft.setCursor(backX + (boxW - tft.textWidth("BACK")) / 2, btnY + (boxH - 26) / 2);
    tft.print("BACK");
    int rebootX = 260;
    tft.fillRoundRect(rebootX, btnY, boxW, boxH, 8, TFT_RED);
    tft.drawRoundRect(rebootX, btnY, boxW, boxH, 8, TFT_MAROON);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setCursor(rebootX + (boxW - tft.textWidth("REBOOT")) / 2, btnY + (boxH - 26) / 2);
    tft.print("REBOOT");
  } else {
    int backX = 80;
    tft.fillRoundRect(backX, btnY, boxW, boxH, 8, tft.color565(220, 220, 220));
    tft.drawRoundRect(backX, btnY, boxW, boxH, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_BLACK, tft.color565(220, 220, 220));
    tft.setCursor(backX + (boxW - tft.textWidth("SKIP")) / 2, btnY + (boxH - 26) / 2);
    tft.print("SKIP");
    int rebootX = 260;
    tft.fillRoundRect(rebootX, btnY, boxW, boxH, 8, TFT_RED);
    tft.drawRoundRect(rebootX, btnY, boxW, boxH, 8, TFT_MAROON);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setCursor(rebootX + (boxW - tft.textWidth("REBOOT")) / 2, btnY + (boxH - 26) / 2);
    tft.print("REBOOT");
  }
}

void printChinese(String text, int x, int y, uint16_t fgColor, uint16_t bgColor, int fontSize = 24);

void drawHeader(String title) {
  tft.fillRect(0, 0, 480, 36, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextFont(4);
  tft.setCursor(10, 6);
  tft.print(title);
}
void drawFooterButton(String text) {
  tft.fillRect(0, 284, 480, 36, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextFont(2);
  tft.setCursor(12, 292);
  tft.print(text);
}

void drawSettingsMenu() {
  currentScreen = 1;
  tft.fillScreen(TFT_WHITE);
  drawHeader("System Settings");
  drawFooterButton("BACK TO HOME");
  int startX[3] = { 10, 165, 320 };
  int startY[3] = { 42, 120, 198 };
  int boxW = 150;
  int boxH = 75;
  String labels[9] = { "Brightness", "Sleep Time", "Route Config", "Wi-Fi Setup", "Refresh Time", "N/A", "N/A", "Factory Reset", "System Info" };

  if (font20_buffer != nullptr) tft.loadFont((const uint8_t*)font20_buffer);
  else tft.loadFont(LittleFS, "/Noto20.vlw");

  bool hasWiFi = (WiFi.status() == WL_CONNECTED && WiFi.localIP().toString() != "0.0.0.0");

  for (int i = 0; i < 9; i++) {
    int x = startX[i % 3];
    int y = startY[i / 3];

    if ((i == 2 && !hasWiFi) || i == 5 || i == 6) {
      tft.fillRoundRect(x, y, boxW, boxH, 8, tft.color565(250, 250, 250));
      tft.drawRoundRect(x, y, boxW, boxH, 8, tft.color565(200, 200, 200));
      tft.setTextColor(tft.color565(180, 180, 180), tft.color565(250, 250, 250));
    } else {
      tft.fillRoundRect(x, y, boxW, boxH, 8, tft.color565(240, 240, 240));
      tft.drawRoundRect(x, y, boxW, boxH, 8, TFT_DARKGREY);
      tft.setTextColor(TFT_BLACK, tft.color565(240, 240, 240));
    }

    int textX = x + (boxW - tft.textWidth(labels[i])) / 2;
    int textY = y + (boxH - 22) / 2;
    tft.setCursor(textX, textY);
    tft.print(labels[i]);
  }
  tft.unloadFont();
}

void drawLayoutMenu() {
  currentScreen = 9;
  temp_layout_mode = layout_mode;
  tft.fillScreen(TFT_WHITE);
  drawHeader("Layout Selection");
  drawFooterButton("BACK TO SETTINGS");
  int boxW = 120;
  int boxH = 120;
  int yBox = 60;
  int x1 = 30, x2 = 180, x3 = 330;
  tft.drawRect(x1, yBox, boxW, boxH, TFT_DARKGREY);
  tft.drawRect(x2, yBox, boxW, boxH, TFT_DARKGREY);
  tft.drawRect(x3, yBox, boxW, boxH, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY);
  tft.drawFastVLine(x1 + boxW / 2, yBox, boxH, TFT_DARKGREY);
  tft.drawCentreString("1", x1 + 30, yBox + 47, 4);
  tft.drawCentreString("2", x1 + 90, yBox + 47, 4);
  tft.drawFastVLine(x2 + boxW / 2, yBox, boxH, TFT_DARKGREY);
  tft.drawFastHLine(x2 + boxW / 2, yBox + boxH / 2, boxW / 2, TFT_DARKGREY);
  tft.drawCentreString("1", x2 + 30, yBox + 47, 4);
  tft.drawCentreString("2", x2 + 90, yBox + 17, 4);
  tft.drawCentreString("3", x2 + 90, yBox + 77, 4);
  tft.drawFastVLine(x3 + boxW / 2, yBox, boxH, TFT_DARKGREY);
  tft.drawFastHLine(x3 + boxW / 2, yBox + boxH / 3, boxW / 2, TFT_DARKGREY);
  tft.drawFastHLine(x3 + boxW / 2, yBox + (boxH * 2) / 3, boxW / 2, TFT_DARKGREY);
  tft.drawCentreString("1", x3 + 30, yBox + 47, 4);
  tft.drawCentreString("2", x3 + 90, yBox + 7, 4);
  tft.drawCentreString("3", x3 + 90, yBox + 47, 4);
  tft.drawCentreString("4", x3 + 90, yBox + 87, 4);
  int yCircle = 210;
  tft.drawCircle(x1 + boxW / 2, yCircle, 12, TFT_DARKGREY);
  tft.drawCircle(x2 + boxW / 2, yCircle, 12, TFT_DARKGREY);
  tft.drawCircle(x3 + boxW / 2, yCircle, 12, TFT_DARKGREY);
  if (temp_layout_mode == 1) tft.fillCircle(x1 + boxW / 2, yCircle, 8, TFT_RED);
  if (temp_layout_mode == 2) tft.fillCircle(x2 + boxW / 2, yCircle, 8, TFT_RED);
  if (temp_layout_mode == 3) tft.fillCircle(x3 + boxW / 2, yCircle, 8, TFT_RED);
}

void drawWebPortalWaitScreen() {
  currentScreen = 10;
  server.begin();

  tft.fillScreen(TFT_WHITE);
  drawHeader("Route Config Portal");
  drawFooterButton("BACK TO LAYOUT");
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextFont(4);

  tft.setCursor(30, 70);
  tft.print("1. Ensure phone/PC is on same Wi-Fi:");

  if (WiFi.status() != WL_CONNECTED || WiFi.localIP().toString() == "0.0.0.0") {
    tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
    tft.setTextFont(2);
    tft.setCursor(30, 240);
    tft.print("Connecting Wi-Fi...");
    unsigned long wStart = millis();
    while ((WiFi.status() != WL_CONNECTED || WiFi.localIP().toString() == "0.0.0.0") && millis() - wStart < 6000) { delay(100); }
    tft.fillRect(30, 240, 400, 30, TFT_WHITE);
  }

  tft.setTextColor(TFT_RED, TFT_WHITE);
  tft.setTextFont(4);
  tft.setCursor(30, 100);
  tft.print(WiFi.SSID());

  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(30, 150);
  tft.print("2. Open browser and visit:");
  tft.setTextColor(TFT_RED, TFT_WHITE);
  tft.setCursor(30, 180);
  String ipStr = "http://" + WiFi.localIP().toString();
  tft.print(ipStr);
  tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
  tft.setTextFont(2);
  tft.setCursor(30, 240);
  tft.print("Waiting for Save & Apply...");
}

void drawBrightnessMenu() {
  currentScreen = 2;
  tft.fillScreen(TFT_WHITE);
  drawHeader("Brightness Control");
  drawFooterButton("BACK TO SETTINGS");
  int currentPercent = (currentBrightness * 100 + 127) / 255;
  String textPart = "BRIGHTNESS: ";
  String numPart = String(currentPercent);
  String unitPart = "%";
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextFont(4);
  int textW = tft.textWidth(textPart);
  tft.setTextFont(7);
  int numW = tft.textWidth(numPart);
  tft.setTextFont(4);
  int unitW = tft.textWidth(unitPart);
  int totalW = textW + numW + unitW + 4;
  int startX = (480 - totalW) / 2;
  int startY = 68;
  tft.setTextFont(4);
  tft.setCursor(startX, startY + 14);
  tft.print(textPart);
  tft.setTextFont(7);
  tft.setCursor(startX + textW, startY - 2);
  tft.print(numPart);
  tft.setTextFont(4);
  tft.setCursor(startX + textW + numW + 4, startY + 14);
  tft.print(unitPart);
  tft.drawFastHLine(20, 155, 440, TFT_DARKGREY);
  int boxW = 68;
  int boxH = 75;
  int gap = (480 - (6 * boxW)) / 7;
  int startY_btn = 175;
  String labels[6] = { "5%", "20%", "40%", "60%", "80%", "100%" };
  tft.setTextFont(4);
  for (int i = 0; i < 6; i++) {
    int x = gap + i * (boxW + gap);
    int y = startY_btn;
    tft.fillRoundRect(x, y, boxW, boxH, 8, tft.color565(230, 240, 250));
    tft.drawRoundRect(x, y, boxW, boxH, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_BLACK, tft.color565(230, 240, 250));
    int textX = x + (boxW - tft.textWidth(labels[i])) / 2;
    int textY = y + (boxH - 26) / 2;
    tft.setCursor(textX, textY);
    tft.print(labels[i]);
  }
}

void drawSleepMenu() {
  currentScreen = 4;
  tft.fillScreen(TFT_WHITE);
  drawHeader("Sleep Time Setup");
  drawFooterButton("BACK TO SETTINGS");
  String textPart = "SLEEP: ";
  String numPart = (sleepMinutes == 0) ? "NEVER" : String(sleepMinutes);
  String unitPart = (sleepMinutes == 0) ? "" : " MIN";
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextFont(4);
  int textW = tft.textWidth(textPart);

  int numFont = (sleepMinutes == 0) ? 4 : 7;
  tft.setTextFont(numFont);
  int numW = tft.textWidth(numPart);
  tft.setTextFont(4);
  int unitW = tft.textWidth(unitPart);
  int totalW = textW + numW + unitW + 4;
  int startX = (480 - totalW) / 2;
  int startY = 68;
  tft.setTextFont(4);
  tft.setCursor(startX, startY + 14);
  tft.print(textPart);
  tft.setTextFont(numFont);
  tft.setCursor(startX + textW, (sleepMinutes == 0) ? startY + 14 : startY - 2);
  tft.print(numPart);
  tft.setTextFont(4);
  tft.setCursor(startX + textW + numW + 4, startY + 14);
  tft.print(unitPart);
  tft.drawFastHLine(20, 155, 440, TFT_DARKGREY);
  int boxW = 68;
  int boxH = 75;
  int gap = (480 - (6 * boxW)) / 7;
  int startY_btn = 175;
  String labels[6] = { "1M", "5M", "10M", "15M", "30M", "60M" };
  tft.setTextFont(4);
  for (int i = 0; i < 6; i++) {
    int x = gap + i * (boxW + gap);
    int y = startY_btn;
    tft.fillRoundRect(x, y, boxW, boxH, 8, tft.color565(230, 240, 250));
    tft.drawRoundRect(x, y, boxW, boxH, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_BLACK, tft.color565(230, 240, 250));
    int textX = x + (boxW - tft.textWidth(labels[i])) / 2;
    int textY = y + (boxH - 26) / 2;
    tft.setCursor(textX, textY);
    tft.print(labels[i]);
  }
}

void drawRefreshMenu() {
  currentScreen = 8;
  tft.fillScreen(TFT_WHITE);
  drawHeader("Refresh Time Setup");
  drawFooterButton("BACK TO SETTINGS");
  String textPart = "REFRESH: ";
  String numPart = "";
  String unitPart = "";
  if (DATA_INTERVAL == 15000) {
    numPart = "15";
    unitPart = " s";
  } else if (DATA_INTERVAL == 30000) {
    numPart = "30";
    unitPart = " s";
  } else if (DATA_INTERVAL == 60000) {
    numPart = "1";
    unitPart = " m";
  } else if (DATA_INTERVAL == 120000) {
    numPart = "2";
    unitPart = " m";
  } else {
    numPart = String(DATA_INTERVAL / 1000);
    unitPart = " s";
  }
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextFont(4);
  int textW = tft.textWidth(textPart);
  tft.setTextFont(7);
  int numW = tft.textWidth(numPart);
  tft.setTextFont(4);
  int unitW = tft.textWidth(unitPart);
  int totalW = textW + numW + unitW + 4;
  int startX = (480 - totalW) / 2;
  int startY = 68;
  tft.setTextFont(4);
  tft.setCursor(startX, startY + 14);
  tft.print(textPart);
  tft.setTextFont(7);
  tft.setCursor(startX + textW, startY - 2);
  tft.print(numPart);
  tft.setTextFont(4);
  tft.setCursor(startX + textW + numW + 4, startY + 14);
  tft.print(unitPart);
  tft.drawFastHLine(20, 155, 440, TFT_DARKGREY);
  int boxW = 95;
  int boxH = 75;
  int gap = (480 - (4 * boxW)) / 5;
  int startY_btn = 175;
  String labels[4] = { "15s", "30s", "1m", "2m" };
  tft.setTextFont(4);
  for (int i = 0; i < 4; i++) {
    int x = gap + i * (boxW + gap);
    int y = startY_btn;
    tft.fillRoundRect(x, y, boxW, boxH, 8, tft.color565(230, 240, 250));
    tft.drawRoundRect(x, y, boxW, boxH, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_BLACK, tft.color565(230, 240, 250));
    int textX = x + (boxW - tft.textWidth(labels[i])) / 2;
    int textY = y + (boxH - 26) / 2;
    tft.setCursor(textX, textY);
    tft.print(labels[i]);
  }
}

void drawSystemInfoPage() {
  currentScreen = 3;
  tft.fillScreen(TFT_WHITE);
  drawHeader("System Information");
  drawFooterButton("BACK TO SETTINGS");
  int currentPercent = (currentBrightness * 100 + 127) / 255;
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextFont(4);
  int startY = 50;
  int spacing = 32;
  tft.setCursor(20, startY);
  tft.print("Firmware:  " + FIRMWARE_VERSION);
  tft.setCursor(20, startY + spacing);
  tft.print("Wi-Fi SSID:  " + (savedSSID == "" ? "Not Set" : savedSSID));
  tft.setCursor(20, startY + spacing * 2);
  tft.print("IP:  " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "Not Connected"));
  tft.setCursor(20, startY + spacing * 3);
  tft.print("Brightness:  " + String(currentPercent) + "%");
  tft.setCursor(20, startY + spacing * 4);
  tft.print("Sleep Timer:  " + (sleepMinutes == 0 ? String("NEVER") : String(sleepMinutes) + " Min"));
  tft.setCursor(20, startY + spacing * 5);
  tft.print("Refresh Interval:  " + String(DATA_INTERVAL / 1000) + " sec");

  String authorText = "Created by: KC427";
  tft.setCursor(460 - tft.textWidth(authorText), 255);
  tft.print(authorText);
}

void drawFactoryResetConfirmPage() {
  currentScreen = 5;
  tft.fillScreen(TFT_WHITE);
  drawHeader("Factory Reset");
  drawFooterButton("BACK TO SETTINGS");
  tft.setTextColor(TFT_RED, TFT_WHITE);
  tft.setTextFont(4);
  String warningTitle = "ARE YOU SURE?";
  int titleW = tft.textWidth(warningTitle);
  tft.setCursor((480 - titleW) / 2, 80);
  tft.print(warningTitle);
  tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
  tft.setTextFont(2);
  String warningSub = "All settings & Wi-Fi will be lost.";
  int subW = tft.textWidth(warningSub);
  tft.setCursor((480 - subW) / 2, 120);
  tft.print(warningSub);
  int boxW = 140;
  int boxH = 80;
  int gap = 66;
  int btnY = 170;
  int backX = gap;
  tft.fillRoundRect(backX, btnY, boxW, boxH, 8, tft.color565(220, 220, 220));
  tft.drawRoundRect(backX, btnY, boxW, boxH, 8, TFT_DARKGREY);
  tft.setTextColor(TFT_BLACK, tft.color565(220, 220, 220));
  tft.setTextFont(4);
  tft.setCursor(backX + (boxW - tft.textWidth("BACK")) / 2, btnY + (boxH - 26) / 2);
  tft.print("BACK");
  int yesX = backX + boxW + gap;
  tft.fillRoundRect(yesX, btnY, boxW, boxH, 8, TFT_RED);
  tft.drawRoundRect(yesX, btnY, boxW, boxH, 8, TFT_MAROON);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextFont(4);
  tft.setCursor(yesX + (boxW - tft.textWidth("YES")) / 2, btnY + (boxH - 26) / 2);
  tft.print("YES");
}

void drawTopBar();
void drawFooterBar();
void drawUIFramework();
void renderCachedBusData();

void handleTouchAction(int x, int y) {
  lastInteractionTime = millis();
  if (currentScreen == 0) {
    if (y >= 275) {
      if (x >= 60 && x <= 180) {
        if (!isFetching && WiFi.status() == WL_CONNECTED) {
          isFetching = true;
          xTaskCreatePinnedToCore([](void*) {
            unsigned long currentMillisUI = millis();
            if (lastWeatherUpdate == 0 || (currentMillisUI - lastWeatherUpdate >= 900000)) {
              fetchWeather();
              lastWeatherUpdate = currentMillisUI;
            }
            fetchAllBusData();
            newDataReady = true;
            isFetching = false;
            vTaskDelete(NULL);
          },
                                  "fetchTask", 10240, NULL, 1, NULL, 0);
          lastDataFetch = millis();
        }
      } else if (x >= 180 && x <= 300) {
        drawSettingsMenu();
        delay(200);
      } else if (x >= 300 && x <= 420) {
        enterDeepSleepMode();
      }
    }
  } else if (currentScreen == 1) {
    if (y >= 275) {
      drawUIFramework();
      renderCachedBusData();
      delay(200);
      return;
    }
    if (y >= 36 && y < 275) {
      int col = -1, row = -1;
      if (x >= 10 && x < 160) col = 0;
      else if (x >= 165 && x < 315) col = 1;
      else if (x >= 320 && x <= 470) col = 2;
      if (y >= 42 && y < 118) row = 0;
      else if (y >= 120 && y < 196) row = 1;
      else if (y >= 198 && y < 274) row = 2;
      if (col != -1 && row != -1) {
        int index = row * 3 + col;
        if (index == 0) {
          drawBrightnessMenu();
          delay(200);
        } else if (index == 1) {
          drawSleepMenu();
          delay(200);
        } else if (index == 2) {
          if (WiFi.status() == WL_CONNECTED && WiFi.localIP().toString() != "0.0.0.0") {
            drawLayoutMenu();
            delay(200);
          } else {
            tft.fillRoundRect(320, 42, 150, 75, 8, tft.color565(255, 230, 230));
            tft.drawRoundRect(320, 42, 150, 75, 8, TFT_RED);
            if (font20_buffer != nullptr) tft.loadFont((const uint8_t*)font20_buffer);
            else tft.loadFont(LittleFS, "/Noto20.vlw");
            tft.setTextColor(TFT_RED, tft.color565(255, 230, 230));
            tft.setCursor(320 + (150 - tft.textWidth("請先設定 Wi-Fi")) / 2, 42 + (75 - 22) / 2);
            tft.print("請先設定 Wi-Fi");
            tft.unloadFont();
            delay(1500);
            drawSettingsMenu();
          }
        } else if (index == 3) {
          startAPMode(true);
          delay(200);
        } else if (index == 4) {
          drawRefreshMenu();
          delay(200);
        } else if (index == 7) {
          drawFactoryResetConfirmPage();
          delay(200);
        } else if (index == 8) {
          drawSystemInfoPage();
          delay(200);
        }
      }
    }
  } else if (currentScreen == 2) {
    if (y >= 275) {
      drawSettingsMenu();
      delay(200);
      return;
    }
    if (y >= 175 && y <= 255) {
      int boxW = 68;
      int gap = (480 - (6 * boxW)) / 7;
      int clickedIndex = -1;
      for (int i = 0; i < 6; i++) {
        int xMin = gap + i * (boxW + gap);
        int xMax = xMin + boxW;
        if (x >= xMin && x <= xMax) {
          clickedIndex = i;
          break;
        }
      }
      if (clickedIndex != -1) {
        int targetPercent = 5;
        if (clickedIndex == 0) targetPercent = 5;
        else if (clickedIndex == 1) targetPercent = 20;
        else if (clickedIndex == 2) targetPercent = 40;
        else if (clickedIndex == 3) targetPercent = 60;
        else if (clickedIndex == 4) targetPercent = 80;
        else if (clickedIndex == 5) targetPercent = 100;
        currentBrightness = (targetPercent * 255) / 100;
        if (currentBrightness < 5) currentBrightness = 5;
        ledcWrite(LCD_BL_PIN, currentBrightness);
        preferences.begin("bus_eta", false);
        preferences.putInt("brightness", currentBrightness);
        preferences.end();
        tft.fillRect(20, 50, 440, 95, TFT_WHITE);
        String textPart = "BRIGHTNESS: ";
        String numPart = String(targetPercent);
        String unitPart = "%";
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setTextFont(4);
        int textW = tft.textWidth(textPart);
        tft.setTextFont(7);
        int numW = tft.textWidth(numPart);
        tft.setTextFont(4);
        int unitW = tft.textWidth(unitPart);
        int totalW = textW + numW + unitW + 4;
        int startX = (480 - totalW) / 2;
        int startY = 68;
        tft.setTextFont(4);
        tft.setCursor(startX, startY + 14);
        tft.print(textPart);
        tft.setTextFont(7);
        tft.setCursor(startX + textW, startY - 2);
        tft.print(numPart);
        tft.setTextFont(4);
        tft.setCursor(startX + textW + numW + 4, startY + 14);
        tft.print(unitPart);
        delay(150);
      }
    }
  } else if (currentScreen == 3) {
    if (y >= 275) {
      drawSettingsMenu();
      delay(200);
      return;
    }
  } else if (currentScreen == 4) {
    if (y >= 275) {
      drawSettingsMenu();
      delay(200);
      return;
    }
    if (y >= 175 && y <= 255) {
      int boxW = 68;
      int gap = (480 - (6 * boxW)) / 7;
      int clickedIndex = -1;
      for (int i = 0; i < 6; i++) {
        int xMin = gap + i * (boxW + gap);
        int xMax = xMin + boxW;
        if (x >= xMin && x <= xMax) {
          clickedIndex = i;
          break;
        }
      }
      if (clickedIndex != -1) {
        int targetMinutes = 30;
        if (clickedIndex == 0) targetMinutes = 1;
        else if (clickedIndex == 1) targetMinutes = 5;
        else if (clickedIndex == 2) targetMinutes = 10;
        else if (clickedIndex == 3) targetMinutes = 15;
        else if (clickedIndex == 4) targetMinutes = 30;
        else if (clickedIndex == 5) targetMinutes = 60;
        sleepMinutes = targetMinutes;
        if (sleepMinutes == 0) currentSleepTime = 0;
        else currentSleepTime = sleepMinutes * 60000UL;
        preferences.begin("bus_eta", false);
        preferences.putUInt("sleepTime", sleepMinutes);
        preferences.end();
        tft.fillRect(20, 50, 440, 95, TFT_WHITE);
        String textPart = "SLEEP: ";
        String numPart = (sleepMinutes == 0) ? "NEVER" : String(sleepMinutes);
        String unitPart = (sleepMinutes == 0) ? "" : " MIN";
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setTextFont(4);
        int textW = tft.textWidth(textPart);
        int numFont = (sleepMinutes == 0) ? 4 : 7;
        tft.setTextFont(numFont);
        int numW = tft.textWidth(numPart);
        tft.setTextFont(4);
        int unitW = tft.textWidth(unitPart);
        int totalW = textW + numW + unitW + 4;
        int startX = (480 - totalW) / 2;
        int startY = 68;
        tft.setTextFont(4);
        tft.setCursor(startX, startY + 14);
        tft.print(textPart);
        tft.setTextFont(numFont);
        tft.setCursor(startX + textW, (sleepMinutes == 0) ? startY + 14 : startY - 2);
        tft.print(numPart);
        tft.setTextFont(4);
        tft.setCursor(startX + textW + numW + 4, startY + 14);
        tft.print(unitPart);
        delay(150);
      }
    }
  } else if (currentScreen == 5) {
    if (y >= 275) {
      drawSettingsMenu();
      delay(200);
      return;
    }
    if (y >= 170 && y <= 250) {
      int boxW = 140;
      int gap = 66;
      int backX = gap;
      int yesX = backX + boxW + gap;
      if (x >= backX && x <= backX + boxW) {
        drawSettingsMenu();
        delay(200);
      } else if (x >= yesX && x <= yesX + boxW) {
        preferences.begin("bus_eta", false);
        preferences.clear();
        preferences.end();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextFont(4);
        tft.setCursor(120, 140);
        tft.print("Factory Reset Done!");
        delay(1500);
        ESP.restart();
      }
    }
  } else if (currentScreen == 7) {
    if (apModeFromSettings) {
      if (x >= 80 && x <= 220 && y >= 240 && y <= 300) {
        tft.fillRect(80, 240, 140, 60, TFT_DARKGREY);
        delay(200);
        server.stop();
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        delay(100);

        if (savedSSID != "" && WiFi.status() != WL_CONNECTED) {
          tft.fillScreen(TFT_WHITE);
          tft.fillRect(0, 0, 480, 36, TFT_RED);
          tft.setTextColor(TFT_WHITE, TFT_RED);
          tft.setTextFont(4);
          tft.setCursor(10, 6);
          tft.print("Reconnecting Wi-Fi...");
          tft.setTextColor(TFT_BLACK, TFT_WHITE);
          tft.setCursor(30, 140);
          tft.print("Connecting to: " + savedSSID);

          WiFi.disconnect(true);
          delay(100);
          WiFi.begin(savedSSID.c_str(), savedPass.c_str());

          unsigned long sw = millis();
          while (WiFi.status() != WL_CONNECTED && millis() - sw < 8000) {
            delay(500);
            tft.print(".");
          }
        }
        drawSettingsMenu();
        lastTouchState = true;
        return;
      } else if (x >= 260 && x <= 400 && y >= 240 && y <= 300) {
        tft.fillRect(260, 240, 140, 60, TFT_RED);
        delay(200);
        ESP.restart();
      }
    } else {
      if (x >= 80 && x <= 220 && y >= 240 && y <= 300) {
        tft.fillRect(80, 240, 140, 60, TFT_DARKGREY);
        delay(200);
        server.stop();
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        delay(100);
        showOfflineWarning = true;
        drawUIFramework();
        lastTouchState = true;
        return;
      } else if (x >= 260 && x <= 400 && y >= 240 && y <= 300) {
        tft.fillRect(260, 240, 140, 60, TFT_RED);
        delay(200);
        ESP.restart();
      }
    }
  } else if (currentScreen == 8) {
    if (y >= 275) {
      drawSettingsMenu();
      delay(200);
      return;
    }
    if (y >= 175 && y <= 250) {
      int boxW = 95;
      int gap = (480 - (4 * boxW)) / 5;
      int clickedIndex = -1;
      for (int i = 0; i < 4; i++) {
        int xMin = gap + i * (boxW + gap);
        int xMax = xMin + boxW;
        if (x >= xMin && x <= xMax) {
          clickedIndex = i;
          break;
        }
      }
      if (clickedIndex != -1) {
        if (clickedIndex == 0) DATA_INTERVAL = 15000;
        else if (clickedIndex == 1) DATA_INTERVAL = 30000;
        else if (clickedIndex == 2) DATA_INTERVAL = 60000;
        else if (clickedIndex == 3) DATA_INTERVAL = 120000;
        preferences.begin("bus_eta", false);
        preferences.putULong("refreshSec", DATA_INTERVAL);
        preferences.end();
        tft.fillRect(20, 50, 440, 95, TFT_WHITE);
        String numPart = "";
        String unitPart = "";
        if (DATA_INTERVAL == 15000) {
          numPart = "15";
          unitPart = " s";
        } else if (DATA_INTERVAL == 30000) {
          numPart = "30";
          unitPart = " s";
        } else if (DATA_INTERVAL == 60000) {
          numPart = "1";
          unitPart = " m";
        } else if (DATA_INTERVAL == 120000) {
          numPart = "2";
          unitPart = " m";
        }
        String textPart = "REFRESH: ";
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setTextFont(4);
        int textW = tft.textWidth(textPart);
        tft.setTextFont(7);
        int numW = tft.textWidth(numPart);
        tft.setTextFont(4);
        int unitW = tft.textWidth(unitPart);
        int totalW = textW + numW + unitW + 4;
        int startX = (480 - totalW) / 2;
        int startY = 68;
        tft.setTextFont(4);
        tft.setCursor(startX, startY + 14);
        tft.print(textPart);
        tft.setTextFont(7);
        tft.setCursor(startX + textW, startY - 2);
        tft.print(numPart);
        tft.setTextFont(4);
        tft.setCursor(startX + textW + numW + 4, startY + 14);
        tft.print(unitPart);
        delay(150);
      }
    }
  } else if (currentScreen == 9) {
    if (y >= 275) {
      drawSettingsMenu();
      delay(200);
      return;
    }
    if (y >= 50 && y <= 250) {
      int clickedMode = 0;
      if (x >= 20 && x <= 160) clickedMode = 1;
      else if (x >= 170 && x <= 310) clickedMode = 2;
      else if (x >= 320 && x <= 460) clickedMode = 3;
      if (clickedMode > 0) {
        temp_layout_mode = clickedMode;
        tft.fillCircle(30 + 60, 210, 10, TFT_WHITE);
        tft.fillCircle(180 + 60, 210, 10, TFT_WHITE);
        tft.fillCircle(330 + 60, 210, 10, TFT_WHITE);
        if (clickedMode == 1) tft.fillCircle(30 + 60, 210, 8, TFT_RED);
        if (clickedMode == 2) tft.fillCircle(180 + 60, 210, 8, TFT_RED);
        if (clickedMode == 3) tft.fillCircle(330 + 60, 210, 8, TFT_RED);
        delay(150);
        drawWebPortalWaitScreen();
      }
    }
  } else if (currentScreen == 10) {
    if (y >= 275) {
      server.stop();
      drawLayoutMenu();
      delay(200);
      return;
    }
  }
}

void startNTP() {
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
}
void checkNTP() {
  if (ntpSynced) return;
  time_t now = time(nullptr);
  if (now > 1700000000) ntpSynced = true;
}

int getMinutesUntil(const char* etaIso) {
  if (etaIso == nullptr || strlen(etaIso) == 0) return -999;
  time_t now;
  time(&now);
  if (now < 1700000000) return -999;
  int year, month, day, hour, minute, second;
  if (sscanf(etaIso, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) return -999;
  struct tm eta_tm;
  eta_tm.tm_year = year - 1900;
  eta_tm.tm_mon = month - 1;
  eta_tm.tm_mday = day;
  eta_tm.tm_hour = hour;
  eta_tm.tm_min = minute;
  eta_tm.tm_sec = second;
  eta_tm.tm_isdst = -1;
  time_t eta_epoch = mktime(&eta_tm);
  double diff = difftime(eta_epoch, now);
  if (diff <= 0) return 0;
  int mins = (int)(diff / 60.0);
  if (mins > 1440) return -999;
  return mins;
}
String getClockTime(const char* etaIso) {
  if (etaIso == nullptr || strlen(etaIso) < 16) return "--:--";
  String s = String(etaIso);
  return s.substring(11, 16);
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(weatherApiUrl);
  http.setTimeout(2000);
  if (http.GET() == 200) {
    DynamicJsonDocument doc(8192);
    if (!deserializeJson(doc, http.getString())) {
      JsonArray tempArray = doc["temperature"]["data"];
      for (JsonObject t : tempArray) {
        if (t["place"] == weatherLoc) {
          currentTemp = String((int)t["value"]);
          break;
        }
      }
      if (currentTemp == "--" && tempArray.size() > 0) currentTemp = String((int)tempArray[0]["value"]);
    }
  }
  http.end();
}

int getBatteryPercentage() {
  if (power.isBatteryConnect()) {
    int pct = power.getBatteryPercent();
    if (pct < 0 || pct > 100) return 100;
    return pct;
  }
  return 100;
}
int getWifiPercentage() {
  int rssi = WiFi.RSSI();
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return 2 * (rssi + 100);
}

void printChinese(String text, int x, int y, uint16_t fgColor, uint16_t bgColor, int fontSize) {
  if (fontSize == 16) {
    if (font16_buffer != nullptr) tft.loadFont((const uint8_t*)font16_buffer);
    else tft.loadFont(LittleFS, "/Noto16.vlw");
  } else if (fontSize == 20) {
    if (font20_buffer != nullptr) tft.loadFont((const uint8_t*)font20_buffer);
    else tft.loadFont(LittleFS, "/Noto20.vlw");
  } else if (fontSize == 28) {
    if (font28_buffer != nullptr) tft.loadFont((const uint8_t*)font28_buffer);
    else tft.loadFont(LittleFS, "/Noto28.vlw");
  } else {
    if (font24_buffer != nullptr) tft.loadFont((const uint8_t*)font24_buffer);
    else tft.loadFont(LittleFS, "/Noto24.vlw");
  }

  tft.setTextColor(fgColor, bgColor);
  tft.setCursor(x, y);
  tft.print(text);
  tft.unloadFont();
}

void printBold(String text, int x, int y, int font, uint16_t fgColor, uint16_t bgColor) {
  tft.setTextFont(font);
  tft.setTextColor(fgColor, bgColor);
  tft.setCursor(x, y);
  tft.print(text);
  tft.setTextColor(fgColor, bgColor);
  tft.setCursor(x + 1, y);
  tft.print(text);
}
void drawWifiArc(int cx, int cy, int r, uint16_t color) {
  for (int i = 45; i <= 135; i++) {
    float angle = i * 3.14159 / 180.0;
    int px = cx + cos(angle) * r;
    int py = cy - sin(angle) * r;
    tft.fillCircle(px, py, 1, color);
  }
}

void drawWifiIcon(int x, int y, uint16_t color, bool showWarning = false) {
  int cx = x + 15;
  int cy = y + 24;
  tft.fillCircle(cx, cy, 2, color);
  drawWifiArc(cx, cy, 8, color);
  drawWifiArc(cx, cy, 14, color);
  drawWifiArc(cx, cy, 20, color);

  if (showWarning) {
    int wx = x + 23;
    int wy = y + 16;
    tft.fillCircle(wx, wy, 8, TFT_DARKGREY);
    tft.drawFastVLine(wx, wy - 4, 6, TFT_WHITE);
    tft.drawFastVLine(wx + 1, wy - 4, 6, TFT_WHITE);
    tft.fillCircle(wx, wy + 5, 1, TFT_WHITE);
  }
}

void drawBatteryIcon(int x, int y, int percentage) {
  tft.drawRect(x, y, 26, 14, TFT_WHITE);
  tft.fillRect(x + 26, y + 4, 3, 6, TFT_WHITE);
  tft.fillRect(x + 1, y + 1, 24, 12, TFT_RED);
  uint16_t color = (percentage <= 20) ? tft.color565(255, 255, 0) : TFT_GREEN;
  int fillW = (percentage * 24) / 100;
  if (fillW > 0) tft.fillRect(x + 1, y + 1, fillW, 12, color);
}

void drawThermoIcon(int x, int y) {
  tft.fillRoundRect(x, y, 6, 14, 3, TFT_WHITE);
  tft.fillCircle(x + 3, y + 15, 5, TFT_WHITE);
  tft.fillCircle(x + 3, y + 15, 3, TFT_RED);
  tft.fillRect(x + 2, y + 4, 2, 10, TFT_RED);
}

void drawTopBar() {
  tft.fillRect(0, 0, 480, 36, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextFont(4);
  time_t now;
  time(&now);
  struct tm* timeinfo = localtime(&now);
  char timeStr[10];
  if (now < 1700000000) sprintf(timeStr, "--:--:--");
  else sprintf(timeStr, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  tft.setCursor(5, 6);
  tft.print(timeStr);
  drawThermoIcon(135, 8);
  tft.setCursor(150, 6);
  tft.print(currentTemp);
  int tempX = 150 + tft.textWidth(currentTemp);
  tft.drawCircle(tempX + 3, 10, 2, TFT_WHITE);
  tft.setCursor(tempX + 7, 6);
  tft.print("C");
  int bat = getBatteryPercentage();
  tft.drawRightString(String(bat) + "%", 435, 6, 4);
  drawBatteryIcon(442, 11, bat);

  bool wifiError = (WiFi.status() != WL_CONNECTED) || (WiFi.localIP().toString() == "0.0.0.0");
  int wifiPct = wifiError ? 0 : getWifiPercentage();
  drawWifiIcon(244, 6, TFT_WHITE, wifiError);
  tft.drawString(String(wifiPct) + "%", 278, 6, 4);
}
void drawFooterBar() {
  tft.fillRect(0, 284, 480, 36, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextFont(2);
  tft.setCursor(12, 292);
  tft.print(FIRMWARE_VERSION);
  tft.drawBitmap(100, 290, refresh_icon_data, 24, 24, TFT_WHITE, TFT_RED);
  tft.drawBitmap(228, 290, setting_icon_data, 24, 24, TFT_WHITE, TFT_RED);
  tft.drawBitmap(356, 290, sleep_icon_data, 24, 24, TFT_WHITE, TFT_RED);
}

void drawUIFramework() {
  currentScreen = 0;
  tft.fillScreen(TFT_WHITE);
  drawTopBar();
  drawFooterBar();

  // 💡 離線主頁顯示設計 (⚙️ 紅色 Icon 圖文混排置中)
  if (showOfflineWarning || (WiFi.status() != WL_CONNECTED)) {
    if (font28_buffer != nullptr) tft.loadFont((const uint8_t*)font28_buffer);
    else tft.loadFont(LittleFS, "/Noto28.vlw");
    tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
    int textY = 120;
    tft.setCursor((480 - tft.textWidth("尚未連接 Wi-Fi")) / 2, textY);
    tft.print("尚未連接 Wi-Fi");

    int w1 = tft.textWidth("請按");
    int w2 = tft.textWidth("進入設定");
    int iconW = 24, gap = 6;
    int totalW = w1 + gap + iconW + gap + w2;
    int startX = (480 - totalW) / 2;
    int rowY = textY + 50;

    tft.setCursor(startX, rowY);
    tft.print("請按");
    tft.drawBitmap(startX + w1 + gap, rowY + 4, setting_icon_data, 24, 24, TFT_RED, TFT_WHITE);
    tft.setCursor(startX + w1 + gap + iconW + gap, rowY);
    tft.print("進入設定");

    tft.unloadFont();
    return;
  }

  tft.fillRect(238, 36, 4, 248, TFT_DARKGREY);
  if (layout_mode == 2) {
    tft.fillRect(242, 36, 238, 124, TFT_WHITE);
    tft.fillRect(242, 160, 238, 124, ROW_BG_LIGHT);
    tft.fillRect(242, 160, 238, 2, TFT_DARKGREY);
  } else if (layout_mode == 3) {
    tft.fillRect(242, 36, 238, 84, TFT_WHITE);
    tft.fillRect(242, 120, 238, 84, ROW_BG_LIGHT);
    tft.fillRect(242, 204, 238, 80, TFT_WHITE);
    tft.fillRect(242, 120, 238, 2, TFT_DARKGREY);
    tft.fillRect(242, 204, 238, 2, TFT_DARKGREY);
  }

  printChinese(busRoutes[0].route + " 往" + busRoutes[0].dest, 10, 42, COLOR_TITLE, TFT_WHITE, 24);
  if (layout_mode == 1) {
    printChinese(busRoutes[1].route + " 往" + busRoutes[1].dest, 250, 42, COLOR_TITLE, TFT_WHITE, 24);
  } else if (layout_mode == 2) {
    printChinese(busRoutes[1].route + " 往" + busRoutes[1].dest, 250, 36 + 8, COLOR_TITLE, TFT_WHITE, 24);
    printChinese(busRoutes[2].route + " 往" + busRoutes[2].dest, 250, 160 + 8, COLOR_TITLE, ROW_BG_LIGHT, 24);
  } else {
    printChinese(busRoutes[1].route + " 往" + busRoutes[1].dest, 250, 36 + 4, COLOR_TITLE, TFT_WHITE, 24);
    printChinese(busRoutes[2].route + " 往" + busRoutes[2].dest, 250, 120 + 4, COLOR_TITLE, ROW_BG_LIGHT, 24);
    printChinese(busRoutes[3].route + " 往" + busRoutes[3].dest, 250, 204 + 4, COLOR_TITLE, TFT_WHITE, 24);
  }
}

String cleanRemark(String rmk) {
  if (rmk.length() == 0) return rmk;
  int atPos = rmk.indexOf("@");
  if (atPos != -1) rmk = rmk.substring(0, atPos);
  atPos = rmk.indexOf("＠");
  if (atPos != -1) rmk = rmk.substring(0, atPos);
  rmk.replace("(", "");
  rmk.replace(")", "");
  rmk.replace("（", "");
  rmk.replace("）", "");
  rmk.trim();
  return rmk;
}

void updateMainRoute(JsonArray dataArray, String routeDir) {
  if (currentScreen != 0 || showOfflineWarning) return;
  tft.fillRect(0, 72, 238, 212, TFT_WHITE);
  if (dataArray.size() == 0 || !ntpSynced) {
    if (!ntpSynced) printChinese("正在同步時間...", 10, 145, TFT_DARKGREY, TFT_WHITE, 20);
    else printChinese("暫時沒有預定班次", 10, 145, TFT_DARKGREY, TFT_WHITE, 20);
    return;
  }

  int count = 0;
  bool hasValidBus = false;

  for (JsonObject item : dataArray) {
    if (count >= 3) break;
    if (String(item["dir"]) != routeDir) continue;

    const char* eta = item["eta"];
    const char* rmk = item["rmk_tc"];
    String rmkStr = (rmk != nullptr) ? String(rmk) : "";
    rmkStr = cleanRemark(rmkStr);

    int mins = -999;
    if (eta != nullptr && strlen(eta) > 0 && rmkStr.indexOf("最後班次已過") == -1) {
      mins = getMinutesUntil(eta);
    }

    hasValidBus = true;

    if (mins == -999) {
      printChinese("尾班車已過", 10, 145, TFT_DARKGREY, TFT_WHITE, 28);
      break;
    }

    int yMins = 0, yTime = 0;
    if (count == 0) {
      yMins = 85;
      yTime = 100;
    }
    if (count == 1) {
      yMins = 156;
      yTime = 171;
    }
    if (count == 2) {
      yMins = 227;
      yTime = 242;
    }

    String clock = getClockTime(eta);
    uint16_t minColor = (mins <= 8) ? COLOR_MINS_URGENT : COLOR_MINS_NORMAL;
    uint16_t clockColor = (mins <= 8) ? COLOR_MINS_URGENT : TFT_BLACK;

    if (mins <= 0) {
      printChinese("到站", 10, yMins + 8, TFT_RED, TFT_WHITE, 28);
    } else {
      tft.setTextColor(minColor, TFT_WHITE);
      tft.drawRightString(String(mins), 65, yMins, 6);
    }

    tft.setTextColor(clockColor, TFT_WHITE);
    tft.setTextFont(4);
    tft.setCursor(75, yTime);
    tft.print("(" + clock + ")");
    if (rmkStr.length() > 0 && rmkStr.indexOf("最後班次已過") == -1) {
      printChinese(rmkStr, 155, yTime + 5, TFT_DARKGREY, TFT_WHITE, 16);
    }

    if (count < 2) tft.drawFastHLine(10, (count == 0) ? 142 : 213, 218, tft.color565(200, 200, 200));
    count++;
  }

  if (!hasValidBus) printChinese("暫時沒有預定班次", 10, 145, TFT_DARKGREY, TFT_WHITE, 20);
}

void drawRightCell(String routeName, String destName, String routeDir, JsonArray dataArray, int startY, int height, uint16_t bgColor) {
  if (currentScreen != 0 || showOfflineWarning) return;

  int bgOffset = (layout_mode == 3) ? 28 : 34;
  tft.fillRect(242, startY + bgOffset, 238, height - bgOffset, bgColor);

  int yCenterNoBus = startY + bgOffset + (height - bgOffset) / 2 - 12;

  if (dataArray.size() == 0 || !ntpSynced) {
    if (!ntpSynced) printChinese("正在同步時間...", 250, yCenterNoBus, TFT_DARKGREY, bgColor, 16);
    else printChinese("暫時沒有預定班次", 250, yCenterNoBus, TFT_DARKGREY, bgColor, 16);
    return;
  }

  int count = 0;
  bool hasValidBus = false;

  if (layout_mode == 1) {
    for (JsonObject item : dataArray) {
      if (count >= 3) break;
      if (String(item["dir"]) != routeDir) continue;

      const char* eta = item["eta"];
      const char* rmk = item["rmk_tc"];
      String rmkStr = (rmk != nullptr) ? String(rmk) : "";
      rmkStr = cleanRemark(rmkStr);

      int mins = -999;
      if (eta != nullptr && strlen(eta) > 0 && rmkStr.indexOf("最後班次已過") == -1) {
        mins = getMinutesUntil(eta);
      }

      hasValidBus = true;

      if (mins == -999) {
        printChinese("尾班車已過", 250, 145, TFT_DARKGREY, bgColor, 28);
        break;
      }

      int yMins = 0, yTime = 0;
      if (count == 0) {
        yMins = 85;
        yTime = 100;
      }
      if (count == 1) {
        yMins = 156;
        yTime = 171;
      }
      if (count == 2) {
        yMins = 227;
        yTime = 242;
      }

      String clock = getClockTime(eta);
      uint16_t minColor = (mins <= 8) ? COLOR_MINS_URGENT : COLOR_MINS_NORMAL;
      uint16_t clockColor = (mins <= 8) ? COLOR_MINS_URGENT : TFT_BLACK;

      if (mins <= 0) {
        printChinese("到站", 250, yMins + 8, TFT_RED, bgColor, 28);
      } else {
        tft.setTextColor(minColor, bgColor);
        tft.drawRightString(String(mins), 305, yMins, 6);
      }

      tft.setTextColor(clockColor, bgColor);
      tft.setTextFont(4);
      tft.setCursor(315, yTime);
      tft.print("(" + clock + ")");
      if (rmkStr.length() > 0 && rmkStr.indexOf("最後班次已過") == -1) {
        printChinese(rmkStr, 395, yTime + 5, TFT_DARKGREY, bgColor, 16);
      }

      if (count < 2) tft.drawFastHLine(250, (count == 0) ? 142 : 213, 218, tft.color565(200, 200, 200));
      count++;
    }
  } else {
    int maxBuses = (layout_mode == 2) ? 3 : 2;
    int ySpacing = (layout_mode == 2) ? 28 : 25;
    int busStartY = (layout_mode == 2) ? startY + 35 : startY + 29;

    for (JsonObject item : dataArray) {
      if (count >= maxBuses) break;
      if (String(item["dir"]) != routeDir) continue;

      const char* eta = item["eta"];
      const char* rmk = item["rmk_tc"];
      String rmkStr = (rmk != nullptr) ? String(rmk) : "";
      rmkStr = cleanRemark(rmkStr);

      int mins = -999;
      if (eta != nullptr && strlen(eta) > 0 && rmkStr.indexOf("最後班次已過") == -1) {
        mins = getMinutesUntil(eta);
      }

      hasValidBus = true;

      if (mins == -999) {
        printChinese("尾班車已過", 250, yCenterNoBus, TFT_DARKGREY, bgColor, 20);
        break;
      }

      int yOffset = busStartY + (count * ySpacing);
      String clock = getClockTime(eta);
      clock.replace(":", " : ");
      String clockStr = "(" + clock + ")";
      uint16_t minColor = (mins <= 8) ? COLOR_MINS_URGENT : COLOR_MINS_NORMAL;
      uint16_t clockColor = (mins <= 8) ? COLOR_MINS_URGENT : TFT_BLACK;

      if (layout_mode == 2) {
        if (count == 0) {
          if (mins <= 0) {
            printChinese("到站", 248, yOffset, TFT_RED, bgColor, 24);
          } else {
            tft.setTextFont(4);
            int w = tft.textWidth(String(mins));
            printBold(String(mins), 300 - w, yOffset, 4, minColor, bgColor);
          }
          printBold(clockStr, 312, yOffset, 4, clockColor, bgColor);
          if (rmkStr.length() > 0 && rmkStr.indexOf("最後班次已過") == -1) printChinese(rmkStr, 400, yOffset + 4, TFT_DARKGREY, bgColor, 16);
        } else {
          if (mins <= 0) {
            printChinese("到站", 260, yOffset + 2, TFT_RED, bgColor, 16);
          } else {
            tft.setTextColor(minColor, bgColor);
            tft.drawRightString(String(mins), 300, yOffset, 4);
          }
          tft.setTextColor(clockColor, bgColor);
          tft.setTextFont(4);
          tft.setCursor(312, yOffset);
          tft.print(clockStr);
          if (rmkStr.length() > 0 && rmkStr.indexOf("最後班次已過") == -1) printChinese(rmkStr, 400, yOffset + 4, TFT_DARKGREY, bgColor, 16);
        }
      } else {
        if (count == 0) {
          if (mins <= 0) {
            printChinese("到站", 248, yOffset, TFT_RED, bgColor, 24);
          } else {
            tft.setTextFont(4);
            int w = tft.textWidth(String(mins));
            printBold(String(mins), 295 - w, yOffset + 2, 4, minColor, bgColor);
          }
          printBold(clockStr, 305, yOffset + 5, 2, clockColor, bgColor);
          if (rmkStr.length() > 0 && rmkStr.indexOf("最後班次已過") == -1) printChinese(rmkStr, 370, yOffset + 5, TFT_DARKGREY, bgColor, 16);
        } else {
          if (mins <= 0) {
            printChinese("到站", 260, yOffset + 2, TFT_RED, bgColor, 16);
          } else {
            tft.setTextColor(minColor, bgColor);
            tft.drawRightString(String(mins), 295, yOffset + 2, 2);
          }
          tft.setTextColor(clockColor, bgColor);
          tft.setTextFont(2);
          tft.setCursor(305, yOffset + 2);
          tft.print(clockStr);
          if (rmkStr.length() > 0 && rmkStr.indexOf("最後班次已過") == -1) printChinese(rmkStr, 370, yOffset + 4, TFT_DARKGREY, bgColor, 16);
        }
      }
      count++;
    }
  }
  if (!hasValidBus) printChinese("暫時沒有預定班次", 250, yCenterNoBus, TFT_DARKGREY, bgColor, 16);
}

void fetchAllBusData() {
  HTTPClient http;
  int numRoutes = (layout_mode == 1) ? 2 : ((layout_mode == 2) ? 3 : 4);
  for (int i = 0; i < numRoutes; i++) {
    if (busRoutes[i].url != "") {
      http.begin(busRoutes[i].url);
      http.setTimeout(8000);
      int code = http.GET();
      if (code == 200) busRoutes[i].jsonCache = http.getString();
      http.end();
    }
  }
}

void renderCachedBusData() {
  if (currentScreen != 0 || isFetching || showOfflineWarning) return;
  if (busRoutes[0].jsonCache != "") {
    DynamicJsonDocument doc0(4096);
    if (!deserializeJson(doc0, busRoutes[0].jsonCache)) updateMainRoute(doc0["data"], busRoutes[0].dir);
  }
  if (layout_mode == 1) {
    if (busRoutes[1].jsonCache != "") {
      DynamicJsonDocument doc1(4096);
      if (!deserializeJson(doc1, busRoutes[1].jsonCache)) drawRightCell(busRoutes[1].route, busRoutes[1].dest, busRoutes[1].dir, doc1["data"], 36, 248, TFT_WHITE);
    }
  } else if (layout_mode == 2) {
    if (busRoutes[1].jsonCache != "") {
      DynamicJsonDocument doc1(4096);
      if (!deserializeJson(doc1, busRoutes[1].jsonCache)) drawRightCell(busRoutes[1].route, busRoutes[1].dest, busRoutes[1].dir, doc1["data"], 36, 124, TFT_WHITE);
    }
    if (busRoutes[2].jsonCache != "") {
      DynamicJsonDocument doc2(4096);
      if (!deserializeJson(doc2, busRoutes[2].jsonCache)) drawRightCell(busRoutes[2].route, busRoutes[2].dest, busRoutes[2].dir, doc2["data"], 160, 124, ROW_BG_LIGHT);
    }
  } else {
    if (busRoutes[1].jsonCache != "") {
      DynamicJsonDocument doc1(4096);
      if (!deserializeJson(doc1, busRoutes[1].jsonCache)) drawRightCell(busRoutes[1].route, busRoutes[1].dest, busRoutes[1].dir, doc1["data"], 36, 84, TFT_WHITE);
    }
    if (busRoutes[2].jsonCache != "") {
      DynamicJsonDocument doc2(4096);
      if (!deserializeJson(doc2, busRoutes[2].jsonCache)) drawRightCell(busRoutes[2].route, busRoutes[2].dest, busRoutes[2].dir, doc2["data"], 120, 84, ROW_BG_LIGHT);
    }
    if (busRoutes[3].jsonCache != "") {
      DynamicJsonDocument doc3(4096);
      if (!deserializeJson(doc3, busRoutes[3].jsonCache)) drawRightCell(busRoutes[3].route, busRoutes[3].dest, busRoutes[3].dir, doc3["data"], 204, 80, TFT_WHITE);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  pinMode(TOUCH_INT_PIN, INPUT_PULLUP);

  bool result = power.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  delay(100);
  turnOnPMU();
  delay(100);

  tft.init();
  tft.setRotation(1);

  ledcAttach(LCD_BL_PIN, PWM_FREQUENCY, 8);

  preferences.begin("bus_eta", true);
  currentBrightness = preferences.getInt("brightness", 204);
  sleepMinutes = preferences.getUInt("sleepTime", 30);
  DATA_INTERVAL = preferences.getULong("refreshSec", 30000);
  savedSSID = preferences.getString("ssid", "");
  savedPass = preferences.getString("pass", "");
  layout_mode = preferences.getInt("layout", 3);
  weatherLoc = preferences.getString("weatherLoc", "香港天文台");

  busRoutes[0].route = preferences.getString("r0_route", "1");
  busRoutes[0].dest = preferences.getString("r0_dest", "尖沙咀碼頭");
  busRoutes[0].url = preferences.getString("r0_url", "https://data.etabus.gov.hk/v1/transport/kmb/eta/E507F6160447C129/1/1");
  busRoutes[0].dir = preferences.getString("r0_dir", "O");
  busRoutes[1].route = preferences.getString("r1_route", "1");
  busRoutes[1].dest = preferences.getString("r1_dest", "竹園邨");
  busRoutes[1].url = preferences.getString("r1_url", "https://data.etabus.gov.hk/v1/transport/kmb/eta/AB057A63C24E990E/1/1");
  busRoutes[1].dir = preferences.getString("r1_dir", "I");
  busRoutes[2].route = preferences.getString("r2_route", "1A");
  busRoutes[2].dest = preferences.getString("r2_dest", "中秀茂坪");
  busRoutes[2].url = preferences.getString("r2_url", "https://data.etabus.gov.hk/v1/transport/kmb/eta/AB057A63C24E990E/1A/1");
  busRoutes[2].dir = preferences.getString("r2_dir", "I");
  busRoutes[3].route = preferences.getString("r3_route", "1A");
  busRoutes[3].dest = preferences.getString("r3_dest", "尖沙咀碼頭");
  busRoutes[3].url = preferences.getString("r3_url", "https://data.etabus.gov.hk/v1/transport/kmb/eta/6F106FD26B684372/1A/1");
  busRoutes[3].dir = preferences.getString("r3_dir", "O");
  preferences.end();

  if (sleepMinutes == 0) currentSleepTime = 0;
  else currentSleepTime = sleepMinutes * 60000UL;
  ledcWrite(LCD_BL_PIN, currentBrightness);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed!");
  } else {
    fs::File f16 = LittleFS.open("/Noto16.vlw", "r");
    if (f16) {
      size_t size = f16.size();
      font16_buffer = (uint8_t*)ps_malloc(size);
      if (font16_buffer) f16.read(font16_buffer, size);
      f16.close();
    }
    fs::File f20 = LittleFS.open("/Noto20.vlw", "r");
    if (f20) {
      size_t size = f20.size();
      font20_buffer = (uint8_t*)ps_malloc(size);
      if (font20_buffer) f20.read(font20_buffer, size);
      f20.close();
    }
    fs::File f24 = LittleFS.open("/Noto24.vlw", "r");
    if (f24) {
      size_t size = f24.size();
      font24_buffer = (uint8_t*)ps_malloc(size);
      if (font24_buffer) f24.read(font24_buffer, size);
      f24.close();
    }
    fs::File f28 = LittleFS.open("/Noto28.vlw", "r");
    if (f28) {
      size_t size = f28.size();
      font28_buffer = (uint8_t*)ps_malloc(size);
      if (font28_buffer) f28.read(font28_buffer, size);
      f28.close();
    }
  }

  setupUnifiedWebServer();

  drawSplashScreen({ "Initializing..." });
  delay(500);

  if (savedSSID == "") {
    drawSplashScreen({ "Initialized.", "No Wi-Fi Found." }, true);
    delay(3000);
    startAPMode(false);
    return;
  }

  drawSplashScreen({ "Initialized.", "Connecting to Wi-Fi..." });
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  unsigned long startAttempt = millis();
  bool connected = false;
  while (millis() - startAttempt < 10000) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    delay(100);
  }

  if (connected) {
    showOfflineWarning = false;
    drawSplashScreen({ "Initialized.", "Wi-Fi Connected.", "Syncing Time..." });
    startNTP();
    unsigned long ntpStart = millis();
    while (!ntpSynced && millis() - ntpStart < 5000) {
      checkNTP();
      delay(100);
    }
    drawSplashScreen({ "Initialized.", "Wi-Fi Connected.", "Time Sync-ed.", "Fetching Bus Data..." });
    fetchWeather();
    fetchAllBusData();
    lastDataFetch = millis();
    lastWeatherUpdate = millis();
    drawUIFramework();
    renderCachedBusData();
    lastInteractionTime = millis();
  } else {
    drawSplashScreen({ "Initialized.", "Wi-Fi Failed!" }, true);
    delay(3000);
    startAPMode(false);
  }
}

void loop() {
  int touchX = 0, touchY = 0;
  checkNTP();

  if (currentScreen == 7) dnsServer.processNextRequest();
  server.handleClient();

  if (readTouch(touchX, touchY)) {
    if (!lastTouchState) {
      handleTouchAction(touchX, touchY);
      lastTouchState = true;
      delay(150);
    }
  } else {
    lastTouchState = false;
  }

  unsigned long currentMillis = millis();
  if (currentSleepTime > 0 && currentMillis - lastInteractionTime >= currentSleepTime) enterDeepSleepMode();

  static bool lastNtpState = false;
  if (ntpSynced && !lastNtpState) {
    if (currentScreen == 0) {
      drawTopBar();
      drawFooterBar();
    }
    lastNtpState = true;
  }

  unsigned long currentMillisUI = millis();
  if (currentScreen == 0 && (currentMillisUI - lastClockUpdate >= CLOCK_UPDATE_INTERVAL)) {
    drawTopBar();
    drawFooterBar();
    lastClockUpdate = currentMillisUI;
  }

  if (currentScreen == 0 && !showOfflineWarning && WiFi.status() == WL_CONNECTED) {
    if (ntpSynced && !isFetching && (lastDataFetch == 0 || (currentMillisUI - lastDataFetch >= DATA_INTERVAL))) {
      isFetching = true;
      xTaskCreatePinnedToCore([](void*) {
        unsigned long currentMillisUI = millis();
        if (lastWeatherUpdate == 0 || (currentMillisUI - lastWeatherUpdate >= 900000)) {
          fetchWeather();
          lastWeatherUpdate = currentMillisUI;
        }
        fetchAllBusData();
        newDataReady = true;
        isFetching = false;
        vTaskDelete(NULL);
      },"fetchTask", 10240, NULL, 1, NULL, 0);
      lastDataFetch = currentMillisUI;
    }
  }

  if (newDataReady) {
    newDataReady = false;
    if (currentScreen == 0 && !showOfflineWarning) {
      renderCachedBusData();
      drawTopBar();
    }
  }
  delay(10);
}