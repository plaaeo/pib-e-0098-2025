# iots-base
Uma codebase desenvolvida para realizar atividades envolvendo microcontroladores no laboratório IoTS.

O repositório contém um projeto [Platform.IO](https://platformio.org/) utilizando o framework **Arduino** como componente do **ESP-IDF**, permitindo a compatibilidade com bibliotecas desenvolvidas para **Arduino**, porém garantindo a flexibilidade do **ESP-IDF**.

## Branch `integracao-icomp`

A branch `integracao-icomp` foi criada para implementar dois firmwares simples de transmissão/recepção entre dois nós LoRa para a **Semana de Integração do IComp** de 2026. O arquivo `src/port/arduino/main.cc` contém o código principal de um nó sensor implementado num **Arduino Nano V3** com os 3 sensores (pH, TDS e temperatura) funcionais, coletando de 10 em 10 segundos e transmitindo as coletas via LoRa. O arquivo `src/port/esp32/main.cc` contém o código principal de um gateway implementado num **Heltec WiFi Lora 32 V2** que recebe coletas via LoRa e as envia para uma base no Firestore.

Esta branch foi criada pensando nos seguintes problemas:
- A implementação do firmware da rede de sensores ainda não estava completa para ser demonstrada no evento;
- O MCU **ATmega328P** presente no **Arduino Nano V3** não possui um flash grande o suficiente para armazenar a biblioteca RadioLib, utilizada por padrão em todo o projeto;
- A implementação concreta para o gateway ainda não havia sido desenvolvida (além da transmissão para o Firebase).
Pensando na data do evento, determinei que seria mais importante fazer uma implementação rápida e descartável, porém cujas partes poderiam ser reutilizadas depois. Logo, pode-se considerar que esta branch estará **inativa** após a data do evento (12 de março de 2026).

## Platform.IO

O Platform.IO é uma ferramenta para desenvolvimento de aplicações para sistemas embarcados, suportando múltiplos frameworks, arquiteturas e plataformas.

Para compilar o projeto, é necessário que instale a extensão do [Platform.IO no VSCode](https://docs.platformio.org/en/latest/integration/ide/vscode.html#ide-vscode) ou a ferramenta CLI [PlatformIO Core](https://docs.platformio.org/en/latest/core/index.html), porém, recomendo a extensão do VSCode por facilitar o desenvolvimento contínuo.

Esta ferramenta é utilizada neste projeto como uma alternativa ao **Arduino IDE**. Com um arquivo de configuração explícito (`platformio.ini`), é possível fixar versões de bibliotecas e instalá-las automaticamente, sem ser necessário manualmente ajustar seu ambiente de desenvolvimento como no **Arduino IDE**.

## Segredos
Para utilizar quaisquer recursos que necessitem de chaves de autenticação ou senhas, crie um arquivo no diretório `include/` com o nome de `secrets.hh` seguindo o seguinte formato

```c
#define WIFI_SSID "***"                 // (Wifi) Nome do ponto de acesso
#define WIFI_PASSWORD "***"             // (Wifi) Senha do ponto de acesso
#define FIREBASE_PROJECT_ID "***"       // (Firebase) ID do projeto (chave "project_id" no JSON)
#define FIREBASE_PRIVATE_KEY_ID "***"   // (Firebase) ID da chave privada (chave "private_key_id" no JSON)
#define FIREBASE_PRIVATE_KEY "***"      // (Firebase) Dados da chave privada (chave "private_key" no JSON)
#define FIREBASE_CLIENT_EMAIL "***"     // (Firebase) E-mail da service account (chave "client_email" no JSON)
#define FIREBASE_CLIENT_ID "***"        // (Firebase) ID do cliente (chave "client_id" no JSON)
```

Normalmente, para realizar a autenticação no Firebase, é necessário um **arquivo JSON** de autenticação do tipo `'service_account'`. Preencha os dados obtidos deste arquivo JSON diretamente no arquivo `secrets.hh`