/**
* Cria um Access Point da sua internet via http request.
* Este projeto tem mapeado um display 16x2 para exibir o status do AP exibindo o IP que ele obteve da rede quando conectado corretamente.
* Quando há o request na rota para criação do AP, o ESP32 exibe no display o SSID e Senha criados de forma aleatória.
*/


#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define I2C_SDA 8
#define I2C_SCL 9

LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* ssid_local = "ssd-name-here";
const char* password_local = "ssd-pass-here";

WebServer server(80);

bool apAtivo = false;
unsigned long apTempoInicio = 0;
const unsigned long TEMPO_DURACAO_AP = 10 * 60 * 1000;
String apSSID = "";
String apPassword = "";

// IPs do AP — fixos para podermos passar o DNS aos clientes
IPAddress ap_ip(192, 168, 4, 1);
IPAddress ap_gw(192, 168, 4, 1);
IPAddress ap_mask(255, 255, 255, 0);
IPAddress ap_leaseStart(192, 168, 4, 2);
IPAddress ap_dns(8, 8, 8, 8);

String gerarStringAleatoria(int tamanho) {
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String resultado = "";
  for (int i = 0; i < tamanho; i++) {
    resultado += charset[random(0, sizeof(charset) - 1)];
  }
  return resultado;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");

  // Importante: já entra em AP_STA desde o boot.
  // Trocar de modo depois (STA -> AP_STA -> STA) atrapalha o NAPT.
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid_local, password_local);

  Serial.println("\n--- BOOT INICIADO ---");
  Serial.print("Conectando a rede local");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[OK] Conectado! IP: " + WiFi.localIP().toString());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PDV Pronto.");
  lcd.setCursor(0, 1);
  lcd.print("IP:");
  lcd.print(WiFi.localIP().toString());

  server.on("/ap/status", HTTP_GET, handleStatus);
  server.on("/ap/ativar", HTTP_POST, handleAtivar);
  server.on("/ap/desativar", HTTP_POST, handleDesativar);

  server.begin();
}

void loop() {
  server.handleClient();

  if (apAtivo && (millis() - apTempoInicio >= TEMPO_DURACAO_AP)) {
    desativarAP();
    Serial.println("Tempo expirado. AP desativado.");
  }
}

void handleStatus() {
  StaticJsonDocument<200> json;
  json["ativo"] = apAtivo;
  if (apAtivo) {
    json["segundos_restantes"] = (TEMPO_DURACAO_AP - (millis() - apTempoInicio)) / 1000;
  } else {
    json["segundos_restantes"] = 0;
  }
  String resposta;
  serializeJson(json, resposta);
  server.send(200, "application/json", resposta);
}

void handleAtivar() {
  if (apAtivo) {
    server.send(400, "application/json", "{\"erro\": \"AP ja ativo.\"}");
    return;
  }

  apSSID = "PIX_" + gerarStringAleatoria(4);
  apPassword = gerarStringAleatoria(8);

  // --- Configura o AP usando a nova API do core 3.x ---
  // Configura IP/máscara/DHCP/DNS ANTES de subir o SSID.
  // Isso garante que os clientes recebam um DNS válido (8.8.8.8) via DHCP.
  WiFi.AP.begin();
  WiFi.AP.config(ap_ip, ap_gw, ap_mask, ap_leaseStart, ap_dns);
  WiFi.AP.create(apSSID.c_str(), apPassword.c_str());

  if (!WiFi.AP.waitStatusBits(ESP_NETIF_STARTED_BIT, 2000)) {
    Serial.println("[ERRO] Falha ao iniciar AP.");
    server.send(500, "application/json", "{\"erro\": \"Falha ao iniciar AP\"}");
    return;
  }

  if (WiFi.AP.enableNAPT(true)) {
    Serial.println("[OK] NAPT ativado com sucesso.");
  } else {
    Serial.println("[ERRO] enableNAPT retornou falso. Verifique a versao do core.");
  }

  apAtivo = true;
  apTempoInicio = millis();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("NET: ");
  lcd.print(apSSID);
  lcd.setCursor(0, 1);
  lcd.print("PWD: ");
  lcd.print(apPassword);

  StaticJsonDocument<400> json;
  json["status"] = "Ativado";
  json["ssid"] = apSSID;
  json["password"] = apPassword;
  json["ip_conexao"] = WiFi.AP.localIP().toString();
  json["segundos_restantes"] = TEMPO_DURACAO_AP / 1000;
  json["qr_code_string"] = "WIFI:S:" + apSSID + ";T:WPA;P:" + apPassword + ";;";
  String resposta;
  serializeJson(json, resposta);
  server.send(200, "application/json", resposta);
}

void handleDesativar() {
  if (!apAtivo) {
    server.send(400, "application/json", "{\"erro\": \"Nenhuma rede ativa.\"}");
    return;
  }
  desativarAP();
  server.send(200, "application/json", "{\"status\": \"Desativado com sucesso\"}");
}

void desativarAP() {
  WiFi.AP.enableNAPT(false);
  WiFi.AP.end();
  // Mantemos WIFI_AP_STA — não voltamos para WIFI_STA puro,
  // para não derrubar a conexao com a rede local nem reordenar interfaces.

  apAtivo = false;
  apSSID = "";
  apPassword = "";

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PDV Pronto.");
  lcd.setCursor(0, 1);
  lcd.print("IP: ");
  lcd.print(WiFi.localIP().toString());
}
