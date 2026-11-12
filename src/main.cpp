/*
  ESP32 + DHT11 (temperatura) + SW-420 (vibração) + BME280/BMP280 (pressão)
  Tudo em uma única página web com três gráficos em tempo real.

  Bibliotecas (Gerenciador de Bibliotecas da Arduino IDE):
    - "DHT sensor library"        (Adafruit)
    - "Adafruit Unified Sensor"   (Adafruit)
    - "Adafruit BME280 Library"   (Adafruit)
    - "Adafruit BMP280 Library"   (Adafruit)   <- só é usada se o módulo for BMP280

  Ligação (veja o arquivo esquema_esp32_sensores.svg):
    DHT11   DATA -> GPIO 4   (VCC -> 3V3, GND -> GND, resistor 10k entre DATA e VCC se for de 4 pinos)
    SW-420  DO   -> GPIO 14  (VCC -> 3V3, GND -> GND)
    BME280  SDA  -> GPIO 23  (VCC -> 3V3, GND -> GND, CSB -> 3V3, SDO -> GND)
    BME280  SCL  -> GPIO 19

  Depois de gravar, abra o Monitor Serial (115200) para ver o IP
  e acesse esse IP no navegador (na mesma rede Wi-Fi).
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
const char* WIFI_SSID = "";
const char* WIFI_SENHA = "";
#endif

// Pinos
#define DHTPIN       4
#define DHTTYPE      DHT11
#define PINO_VIB     14
#define PINO_SDA     23
#define PINO_SCL     19

DHT dht(DHTPIN, DHTTYPE);
Adafruit_BME280 bme;
Adafruit_BMP280 bmp;
WebServer server(80);

enum TipoPressao { NENHUM, TIPO_BME280, TIPO_BMP280 };
TipoPressao tipoPressao = NENHUM;

// Valores mais recentes
float temperatura = NAN;   // DHT11
float umidade     = NAN;   // DHT11
float pressaoHpa  = NAN;   // BME280/BMP280
float temp2       = NAN;   // temperatura do BME280/BMP280
float umid2       = NAN;   // umidade (só no BME280)

// Vibração
volatile unsigned long pulsosVib = 0;
volatile unsigned long ultimoPulso = 0;
unsigned long pulsosAnterior = 0;
unsigned long vibNoIntervalo = 0;

unsigned long ultimaLeitura = 0;
const unsigned long INTERVALO_MS = 2000;
bool servidorIniciado = false;

void IRAM_ATTR isrVibracao() {
  pulsosVib++;
  ultimoPulso = millis();
}

const char PAGINA[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 - Sensores</title>
<style>
  body { font-family: system-ui, sans-serif; margin: 0; padding: 20px; background: #101418; color: #e8eaed; max-width: 900px; margin-inline: auto; }
  h1 { font-size: 1.2rem; margin: 0 0 16px; font-weight: 600; }
  .bloco { background: #1b2128; border-radius: 12px; padding: 14px; margin-bottom: 14px; }
  .topo { display: flex; justify-content: space-between; align-items: baseline; margin-bottom: 8px; flex-wrap: wrap; gap: 8px; }
  .topo small { color: #9aa4af; }
  .valor { font-size: 2rem; font-weight: 700; }
  .sec { color: #9aa4af; font-size: .85rem; }
  canvas { width: 100%; height: 170px; display: block; }
  #status { color: #9aa4af; font-size: .85rem; }
  .vib-ok { color: #66bb6a; } .vib-alerta { color: #ef5350; }
</style>
</head>
<body>
  <h1>Painel de sensores - ESP32</h1>

  <div class="bloco">
    <div class="topo">
      <div><small>Temperatura (DHT11)</small><br><span class="valor" style="color:#ff7043"><span id="t">--</span> °C</span></div>
      <div class="sec">Umidade DHT11: <span id="u">--</span> %<br><span id="t2txt"></span></div>
    </div>
    <canvas id="g_temp"></canvas>
  </div>

  <div class="bloco">
    <div class="topo">
      <div><small>Pressão (<span id="tipoP">--</span>)</small><br><span class="valor" style="color:#4fc3f7"><span id="p">--</span> hPa</span></div>
      <div class="sec" id="ptxt"></div>
    </div>
    <canvas id="g_pres"></canvas>
  </div>

  <div class="bloco">
    <div class="topo">
      <div><small>Vibração (SW-420)</small><br><span class="valor vib-ok" id="vib">--</span></div>
      <div class="sec">Mudanças de sinal nos últimos 2 s: <span id="vn">0</span></div>
    </div>
    <canvas id="g_vib"></canvas>
  </div>

  <div id="status">Aguardando dados...</div>

<script>
const MAX = 60;  // 60 pontos x 2 s = últimos 2 minutos
const dados = { temp: [], pres: [], vib: [] };

function grafico(id, serie, cor, tipo, faixaMin) {
  const c = document.getElementById(id), ctx = c.getContext('2d');
  const dpr = window.devicePixelRatio || 1, w = c.clientWidth, h = c.clientHeight;
  c.width = w * dpr; c.height = h * dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  if (serie.length < 2) return;

  const padE = 46, padD = 10, padT = 12, padB = 12;
  let min = Math.min(...serie), max = Math.max(...serie);
  if (tipo === 'barra') { min = 0; max = Math.max(max, 5); }
  else if (max - min < faixaMin) { const m = (max + min) / 2; min = m - faixaMin / 2; max = m + faixaMin / 2; }
  else { min -= 0.5; max += 0.5; }

  const x = i => padE + i * (w - padE - padD) / (MAX - 1);
  const y = v => padT + (max - v) * (h - padT - padB) / (max - min);

  ctx.strokeStyle = '#2a323b'; ctx.fillStyle = '#9aa4af'; ctx.font = '11px system-ui'; ctx.lineWidth = 1;
  for (let k = 0; k <= 3; k++) {
    const v = min + (max - min) * k / 3;
    ctx.beginPath(); ctx.moveTo(padE, y(v)); ctx.lineTo(w - padD, y(v)); ctx.stroke();
    ctx.fillText(v.toFixed(tipo === 'barra' ? 0 : 1), 4, y(v) + 4);
  }

  ctx.strokeStyle = cor; ctx.fillStyle = cor;
  if (tipo === 'barra') {
    const bw = Math.max(2, (w - padE - padD) / MAX - 2);
    serie.forEach((v, i) => ctx.fillRect(x(i) - bw / 2, y(v), bw, y(0) - y(v)));
  } else {
    ctx.lineWidth = 2.5; ctx.lineJoin = 'round';
    ctx.beginPath();
    serie.forEach((v, i) => i ? ctx.lineTo(x(i), y(v)) : ctx.moveTo(x(i), y(v)));
    ctx.stroke();
    ctx.beginPath(); ctx.arc(x(serie.length - 1), y(serie[serie.length - 1]), 4, 0, 7); ctx.fill();
  }
}

function empilhar(lista, v) {
  if (v === null || v === undefined) return;
  lista.push(v);
  if (lista.length > MAX) lista.shift();
}

function desenharTudo() {
  grafico('g_temp', dados.temp, '#ff7043', 'linha', 2);
  grafico('g_pres', dados.pres, '#4fc3f7', 'linha', 1);
  grafico('g_vib',  dados.vib,  '#ab47bc', 'barra', 0);
}

async function atualizar() {
  const st = document.getElementById('status');
  try {
    const d = await (await fetch('/dados')).json();

    document.getElementById('t').textContent = d.temp !== null ? d.temp.toFixed(1) : '--';
    document.getElementById('u').textContent = d.umid !== null ? d.umid.toFixed(0) : '--';
    empilhar(dados.temp, d.temp);

    document.getElementById('tipoP').textContent = d.sensorP;
    if (d.pres !== null) {
      document.getElementById('p').textContent = d.pres.toFixed(1);
      let extra = 'Temperatura do ' + d.sensorP + ': ' + d.temp2.toFixed(1) + ' °C';
      if (d.umid2 !== null) extra += ' | Umidade: ' + d.umid2.toFixed(0) + ' %';
      document.getElementById('ptxt').textContent = extra;
      empilhar(dados.pres, d.pres);
    } else {
      document.getElementById('p').textContent = '--';
      document.getElementById('ptxt').textContent = 'Sensor de pressão não encontrado (confira SDA/SCL).';
    }

    const el = document.getElementById('vib');
    el.textContent = d.vib ? 'VIBRANDO' : 'Parado';
    el.className = 'valor ' + (d.vib ? 'vib-alerta' : 'vib-ok');
    document.getElementById('vn').textContent = d.vibN;
    empilhar(dados.vib, d.vibN);

    desenharTudo();
    st.textContent = 'Atualizado às ' + new Date().toLocaleTimeString();
  } catch (e) {
    st.textContent = 'Sem conexão com o ESP32.';
  }
}

window.addEventListener('resize', desenharTudo);
atualizar();
setInterval(atualizar, 2000);
</script>
</body>
</html>
)rawliteral";

String num(float v, int casas) {
  return isnan(v) ? String("null") : String(v, casas);
}

void iniciarPressao() {
  Wire.begin(PINO_SDA, PINO_SCL);
  const uint8_t enderecos[] = {0x76, 0x77};

  for (uint8_t a : enderecos) {
    if (bme.begin(a)) {
      tipoPressao = TIPO_BME280;
      Serial.printf("BME280 encontrado em 0x%02X\n", a);
      return;
    }
  }
  for (uint8_t a : enderecos) {
    if (bmp.begin(a)) {
      tipoPressao = TIPO_BMP280;
      Serial.printf("BMP280 encontrado em 0x%02X\n", a);
      return;
    }
  }
  Serial.println("Nenhum sensor de pressao encontrado (confira SDA/SCL, CSB e SDO)");
}

void lerSensores() {
  // DHT11
  float t = dht.readTemperature();
  float u = dht.readHumidity();
  temperatura = isnan(t) ? NAN : t;
  umidade     = isnan(u) ? NAN : u;

  // Pressão
  pressaoHpa = NAN; temp2 = NAN; umid2 = NAN;
  if (tipoPressao == TIPO_BME280) {
    pressaoHpa = bme.readPressure() / 100.0F;
    temp2 = bme.readTemperature();
    umid2 = bme.readHumidity();
  } else if (tipoPressao == TIPO_BMP280) {
    pressaoHpa = bmp.readPressure() / 100.0F;
    temp2 = bmp.readTemperature();
  }

  // Vibração: pulsos contados desde a última leitura
  noInterrupts();
  unsigned long p = pulsosVib;
  interrupts();
  vibNoIntervalo = p - pulsosAnterior;
  pulsosAnterior = p;

  if (isnan(temperatura) || isnan(umidade)) {
    Serial.println("DHT11: sem leitura valida (verifique DATA no GPIO 4 e o pull-up de 10k).");
  } else {
    Serial.printf("DHT11: %.1f C %.0f %%", temperatura, umidade);
  }

  if (tipoPressao == NENHUM) {
    Serial.print(" | Pressao: sensor nao detectado (verifique SDA GPIO 23, SCL GPIO 19 e endereco).");
  } else if (isnan(pressaoHpa)) {
    Serial.print(" | Pressao: sem leitura valida.");
  } else {
    Serial.printf(" | Pressao: %.1f hPa", pressaoHpa);
  }
  Serial.printf(" | Vibracao: %lu transicoes nos ultimos 2 s\n", vibNoIntervalo);
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  iniciarPressao();

  pinMode(PINO_VIB, INPUT);
  Serial.printf("SW-420: lendo saida DO no GPIO %d (nivel inicial: %s)\n",
                PINO_VIB, digitalRead(PINO_VIB) == HIGH ? "HIGH" : "LOW");
  attachInterrupt(digitalPinToInterrupt(PINO_VIB), isrVibracao, CHANGE);

  lerSensores();

  if (WIFI_SSID[0] != '\0') {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_SENHA);
    Serial.println("Conectando ao Wi-Fi em segundo plano; leituras dos sensores continuam no Monitor Serial.");
  } else {
    Serial.println("Wi-Fi nao configurado. Copie src/wifi_secrets.h.example para src/wifi_secrets.h e informe sua rede.");
  }

  server.on("/", []() {
    server.send_P(200, "text/html", PAGINA);
  });

  server.on("/dados", []() {
    const char* nomeP = tipoPressao == TIPO_BME280 ? "BME280" :
                        tipoPressao == TIPO_BMP280 ? "BMP280" : "nenhum";
    bool vibrando = pulsosVib > 0 && (millis() - ultimoPulso) < 1500;

    String json = "{";
    json += "\"temp\":"  + num(temperatura, 1) + ",";
    json += "\"umid\":"  + num(umidade, 0) + ",";
    json += "\"pres\":"  + num(pressaoHpa, 1) + ",";
    json += "\"temp2\":" + num(temp2, 1) + ",";
    json += "\"umid2\":" + num(umid2, 0) + ",";
    json += "\"sensorP\":\"" + String(nomeP) + "\",";
    json += "\"vib\":"   + String(vibrando ? "true" : "false") + ",";
    json += "\"vibN\":"  + String(vibNoIntervalo);
    json += "}";

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
  });

}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!servidorIniciado) {
      server.begin();
      servidorIniciado = true;
      Serial.print("Conectado! Acesse: http://");
      Serial.println(WiFi.localIP());
    }
    server.handleClient();
  }

  if (millis() - ultimaLeitura >= INTERVALO_MS) {
    ultimaLeitura = millis();
    lerSensores();
  }
}
