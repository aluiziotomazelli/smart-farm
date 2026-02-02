# Guia de Estudo: Simulação com Wokwi para ESP32

O Wokwi é um simulador de eletrônica online e integrado ao VS Code que permite rodar código ESP-IDF em um ambiente que emula o hardware real. Ele é ideal para testar a integração entre software e periféricos sem hardware físico.

## 1. Por que usar o Wokwi?

*   **Feedback Instantâneo:** Não há tempo de flash. O simulador carrega o binário quase instantaneamente.
*   **Debug Avançado:** Suporte nativo a GDB (via VS Code) e Analisador de Lógica (Logic Analyzer).
*   **Periféricos Prontos:** Sensores como HC-SR04, botões, LEDs, displays I2C/SPI e até WiFi já estão implementados.
*   **Zero Risco:** Teste curto-circuitos ou pinagens erradas sem queimar componentes.

## 2. Configuração no VS Code

Para usar de forma interativa no seu ambiente local:

1.  Instale a extensão **Wokwi Simulator** no VS Code.
2.  Obtenha uma **Wokwi License Key** (gratuita para uso pessoal/estudante no site do Wokwi).
3.  Pressione `F1` e selecione `Wokwi: Request License Key`.

## 3. Arquivos de Configuração

### `wokwi.toml`
Este arquivo diz ao simulador onde encontrar o binário compilação (`.elf` ou `.bin`).
```toml
[wokwi]
version = 1
firmware = 'build/unit-test-app.elf'
elf = 'build/unit-test-app.elf'
```

### `diagram.json`
Define as conexões físicas. Exemplo para o sensor ultrassônico:
```json
{
  "version": 1,
  "author": "Jules",
  "editor": "wokwi",
  "parts": [
    { "type": "board-esp32-devkit-v1", "id": "esp", "top": 0, "left": 0 },
    { "type": "wokwi-hc-sr04", "id": "sensor", "top": -100, "left": 50 }
  ],
  "connections": [
    [ "esp:GND", "sensor:GND", "black", [ "v0" ] ],
    [ "esp:5V", "sensor:VCC", "red", [ "v0" ] ],
    [ "esp:D5", "sensor:TRIG", "blue", [ "v0" ] ],
    [ "esp:D18", "sensor:ECHO", "green", [ "v0" ] ]
  ]
}
```

## 4. Simulando seus Componentes

### Sensor Ultrassônico (`sensor_ultrasonic`)
O Wokwi possui o componente `wokwi-hc-sr04`.
*   No simulador, você pode clicar no sensor enquanto a simulação roda para **ajustar a distância manualmente** com um slider e ver como o seu código reage em tempo real.

### Sensor de Nível/Boia (`sensor_floatswitch`)
Como a boia é essencialmente um switch (contato seco), usamos o `wokwi-pushbutton` ou `wokwi-slide-switch`.
*   Conecte um pino ao GPIO do ESP32 e o outro ao GND (usando o pull-up interno do ESP32 configurado no seu código).

### WiFi Manager
O Wokwi simula um Access Point chamado **Wokwi-GUEST** (sem senha).
*   Para testar seu `wifi_manager`, você deve configurar as credenciais para SSID: `Wokwi-GUEST` e Password: `` (vazio).
*   O simulador provê DHCP e internet real para o ESP32.

## 5. Ferramentas de Análise

### Analisador de Lógica
Útil para depurar o protocolo do sensor ultrassônico ou I2C.
1.  Adicione o componente `wokwi-logic-analyzer` ao `diagram.json`.
2.  Conecte os pinos que deseja monitorar.
3.  Após parar a simulação, o Wokwi gera um arquivo `.vcd` que pode ser aberto no **PulseView** ou **GTKWave**.

### Debug com GDB
1.  Compile seu projeto com flags de debug (`-g`).
2.  No VS Code, inicie a simulação do Wokwi.
3.  Pressione `F5` (ou use a aba de Debug) para conectar o GDB ao simulador. Você poderá colocar breakpoints e inspecionar variáveis exatamente como no hardware real.

## 6. Próximos Passos (Automação)
Para o futuro (CI/CD), existe o `wokwi-cli`. Ele permite rodar os testes e esperar por uma string específica no Serial (ex: `Unity Test Passed`) para definir se o teste passou no pipeline do GitHub Actions.
