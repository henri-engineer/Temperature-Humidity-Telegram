#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

// Intervalo de leitura otimizado (DHT22 recomenda mínimo 2s)
const unsigned long INTERVALO_LEITURA = 3000;  // 3 segundos
const unsigned long INTERVALO_DISPLAY = 1000;  // 1 segundo para atualizar hora

// Timeouts para reconexão
const unsigned long WIFI_TIMEOUT = 15000;      // 15s
const unsigned long MQTT_RETRY_INTERVAL = 5000; // 5s

// ------------------- OBJETOS -------------------
WiFiClient espClient;
PubSubClient client(espClient);

LiquidCrystal_I2C lcd1(0x27, 20, 4);  // Display 1 - Dados em tempo real
LiquidCrystal_I2C lcd2(0x20, 20, 4);  // Display 2 - Estatísticas

RTC_DS1307 rtc;
DHTesp dht;
const int DHT_PIN = 15;

// ------------------- VARIÁVEIS -------------------
// Estatísticas
float tempMax = -100.0, tempMin = 100.0;
float umidMax = -100.0, umidMin = 100.0;
float tempAtual = 0.0, umidAtual = 0.0;

// Controle de timing
unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastMqttAttempt = 0;

// Flags de status
bool sensorOk = false;
bool wifiConnected = false;
bool mqttConnected = false;
bool rtcOk = false;

