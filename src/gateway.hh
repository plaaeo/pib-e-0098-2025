#if defined(ESP32) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
#    include <WiFi.h>
#elif defined(ESP8266)
#    include <ESP8266WiFi.h>
#endif

#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>

#include "sensors.hh"

//< Possui os '#define's com chave de API do Firebase, URL, login do Wifi, etc.
#include "secret.hh"

/**
 * @brief Código adaptado de `gateway.ino` para interagir com o
 * Firebase/Firestore. A autenticação de usuário (email/senha) foi substituída
 * por um método menos invasivo, a autenticação por conta de serviço, cujos
 * detalhes devem ser preenchidos no arquivo 'secret.hh'
 */
namespace gw {
constexpr auto TAG = "gw";

// ID do ponto de coleta atual no Firestore
constexpr auto PONTO_ID = "ponto_suframa";
constexpr auto MUNICIPIO = "Manaus";
constexpr auto LOCAL_COLETA = "Tanque";
constexpr auto TIPO_AGUA = "Água de tanque";

// Set up NTP server
constexpr auto NTP_SERVER = "pool.ntp.org";
constexpr long GMT_OFFSET_SEC = -10800;  // Adjust if needed
constexpr int  DAYLIGHT_OFFSET_SEC = 3600;

// Firebase and authentication
FirebaseConfig config;
FirebaseAuth   auth;
FirebaseData   fbData;

//< Retorna uma string identificando a data e hora atuais.
String getCurrentTimestamp()
{
    time_t t;
    time(&t);

    // Obter tempo em formato local
    struct tm timeinfo;
    localtime_r(&t, &timeinfo);

    char buffer[100];
    auto len =
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);

    // Mover os dígitos do fuso horário (https://stackoverflow.com/a/48772690)
    if (len > 1) {
        char minute[] = { buffer[len - 2], buffer[len - 1], '\0' };
        sprintf(buffer + len - 2, ":%s", minute);
    }

    return String(buffer);
}

//< Realiza o setup das bibliotecas necessárias para enviar os dados para o
// Firebase.
void setup()
{
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    PORT_LOGI(TAG, "Connecting to Wi-Fi...");
    while (WiFi.status() != WL_CONNECTED)
        delay(300);

    // Log IP address
    PORT_LOGI(TAG, "Connected as %s", WiFi.localIP().toString().c_str());

    // Initialize NTP
    PORT_LOGI(TAG, "Initializing NTP...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    PORT_LOGI(TAG, "NTP configured at %s", getCurrentTimestamp().c_str());

    PORT_LOGI(TAG, "Firebase Client v" FIREBASE_CLIENT_VERSION);

    /* Configure service account credentials (from 'secret.hh') */
    config.service_account.data.project_id = FIREBASE_PROJECT_ID;
    config.service_account.data.private_key_id = FIREBASE_PRIVATE_KEY_ID;
    config.service_account.data.private_key = FIREBASE_PRIVATE_KEY;
    config.service_account.data.client_email = FIREBASE_CLIENT_EMAIL;
    config.service_account.data.client_id = FIREBASE_CLIENT_ID;

    /* Assign the callback function for the long running token generation task
     */
    config.token_status_callback =
        tokenStatusCallback;  // see addons/TokenHelper.h

    // Since Firebase v4.4.x, BearSSL engine was used, the SSL buffer need to be
    // set. Large data transmission may require larger RX buffer, otherwise
    // connection issue or data read time out can be occurred.
    fbData.setBSSLBufferSize(
        2048 /* Rx buffer size in bytes from 512 - 16384 */,
        1024 /* Tx buffer size in bytes from 512 - 16384 */);
    Firebase.begin(&config, &auth);

    // Comment or pass false value when WiFi reconnection will control by your
    // code or third party library e.g. WiFiManager
    Firebase.reconnectNetwork(true);
    PORT_LOGI(TAG, "Firebase initialized.");
}

//< Envia uma unica `sens::reading_t` para o Firestore.
void send_reading_firestore(const sens::Reading &reading)
{
    auto timestamp = getCurrentTimestamp();

    // Aguardar o Firebase estar pronto (?)
    PORT_LOGI(TAG, "Aguardando o Firebase...");
    while (!Firebase.ready()) {
    };
    PORT_LOGI(TAG, "Enviando ao Firebase...");

    // Preencher documento com valores estáticos
    FirebaseJson json;
    json.set("fields/pontoId/stringValue", PONTO_ID);
    json.set("fields/timestamp/timestampValue", timestamp);
    json.set("fields/municipio/stringValue", MUNICIPIO);
    json.set("fields/local_coleta/stringValue", LOCAL_COLETA);
    json.set("fields/tipo_agua/stringValue", TIPO_AGUA);

    // Criar lista temporária com os tipos de coleta para enviar
    struct
    {
        const char *id;
        float       valor;
    } coletas[] = {
        { "temperatura", reading.temperature },
        { "ph", reading.ph },
        { "tds", reading.tds },
    };

    for (auto &coleta : coletas) {
        json.set("fields/sensorId/stringValue", coleta.id);
        json.set("fields/valor/doubleValue", coleta.valor);

        // Criar documento no Firestore
        if (Firebase.Firestore.createDocument(&fbData, FIREBASE_PROJECT_ID, "",
                                              "leiturasSensores", json.raw())) {
            PORT_LOGI(TAG, "Coleta de '%s' enviada para o Firestore",
                      coleta.id);
        } else {
            PORT_LOGE(TAG, "Falha ao enviar '%s' ao Firestore: %s", coleta.id,
                      fbData.errorReason().c_str());
        }
    }
}
}  // namespace gw