

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