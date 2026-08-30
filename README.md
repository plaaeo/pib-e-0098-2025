# pib-e-0098-2025

_Avaliação Experimental de Redes de Sensores sem Fio para IoT aplicada ao Monitoramento da Qualidade da Água dos Rios da Amazônia_

---

O repositório contém um projeto [Platform.IO](https://platformio.org/) utilizando o framework **Arduino** como componente do **ESP-IDF**, permitindo a compatibilidade com bibliotecas desenvolvidas para **Arduino**, porém garantindo a flexibilidade do **ESP-IDF** manualmente configurável.

## Platform.IO

O Platform.IO é uma ferramenta para desenvolvimento de aplicações para sistemas embarcados, suportando múltiplos frameworks, arquiteturas e plataformas.

Para compilar o projeto, é necessário que instale a extensão do [Platform.IO no VSCode](https://docs.platformio.org/en/latest/integration/ide/vscode.html#ide-vscode) ou a ferramenta CLI [PlatformIO Core](https://docs.platformio.org/en/latest/core/index.html), porém, recomendo a extensão do VSCode por facilitar o desenvolvimento contínuo.

Esta ferramenta é utilizada neste projeto como uma alternativa ao **Arduino IDE**. Com um arquivo de configuração explícito (`platformio.ini`), é possível fixar versões de bibliotecas e instalá-las automaticamente, sem ser necessário manualmente ajustar seu ambiente de desenvolvimento como no **Arduino IDE**.
