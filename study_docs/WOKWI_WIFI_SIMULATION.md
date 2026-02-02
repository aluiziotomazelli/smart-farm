# Testando o wifi_manager no Wokwi

O Wokwi suporta simulação de WiFi para o ESP32, o que permite testar a lógica do `wifi_manager` sem hardware e com uma conexão de rede real.

## 1. Configuração da Rede Virtual

O Wokwi fornece um Access Point virtual com as seguintes credenciais:

*   **SSID:** `Wokwi-GUEST`
*   **Password:** (vazio)
*   **Segurança:** Aberta (Open)

## 2. Como testar seu componente

Para validar o `wifi_manager` no simulador:

1.  **Credenciais:** Configure seu código ou o Kconfig para usar o SSID `Wokwi-GUEST`.
2.  **Execução:** Quando o ESP32 chamar `esp_wifi_connect()`, o Wokwi irá interceptar e "conectar" à rede virtual.
3.  **DHCP:** O simulador possui um servidor DHCP que atribuirá um IP (geralmente na faixa `10.0.x.x`) ao ESP32.
4.  **Internet:** O ESP32 terá acesso real à internet através do seu computador (útil para testar o `ota_manager` ou requisições HTTP).

## 3. Limitações Conhecidas

*   **Escaneamento:** `esp_wifi_scan_start()` retornará apenas a rede `Wokwi-GUEST`.
*   **Modo AP:** O Wokwi simula principalmente o modo Estação (STA). O modo SoftAP tem suporte limitado.
*   **Protocolos:** Suporta TCP/UDP de forma robusta.

## 4. Analisador de Rede (Wireshark)

Uma das maiores vantagens é que o Wokwi pode gerar um arquivo `.pcap` com todo o tráfego de rede do simulador.

Para ativar:
1.  Adicione no seu `wokwi.toml`:
    ```toml
    [wokwi]
    network_dump = "network.pcap"
    ```
2.  Após rodar o teste, abra o arquivo `network.pcap` no **Wireshark** para ver exatamente o que o `wifi_manager` está enviando/recebendo.
