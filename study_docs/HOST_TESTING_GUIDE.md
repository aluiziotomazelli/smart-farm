# Guia de Estudo: Testes Unitários no Host (Linux) com ESP-IDF

Este guia resume o estudo sobre como utilizar o sistema de mocks e testes em host do ESP-IDF, focando nos componentes `gpio_validator` e `wifi_manager`.

## 1. Arquitetura de Testes em Host

O ESP-IDF permite executar aplicações (incluindo testes) diretamente no Linux/macOS. Existem duas abordagens principais que podem ser combinadas:

1.  **Linux Target (Simulação POSIX):** O IDF compila o código para a arquitetura do seu PC (`x86_64` ou `arm64`) em vez do chip ESP32. Alguns componentes (como `esp_hw_support`, `esp_event`, `nvs_flash`, `freertos`) possuem uma implementação específica para Linux que simula o comportamento real do hardware ou do RTOS usando APIs do sistema operacional (pthreads, sockets, etc).
2.  **CMock (Mocking):** Para componentes que não possuem uma simulação para Linux ou que você deseja isolar completamente, o IDF integra o framework **CMock**. Ele gera automaticamente implementações "falsas" (mocks) de headers C, permitindo que você defina expectativas (ex: `expect_esp_wifi_connect()`) e valores de retorno nos seus testes.

## 2. Requisitos de Sistema

Para rodar testes de host com mocks, seu sistema Linux precisa de:

*   **ESP-IDF configurado:** Com a variável `IDF_PATH` exportada.
*   **Ruby:** Necessário para o CMock processar os headers e gerar os mocks.
*   **libbsd-dev:** Necessário para algumas funções de compatibilidade BSD usadas no simulador Linux do IDF.
*   **GCC/G++ e CMake:** Ferramentas padrão de compilação.

Instalação no Ubuntu/Debian:
```bash
sudo apt-get install ruby libbsd-dev
```

## 3. Diferenças entre Versões (v5.1.1 vs v5.5.2)

Durante o estudo, notamos que a infraestrutura de mocks evoluiu significativamente:

| Recurso | ESP-IDF v5.1.1 (Atual) | ESP-IDF v5.5.2 (Alvo) |
| :--- | :--- | :--- |
| **Local dos Mocks** | `tools/mocks` | `tools/mocks` |
| **Mock de `esp_wifi`** | **Não disponível** nativamente. | **Disponível** em `tools/mocks/esp_wifi`. |
| **Mocks de Driver** | GPIO, SPI, I2C disponíveis. | GPIO, SPI, I2C, RMT, etc. |
| **Suporte Linux** | Experimental. | Mais estável e com mais componentes portados. |

**Implicação para o `wifi_manager`:** Na v5.1.1, para testar no host, precisaríamos criar manualmente uma pasta de mock para o `esp_wifi` ou copiar a da v5.5.2. Na v5.5.2, basta referenciar o mock oficial.

## 4. Estudo de Caso: `gpio_validator`

O `gpio_validator` é um excelente candidato para testes em host porque sua lógica é puramente de decisão baseada no modelo do chip e nas capacidades do GPIO.

*   **Como testar:** No host, o target `linux` simula o `esp_chip_info()`. Podemos usar mocks para forçar o retorno de diferentes modelos de chip (ESP32, S3, C3) e validar se o `gpio_validator` retorna `ESP_ERR_INVALID_ARG` para os pinos proibidos de cada um.
*   **Vantagem:** O teste roda em milissegundos e não exige hardware físico para validar se a tabela de pinos está correta.

## 5. Estudo de Caso: `wifi_manager`

O `wifi_manager` é mais complexo pois depende de:
*   `freertos` (Tasks, Queues, Event Groups, Mutexes)
*   `esp_wifi` (API de controle do rádio)
*   `esp_event` (Sistema de eventos)
*   `nvs_flash` (Persistência de credenciais)

### Estratégia de Mocking para `wifi_manager`:
1.  **FreeRTOS:** Usar o mock/simulador de FreeRTOS do IDF. No host, ele permite criar tasks, mas o escalonamento é cooperativo ou simulado via pthreads.
2.  **esp_wifi:** Como não existe na v5.1.1, criaríamos um mock simples que apenas registra se `esp_wifi_connect()` foi chamado.
3.  **Lógica da FSM:** O maior valor do teste de host aqui é validar a **Máquina de Estados**. Podemos simular eventos (ex: `WIFI_EVENT_STA_DISCONNECTED`) chamando os handlers e verificar se o `wifi_manager` entra em `WAITING_RECONNECT` com o backoff correto.

## 6. Como configurar um teste de host no seu componente

A estrutura recomendada é criar uma pasta `host_test` dentro do componente:

```text
meu_componente/
├── host_test/
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   └── test_meu_componente.cpp
│   └── CMakeLists.txt (Projeto de teste)
├── meu_componente.cpp
└── include/meu_componente.hpp
```

No `CMakeLists.txt` do projeto de teste, você define o target como Linux:
```cmake
set(COMPONENTS main meu_componente)
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/freertos")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
```

## 7. Conclusão do Estudo

*   **É possível?** Sim, e é a recomendação atual da Espressif para garantir qualidade sem depender de hardware.
*   **Recomendação:** Migrar para a v5.5.2 facilitará muito o trabalho com o `wifi_manager` devido ao mock nativo de WiFi.
*   **Mocking em C++:** Como o CMock foca em funções C, a melhor estratégia para testar seus componentes C++ (como o `WiFiManager`) é mockar as funções da API do ESP-IDF que eles consomem. Assim, você testa se sua classe C++ interage corretamente com o sistema operacional.
*   **Próximo Passo:** Veja o protótipo criado na pasta `components/gpio_validator/host_test` para um exemplo prático de implementação.
