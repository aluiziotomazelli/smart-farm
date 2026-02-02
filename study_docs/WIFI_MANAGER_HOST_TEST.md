# Estratégia de Teste de Host: wifi_manager

O `wifi_manager` é um componente complexo com dependências profundas do ESP-IDF. Para testá-lo no host (Linux), especialmente na versão v5.1.1 onde o mock de WiFi oficial não existe, seguimos a seguinte estratégia.

## 1. Dependências e Mocks Necessários

| Dependência | Solução no Host |
| :--- | :--- |
| **FreeRTOS** | Usar `tools/mocks/freertos` do IDF. Ele provê implementações de Queue, Event Groups e Mutex que funcionam no host. |
| **esp_wifi** | **Mock Customizado.** Criar um mock simplificado das funções usadas (`esp_wifi_init`, `esp_wifi_start`, `esp_wifi_connect`, etc). |
| **esp_event** | Usar a implementação real do `esp_event` para Linux (disponível no IDF). |
| **nvs_flash** | Usar a implementação real do `nvs_flash` para Linux (ele cria um arquivo binário no seu PC para simular a flash). |

## 2. Criando um Mock Customizado para `esp_wifi` (v5.1.1)

Como o mock oficial não existe na v5.1.1, você pode criar um na sua própria pasta de testes:

**Pasta:** `components/wifi_manager/host_test/mocks/esp_wifi/`

### CMakeLists.txt do Mock:
```cmake
idf_component_mock(INCLUDE_DIRS "$ENV{IDF_PATH}/components/esp_wifi/include"
                   REQUIRES esp_event
                   MOCK_HEADER_FILES "$ENV{IDF_PATH}/components/esp_wifi/include/esp_wifi.h")
```

### mock_config.yaml:
```yaml
:cmock:
  :plugins:
    - :expect
    - :expect_any_args
    - :return_thru_ptr
```

## 3. Exemplo de Teste da Máquina de Estados

No teste de host, podemos validar se a FSM (Finite State Machine) reage corretamente a eventos do driver, sem precisar de rádio.

```cpp
TEST_CASE("WiFi Manager: Transição de Reconexão", "[wm][host]")
{
    WiFiManager& wm = WiFiManager::get_instance();
    wm.init();

    // Configura expectativa de chamada do driver
    esp_wifi_connect_ExpectAndReturn(ESP_OK);

    // Simula uma desconexão por Beacon Timeout
    // No host, chamamos o handler diretamente ou via esp_event_post
    wifi_event_sta_disconnected_t disconn = { .reason = WIFI_REASON_BEACON_TIMEOUT };
    esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconn, sizeof(disconn), 0);

    // Verifica se o estado mudou para WAITING_RECONNECT
    // (Pode precisar de um pequeno delay para a task processar)
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(WiFiManager::State::WAITING_RECONNECT, wm.get_state());
}
```

## 4. Desafios e Soluções

1.  **Threading:** O `wifi_manager` lança uma task. No simulador Linux do FreeRTOS, as tasks são pthreads. Você deve garantir que seus asserts esperem o processamento da task ou usem mecanismos de sincronização.
2.  **Singletons:** Como o `WiFiManager` é um singleton, é vital chamar `deinit()` entre os testes para resetar o estado global, ou adicionar um método `test_reset()` para uso exclusivo em testes unitários.
3.  **Versão 5.5.2:** Ao migrar para a 5.5.2, todo o trabalho de criar o mock de `esp_wifi` desaparece, pois ele já vem pronto em `tools/mocks/esp_wifi`.

## 5. Conclusão da Análise

Testar o `wifi_manager` no host é extremamente valioso para validar a **lógica de retry, backoff e persistência de credenciais**, que são as partes mais propensas a bugs de lógica. O esforço inicial de configurar os mocks se paga rapidamente pela velocidade de execução dos testes.
