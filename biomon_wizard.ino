#include "WiFi.h"
#include <Adafruit_ADS1X15.h>
#include "src/mongoose_glue.h"
#include "time.h"
#include <ESP_Google_Sheet_Client.h>
#include <cstring>
#include <Preferences.h>

#define S0 32 //LSB
#define S1 33
#define S2 25
#define S3 26 //MSB

// Crie variáveis globais que guardarão o Wi-Fi atual
String current_ssid = "";
String current_pass = "";

Preferences preferences; // Objeto para acessar a memória do ESP32
#define PROJECT_ID ""
#define CLIENT_EMAIL ""

const char PRIVATE_KEY[] PROGMEM = "";
const char spreadsheetId[] = "";

const char* ntpServer = "pool.ntp.org";
// --- Variáveis do Google Sheets e Batching ---
double intervaloDesejadoUsuario = 1.0; // Padrão de 1 segundo
unsigned long ultimoTempoLeituraBatch = 0;
unsigned long ultimoTempoEnvioSheets = 0;

#define TAMANHO_MAX_BUFFER 10 // Máximo de leituras no pacote
struct Leitura {
  double sheetsDate;
  double t[12];
  bool ativa[12];
};
Leitura bufferLeituras[TAMANHO_MAX_BUFFER];
int leiturasNoBuffer = 0;
unsigned long epochTime;
unsigned long ultimoTempoSensor = 0;
const unsigned long intervaloSensor = 250; // Lê as baterias 4 vezes por segundo
bool dashboardAtiva = true; // Começa ligada por padrão

struct config_wifi {
  char ssid_atual[32];
  char pass_atual[64];
  char novo_ssid[32];
  char novo_pass[64];
};

Adafruit_ADS1115 ads;

// Estado local dos 12 botões/células
static struct gerenciar_celula s_gerenciar_celula = {
  false, false, false, false, false, false, false, false, false, false, false, false
};

// Snapshot da última leitura, usado na interface e no envio ao Sheets
bool celulasAtivasUltimaLeitura[12] = {
  false, false, false, false, false, false, false, false, false, false, false, false
};

void tokenStatusCallback(TokenInfo info);

void my_get_gerenciar_celula(struct gerenciar_celula *data);
void my_set_gerenciar_celula(struct gerenciar_celula *data);

static inline void copiarCelulasParaArray(bool destino[12]) {
  destino[0]  = s_gerenciar_celula.c01;
  destino[1]  = s_gerenciar_celula.c02;
  destino[2]  = s_gerenciar_celula.c03;
  destino[3]  = s_gerenciar_celula.c04;
  destino[4]  = s_gerenciar_celula.c05;
  destino[5]  = s_gerenciar_celula.c06;
  destino[6]  = s_gerenciar_celula.c07;
  destino[7]  = s_gerenciar_celula.c08;
  destino[8]  = s_gerenciar_celula.c09;
  destino[9]  = s_gerenciar_celula.c10;
  destino[10] = s_gerenciar_celula.c11;
  destino[11] = s_gerenciar_celula.c12;
}

static inline void copiarArrayParaCelulas(const bool origem[12]) {
  s_gerenciar_celula.c01 = origem[0];
  s_gerenciar_celula.c02 = origem[1];
  s_gerenciar_celula.c03 = origem[2];
  s_gerenciar_celula.c04 = origem[3];
  s_gerenciar_celula.c05 = origem[4];
  s_gerenciar_celula.c06 = origem[5];
  s_gerenciar_celula.c07 = origem[6];
  s_gerenciar_celula.c08 = origem[7];
  s_gerenciar_celula.c09 = origem[8];
  s_gerenciar_celula.c10 = origem[9];
  s_gerenciar_celula.c11 = origem[10];
  s_gerenciar_celula.c12 = origem[11];
}

unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return 0;
  }
  time(&now);
  return now;
}

static inline void selectMux(uint8_t ch) {
  digitalWrite(S0, (ch & 0x01) ? HIGH : LOW);
  digitalWrite(S1, (ch & 0x02) ? HIGH : LOW);
  digitalWrite(S2, (ch & 0x04) ? HIGH : LOW);
  digitalWrite(S3, (ch & 0x08) ? HIGH : LOW);
}


extern double tensao[12];
extern double maior;
extern double menor;
extern double soma;
extern double media;
extern int pares_ativos;

static inline double lerCanalMv(uint8_t ch) {
  selectMux(ch);
  delay(2);
  return ads.computeVolts(ads.readADC_SingleEnded(0)) * 1000.0;
}

