# Sensores com ESP32

Projeto PlatformIO para ESP32 com DHT11, SW-420 e BME280/BMP280. As leituras
aparecem no Monitor Serial a 115200 baud; quando o Wi-Fi estiver configurado,
tambem ficam disponiveis no painel web.

## Configurar Wi-Fi

Copie `src/wifi_secrets.h.example` para `src/wifi_secrets.h` e substitua os
valores de exemplo pelas credenciais da sua rede. O arquivo com as credenciais
reais e ignorado pelo Git e nao deve ser publicado.

Sem esse arquivo, o firmware continua lendo sensores e exibindo os dados no
Monitor Serial, mas nao conecta ao Wi-Fi nem inicia o painel web.

## Compilar e enviar

Com o PlatformIO instalado e o ESP32 conectado:

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

Confira as ligacoes dos pinos no inicio de `src/main.cpp` antes de conectar os
sensores.
