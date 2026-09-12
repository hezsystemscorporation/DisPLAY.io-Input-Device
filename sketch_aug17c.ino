#include <esp_now.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <Wire.h>

#define SDA_PIN 8
#define SCL_PIN 9

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, SCL_PIN, SDA_PIN);

String displayMessage = "";

volatile bool newDataReady = false;
char safeBuffer[255] = {0};

// Initialize FreeRTOS hardware spinlock for thread safety
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void updateDisplay() {
  u8g2.clearBuffer();                  
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  int y = 15;
  int strLen = displayMessage.length();
  int charsPerLine = 20; 
  
  for(int i = 0; i < strLen; i += charsPerLine) {
    String line = displayMessage.substring(i, min(i + charsPerLine, strLen));
    u8g2.drawStr(0, y, line.c_str());
    y += 15;
  }
  u8g2.sendBuffer();                   
}

// Hardware interrupt callback for incoming ESP-NOW packets
void OnDataRecv(const esp_now_recv_info *esp_now_info, const uint8_t *incomingData, int len) {
  if (len <= 0 || len >= 255) return;

  // Filter: Scan the packet for non-printable characters.
  // This silently drops stray RF packets from other devices in the environment.
  for(int i = 0; i < len; i++) {
    if(incomingData[i] != 0 && (incomingData[i] < 32 || incomingData[i] > 126)) {
      return; 
    }
  }

  // Lock memory block to prevent race conditions during the copy operation
  portENTER_CRITICAL_ISR(&mux);
  memset(safeBuffer, 0, sizeof(safeBuffer));       
  memcpy(safeBuffer, incomingData, len);
  newDataReady = true;                             
  portEXIT_CRITICAL_ISR(&mux);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize OLED and boot animation
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr); 
  u8g2.drawStr(25, 40, "By HEZ");     
  u8g2.sendBuffer();
  delay(2000);                        

  WiFi.mode(WIFI_STA);
  String macAddress = WiFi.macAddress();
  
  displayMessage = "MAC Address:\n" + macAddress;
  updateDisplay();
  
  // Forward boot status to host
  Serial.print("{\"event\":\"boot\", \"mac\":\"");
  Serial.print(macAddress);
  Serial.println("\"}");

  if (esp_now_init() != ESP_OK) {
    displayMessage = "ESP-NOW Init Error!";
    updateDisplay();
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  if (newDataReady) {
    
    // Lock memory block while extracting data to prevent ISR corruption
    portENTER_CRITICAL(&mux);
    String tempString = String(safeBuffer);
    newDataReady = false; 
    portEXIT_CRITICAL(&mux);
    
    displayMessage = tempString;
    
    updateDisplay();
    
    Serial.print("{\"event\":\"recv_data\", \"payload\":\"");
    Serial.print(displayMessage);
    Serial.println("\"}");
  }
  
  delay(20); 
}