void lerSensores() {
  bool celulasAtivasAgora[12];
  copiarCelulasParaArray(celulasAtivasAgora);
  memcpy(celulasAtivasUltimaLeitura, celulasAtivasAgora, sizeof(celulasAtivasAgora));

  for (int i = 0; i < 12; i++) {
    if (celulasAtivasAgora[i]) {
      tensao[i] = lerCanalMv(i);
    } else {
      tensao[i] = 0.0;
    }
  }

  // Mantém o filtro de tensões negativas
  for (int i = 0; i < 12; i++) {
    if (tensao[i] < 0.0) {
      tensao[i] = 0.0;
    }
  }

  soma = 0.0;
  pares_ativos = 0;

  bool primeiraAtiva = true;

  for (int i = 0; i < 12; i++) {
    if (!celulasAtivasAgora[i]) {
      continue;
    }

    if (primeiraAtiva) {
      maior = tensao[i];
      menor = tensao[i];
      primeiraAtiva = false;
    } else {
      if (tensao[i] > maior) maior = tensao[i];
      if (tensao[i] < menor) menor = tensao[i];
    }

    soma += tensao[i];
    pares_ativos++;
  }

  if (pares_ativos > 0) {
    media = soma / pares_ativos;
  } else {
    media = 0.0;
    maior = 0.0;
    menor = 0.0;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(50);

  // Abre o espaço de memória chamado "wifi_config" (modo leitura/escrita)
  preferences.begin("wifi_config", false);

  // Puxa o que está salvo. Se não tiver nada salvo, usa o valor padrão.
  current_ssid = preferences.getString("ssid", "");
  current_pass = preferences.getString("pass", "");
  // Puxa o intervalo salvo. Se não existir, usa 1.0 como padrão
  intervaloDesejadoUsuario = preferences.getDouble("int_sheets", 1.0);

  pinMode(34, INPUT);
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);

  // The ADC input range (or gain) can be changed via the following
  // functions, but be careful never to exceed VDD +0.3V max, or to
  // exceed the upper and lower limits if you adjust the input range!
  // Setting these values incorrectly may destroy your ADC!
  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  //ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  //ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  //ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  //ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  //ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV

  if (!ads.begin()) {
    Serial.println("Falha ao iniciar o ADS1115.");
    while (1);
  }

  // Set logging function to serial print
  mg_log_set_fn([](char ch, void *) { Serial.print(ch); }, NULL);
  mg_log_set(MG_LL_DEBUG);

  WiFi.mode(WIFI_AP_STA);

  // Cria o ponto de acesso
  WiFi.softAP("BioMon", "BiomonUbiquos");

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  Serial.printf("Tentando conectar em: %s\n", current_ssid.c_str());
  WiFi.begin(current_ssid.c_str(), current_pass.c_str());

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado!");
    Serial.print("STA IP: ");
    Serial.println(WiFi.localIP());
    WiFi.setSleep(false);
    
    // Liga a reconexão automática APENAS se a rede foi encontrada com sucesso
    WiFi.setAutoReconnect(true); 
  } else {
    Serial.println("\nFalha ao conectar no Wi-Fi. Desligando modo Estação para estabilizar o AP BioMon.");
    
    // Desliga a busca por rede e fixa o ESP32 apenas como Ponto de Acesso
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP); 
  }

  // O bloco do ntpServer continua igual
  configTime(-3 * 3600, 0, ntpServer);
  Serial.print("Sincronizando hora com a internet");
  struct tm timeinfo;
  unsigned long t0 = millis();
  while (!getLocalTime(&timeinfo) && (millis() - t0 < 15000)) {
    Serial.print(".");
    delay(1000);
  }
  if (getLocalTime(&timeinfo)) {
    Serial.println("\nHora sincronizada com sucesso!");
  } else {
    Serial.println("\nNao foi possivel sincronizar a hora agora.");
  }
  

  GSheet.setTokenCallback(tokenStatusCallback);
  GSheet.setPrerefreshSeconds(10 * 60);
  GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

  mongoose_init();
  mongoose_set_http_handlers("resumo", my_get_resumo, NULL);
  mongoose_set_http_handlers("tensoes", my_get_tensoes, NULL);
  mongoose_set_http_handlers("texto", my_get_texto, NULL);
  mongoose_set_http_handlers("wifi", my_get_wifi, my_set_wifi);
  mongoose_set_http_handlers("config", my_get_config, my_set_config);
  mongoose_set_http_handlers("gerenciar_celula", my_get_gerenciar_celula, my_set_gerenciar_celula);
  mongoose_set_http_handlers("ip", my_get_ip, NULL);

  mongoose_add_ws_reporter(250, "resumo");
  mongoose_add_ws_reporter(250, "tensoes");
}

