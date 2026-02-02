# Simulação Wokwi: sensor_ultrasonic

Este diretório contém os arquivos necessários para simular o componente `sensor_ultrasonic` usando o Wokwi.

## Arquivos
*   `diagram.json`: Define a conexão entre o ESP32 e o sensor HC-SR04.
*   `wokwi.toml`: Configura o simulador para carregar o binário dos testes unitários.

## Pinagem Utilizada
| Sensor HC-SR04 | ESP32 DevKit V1 |
| :--- | :--- |
| VCC | 5V |
| TRIG | D5 (GPIO 5) |
| ECHO | D18 (GPIO 18) |
| GND | GND |

## Como rodar
1.  Compile os testes unitários na raiz do projeto:
    ```bash
    idf.py -T sensor_ultrasonic build
    ```
2.  Abra o arquivo `diagram.json` no VS Code.
3.  Pressione `F1` e selecione **Wokwi: Start Simulation**.
4.  No menu do Unit Test (via Serial), selecione os testes de `[ultrasonic]`.
5.  **Interação:** Clique no sensor HC-SR04 no simulador para ajustar a distância e validar a leitura no log do Serial.