// ------------------- FUNÇÕES DE CONECTIVIDADE -------------------
void setupWiFi() {
  Serial.println("\n=== Iniciando conexão Wi-Fi ===");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  unsigned long startAttempt = millis();
  
  while (WiFi.status() != WL_CONNECTED && 
         millis() - startAttempt < WIFI_TIMEOUT) {
    delay(100);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✓ Wi-Fi conectado!");
    Serial.print("✓ IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("✓ RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    wifiConnected = false;
    Serial.println("\n✗ Falha ao conectar Wi-Fi!");
  }
}

void setupMQTT() {
  client.setServer(mqtt_server, mqtt_port);
  client.setKeepAlive(60);
  client.setSocketTimeout(30);
}

bool reconnectMQTT() {
  // Evita tentativas muito frequentes
  if (millis() - lastMqttAttempt < MQTT_RETRY_INTERVAL) {
    return false;
  }
  
  lastMqttAttempt = millis();
  
  Serial.print("→ Conectando MQTT...");
  String clientId = "ESP32-DHT22-" + String(WiFi.macAddress());
  clientId.replace(":", "");
  
  if (client.connect(clientId.c_str())) {
    mqttConnected = true;
    Serial.println(" ✓ Conectado!");
    return true;
  } else {
    mqttConnected = false;
    Serial.print(" ✗ Falha (");
    Serial.print(client.state());
    Serial.println(")");
    return false;
  }
}

void sendMQTT(float temp, float umid) {
  if (!mqttConnected) return;
  
  // JSON compacto e eficiente
  char payload[60];
  snprintf(payload, sizeof(payload), 
           "{\"temperatura\":%.1f,\"umidade\":%.1f}", 
           temp, umid);
  
  if (client.publish(mqtt_topic, payload, false)) {
    Serial.println("✓ MQTT enviado: " + String(payload));
  } else {
    Serial.println("✗ Falha ao enviar MQTT");
    mqttConnected = false;
  }
}

// ------------------- FUNÇÕES DE DISPLAY -------------------
void updateLCD1(DateTime now, float temp, float umid) {
  char linha[21];
  
  // Linha 0: Data
  snprintf(linha, sizeof(linha), "Data: %02d/%02d/%04d", 
           now.day(), now.month(), now.year());
  lcd1.setCursor(0, 0);
  lcd1.print(linha);
  
  // Linha 1: Hora
  snprintf(linha, sizeof(linha), "Hora: %02d:%02d:%02d", 
           now.hour(), now.minute(), now.second());
  lcd1.setCursor(0, 1);
  lcd1.print(linha);
  
  // Linha 2: Temperatura
  snprintf(linha, sizeof(linha), "Temp: %.1f%cC    ", temp, 223);
  lcd1.setCursor(0, 2);
  lcd1.print(linha);
  
  // Linha 3: Umidade
  snprintf(linha, sizeof(linha), "Umid: %.1f%%     ", umid);
  lcd1.setCursor(0, 3);
  lcd1.print(linha);
}

void updateLCD2(DateTime now) {
  char linha[21];
  
  // Linha 0: Título
  lcd2.setCursor(0, 0);
  lcd2.print("  << Estatisticas >>  ");
  
  // Linha 1: Timestamp
  snprintf(linha, sizeof(linha), "%02d/%02d %02d:%02d:%02d", 
           now.day(), now.month(), now.hour(), now.minute(), now.second());
  lcd2.setCursor(0, 1);
  lcd2.print(linha);
  lcd2.print("     ");
  
  // Linha 2: Temperaturas máx/mín
  snprintf(linha, sizeof(linha), "T:%.1f%c %.1f%c   ", 
           tempMax, 223, tempMin, 223);
  lcd2.setCursor(0, 2);
  lcd2.print(linha);
  
  // Linha 3: Umidades máx/mín
  snprintf(linha, sizeof(linha), "U:%.1f%% %.1f%%   ", 
           umidMax, umidMin);
  lcd2.setCursor(0, 3);
  lcd2.print(linha);
}

void showStartupScreen() {
  lcd1.clear();
  lcd1.setCursor(3, 0);
  lcd1.print("Sistema DHT22");
  lcd1.setCursor(2, 1);
  lcd1.print("Inicializando...");
  
  lcd2.clear();
  lcd2.setCursor(4, 1);
  lcd2.print("Aguarde...");
}

void showStatus() {
  lcd1.setCursor(0, 3);
  lcd1.print("W");
  lcd1.print(wifiConnected ? "+" : "-");
  lcd1.print(" M");
  lcd1.print(mqttConnected ? "+" : "-");
  lcd1.print(" S");
  lcd1.print(sensorOk ? "+" : "-");
}

// ------------------- LEITURA DE SENSORES -------------------
bool readSensors() {
  TempAndHumidity data = dht.getTempAndHumidity();
  
  // Valida leitura
  if (isnan(data.temperature) || isnan(data.humidity)) {
    Serial.println("✗ Erro na leitura do DHT22");
    sensorOk = false;
    return false;
  }
  
  // Valida range (DHT22: -40~80°C, 0~100%)
  if (data.temperature < -40 || data.temperature > 80 ||
      data.humidity < 0 || data.humidity > 100) {
    Serial.println("✗ Leitura fora do range válido");
    sensorOk = false;
    return false;
  }
  
  sensorOk = true;
  tempAtual = data.temperature;
  umidAtual = data.humidity;
  
  // Atualiza máximas e mínimas
  if (tempAtual > tempMax) tempMax = tempAtual;
  if (tempAtual < tempMin) tempMin = tempAtual;
  if (umidAtual > umidMax) umidMax = umidAtual;
  if (umidAtual < umidMin) umidMin = umidAtual;
  
  return true;
}

// ------------------- SETUP -------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n╔════════════════════════════════════════╗");
  Serial.println("║   Sistema de Monitoramento DHT22     ║");
  Serial.println("║   Temperatura & Umidade com MQTT     ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Inicializa I2C
  Wire.begin();
  
  // Inicializa LCDs
  Serial.print("→ Inicializando displays...");
  lcd1.init();
  lcd1.backlight();
  lcd2.init();
  lcd2.backlight();
  showStartupScreen();
  Serial.println(" ✓");
  delay(1000);
  
  // Inicializa RTC
  Serial.print("→ Inicializando RTC...");
  if (rtc.begin()) {
    rtcOk = true;
    if (!rtc.isrunning()) {
      Serial.println(" ! Ajustando horário");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    } else {
      Serial.println(" ✓");
    }
  } else {
    Serial.println(" ✗ Falha!");
    rtcOk = false;
  }
  
  // Inicializa DHT22
  Serial.print("→ Inicializando DHT22...");
  dht.setup(DHT_PIN, DHTesp::DHT22);
  Serial.println(" ✓");
  delay(2000); // DHT22 precisa de tempo para estabilizar
  
  // Conecta Wi-Fi
  setupWiFi();
  
  // Configura MQTT
  if (wifiConnected) {
    setupMQTT();
  }
  
  Serial.println("\n=== Sistema Iniciado ===\n");
}

// ------------------- LOOP PRINCIPAL -------------------
void loop() {
  unsigned long currentMillis = millis();
  
  // === GESTÃO DE CONECTIVIDADE ===
  // Verifica Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      Serial.println("✗ Wi-Fi desconectado!");
      wifiConnected = false;
      mqttConnected = false;
    }
    setupWiFi();
  } else {
    wifiConnected = true;
  }
  
  // Verifica/reconecta MQTT
  if (wifiConnected) {
    if (!client.connected()) {
      if (mqttConnected) {
        Serial.println("✗ MQTT desconectado!");
        mqttConnected = false;
      }
      reconnectMQTT();
    }
    client.loop(); // Mantém conexão MQTT ativa
  }
  
  // === LEITURA DE SENSORES ===
  if (currentMillis - lastSensorRead >= INTERVALO_LEITURA) {
    lastSensorRead = currentMillis;
    
    if (readSensors()) {
      // Envia dados via MQTT se conectado
      if (mqttConnected) {
        sendMQTT(tempAtual, umidAtual);
      }
      
      // Log no Serial
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      Serial.printf("📊 Temp: %.1f°C | Umid: %.1f%%\n", tempAtual, umidAtual);
      Serial.printf("📈 Max: T=%.1f°C U=%.1f%% | Min: T=%.1f°C U=%.1f%%\n", 
                    tempMax, umidMax, tempMin, umidMin);
    }
  }
  
  // === ATUALIZAÇÃO DOS DISPLAYS ===
  if (rtcOk && currentMillis - lastDisplayUpdate >= INTERVALO_DISPLAY) {
    lastDisplayUpdate = currentMillis;
    
    DateTime now = rtc.now();
    
    if (sensorOk) {
      updateLCD1(now, tempAtual, umidAtual);
      updateLCD2(now);
    } else {
      // Mostra mensagem de erro
      lcd1.setCursor(0, 2);
      lcd1.print("Erro ao ler sensor!");
    }
    
    // Mostra status de conexão (canto inferior esquerdo LCD1)
    showStatus();
  }
  
  // Pequeno delay para não sobrecarregar o processador
  delay(10);
}