double tensao[12] = {0};
double maior = 0;
double menor = 0;
double soma = 0;
double media = 0;
int pares_ativos = 0;

void loop() {
  unsigned long now = millis();
  bool ready = GSheet.ready();

  // 1. MOTOR DA DASHBOARD (Visualização)
  // Só funciona se o checkbox estiver LIGADO (true)
  if (dashboardAtiva && (now - ultimoTempoSensor >= intervaloSensor)) {
    ultimoTempoSensor = now;
    lerSensores();
  }

  // 2. MOTOR DO GOOGLE SHEETS
  if (intervaloDesejadoUsuario < 1.0) {
    // --- MODO BATCHING (< 1 SEGUNDO) ---
    if (now - ultimoTempoLeituraBatch >= (unsigned long)(intervaloDesejadoUsuario * 1000.0)) {
      ultimoTempoLeituraBatch = now;

      // FORÇA A LEITURA: Se a tela estiver desligada, os dados estariam velhos.
      if (!dashboardAtiva) lerSensores();

      if (leiturasNoBuffer < TAMANHO_MAX_BUFFER) {
        epochTime = getTime();
        bufferLeituras[leiturasNoBuffer].sheetsDate = (epochTime / 86400.0) + 25569.0 - (3.0 / 24.0);
        for (int i = 0; i < 12; i++) {
          bufferLeituras[leiturasNoBuffer].t[i] = round(tensao[i] * 100.0) / 100.0;
          bufferLeituras[leiturasNoBuffer].ativa[i] = celulasAtivasUltimaLeitura[i];
        }
        leiturasNoBuffer++;
      }
    }

    int pacoteEsperado = (int)(1.0 / intervaloDesejadoUsuario);

    if (ready && (leiturasNoBuffer >= pacoteEsperado)) {
      ultimoTempoEnvioSheets = now;

      FirebaseJson response;
      FirebaseJson valueRange;
      valueRange.add("majorDimension", "ROWS");

      for (int row = 0; row < leiturasNoBuffer; row++) {
        String path = "values/[" + String(row) + "]/";
        valueRange.set(path + "[0]", bufferLeituras[row].sheetsDate);
        for (int col = 0; col < 12; col++) {
          if (bufferLeituras[row].ativa[col]) {
            valueRange.set(path + "[" + String(col + 1) + "]", bufferLeituras[row].t[col]);
          } else {
            valueRange.set(path + "[" + String(col + 1) + "]", "");
          }
        }
      }

      bool success = GSheet.values.append(&response, spreadsheetId, "Planilha!A1", &valueRange);
      if (!success) Serial.println(GSheet.errorReason());

      leiturasNoBuffer = 0;
      valueRange.clear();
      response.clear();
    }

  } else {
    // --- MODO DIRETO (>= 1 SEGUNDO) ---
    if (ready && (now - ultimoTempoEnvioSheets >= (unsigned long)(intervaloDesejadoUsuario * 1000.0))) {
      ultimoTempoEnvioSheets = now;

      // FORÇA A LEITURA: Garante o dado fresco na hora de enviar
      if (!dashboardAtiva) lerSensores();

      FirebaseJson response;
      FirebaseJson valueRange;
      epochTime = getTime();
      double sheetsDate = (epochTime / 86400.0) + 25569.0 - (3.0 / 24.0);

      valueRange.add("majorDimension", "ROWS");
      valueRange.set("values/[0]/[0]", sheetsDate);
      for (int col = 0; col < 12; col++) {
        if (celulasAtivasUltimaLeitura[col]) {
          valueRange.set("values/[0]/[" + String(col + 1) + "]", round(tensao[col] * 100.0) / 100.0);
        } else {
          valueRange.set("values/[0]/[" + String(col + 1) + "]", "");
        }
      }

      bool success = GSheet.values.append(&response, spreadsheetId, "Planilha!A1", &valueRange);
      if (!success) Serial.println(GSheet.errorReason());

      valueRange.clear();
      response.clear();
    }
  }

  mongoose_poll();
}

extern "C" int lwip_hook_ip6_input(struct pbuf *p, struct netif *inp) __attribute__((weak));
extern "C" int lwip_hook_ip6_input(struct pbuf *p, struct netif *inp) {
  if (ip6_addr_isany_val(inp->ip6_addr[0].u_addr.ip6)) {
    pbuf_free(p);
    return 1;
  }
  return 0;
}

void my_get_resumo(struct resumo *data) {
  data->media = media;
  data->maior = maior;
  data->menor = menor;
  data->ativos = pares_ativos;
}

void my_get_texto(struct texto *data) {
  if (WiFi.status() == WL_CONNECTED)
    strcpy(data->wifi, "Conectado");
  else
    strcpy(data->wifi, "Desconectado");
}

void my_get_config(struct config *data) {
  // Envia o valor atual do ESP32 para a barrinha do navegador
  data->salvamento_novo_sheet = intervaloDesejadoUsuario;
  data->salvamento_atual_sheet = intervaloDesejadoUsuario;
  data->dashboard_ativa = dashboardAtiva;
}

void my_set_config(struct config *data) {
  double novoValor = data->salvamento_novo_sheet;

  // Trava de segurança no código (mínimo 0.2s, máximo 3600)
  if (novoValor < 0.2) novoValor = 0.2;
  if (novoValor > 3600.0) novoValor = 3600.0;

  // Atualiza a variável que controla o envio
  intervaloDesejadoUsuario = novoValor;

  // Salva na memória flash para não perder quando faltar energia
  preferences.putDouble("int_sheets", novoValor);

  data->salvamento_atual_sheet = novoValor;

  Serial.printf("Novo intervalo do Sheets salvo: %.1f segundos\n", novoValor);

  // 2. TRATA O CHECKBOX DA DASHBOARD
  dashboardAtiva = data->dashboard_ativa;
  Serial.printf("MODO ECO: A Dashboard agora esta %s\n", dashboardAtiva ? "LIGADA" : "DESLIGADA");
}

void my_get_wifi(struct wifi *data) {
  // Preenche a interface com a rede ATUAL que o ESP tentou conectar
  strcpy(data->ssid_atual, current_ssid.c_str());
  strcpy(data->pass_atual, current_pass.c_str());

  // Limpa as caixas de novo input
  data->ssid_novo[0] = '\0';
  data->pass_novo[0] = '\0';
}

// --- O SET: Quando o usuário CLICA NO BOTÃO ---
void my_set_wifi(struct wifi *data) {
  bool precisaReiniciar = false;

  // Se o usuário digitou algo dashboardAtivano campo de novo SSID
  if (strlen(data->ssid_novo) > 0) {
    Serial.printf("Salvando novo SSID: %s\n", data->ssid_novo);
    preferences.putString("ssid", data->ssid_novo);
    precisaReiniciar = true;
  }

  // Se o usuário digitou algo no campo de nova senha
  if (strlen(data->pass_novo) > 0) {
    Serial.printf("Salvando nova senha: %s\n", data->pass_novo);
    preferences.putString("pass", data->pass_novo);
    precisaReiniciar = true;
  }

  // Se houve alguma mudança, o ideal é reiniciar o ESP32 para ele conectar na rede nova
  if (precisaReiniciar) {
    Serial.println("Configuracoes salvas. Reiniciando o ESP32 em 3 segundos...");
    delay(3000);
    ESP.restart(); // Reinicia o chip
  }
}

void my_get_ip(struct ip *data) {

  // IP do Access Point
  strcpy(data->esp32, WiFi.softAPIP().toString().c_str());

  // IP da rede WiFi (STA)
  if (WiFi.status() == WL_CONNECTED) {
    strcpy(data->local, WiFi.localIP().toString().c_str());
  } else {
    strcpy(data->local, "-");
  }
}

static struct tensoes s_tensoes = {12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
void my_get_tensoes(struct tensoes *data) {
  data->t01 = tensao[0];
  data->t02 = tensao[1];
  data->t03 = tensao[2];
  data->t04 = tensao[3];
  data->t05 = tensao[4];
  data->t06 = tensao[5];
  data->t07 = tensao[6];
  data->t08 = tensao[7];
  data->t09 = tensao[8];
  data->t10 = tensao[9];
  data->t11 = tensao[10];
  data->t12 = tensao[11];
}

void my_get_gerenciar_celula(struct gerenciar_celula *data) {
  *data = s_gerenciar_celula;
}

void my_set_gerenciar_celula(struct gerenciar_celula *data) {
  s_gerenciar_celula = *data;
}

// Callback para mostrar o status da geracao do Token do Google
void tokenStatusCallback(TokenInfo info) {
  if (info.status == token_status_error) {
    Serial.printf("Erro na geracao do Token: %s\n", GSheet.getTokenError(info).c_str());
  } else {
    Serial.printf("Status do Token: %s\n", GSheet.getTokenStatus(info).c_str());
  }
}
