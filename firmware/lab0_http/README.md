# Lab 0 - Minimal IoT Implementation (HTTP) con Zephyr RTOS

Este proyecto implementa un nodo IoT básico utilizando **Zephyr RTOS (v4.4)** sobre una placa **ESP32 DevKit C**. El dispositivo actúa como un servidor HTTP nativo que expone capacidades de sensado (Sensing) y actuación (Actuating) mediante una API REST y formato JSON.

## Capacidades Implementadas
1. **Sensing (GET `/api/sensor`):** Retorna una simulación de lectura de temperatura.
   - Respuesta: `{"temperature": 24.5}`
2. **Actuating (POST `/api/control`):** Recibe un comando para controlar el estado de un actuador (LED).
   - Payload esperado: `{"state": 1}` o `{"state": 0}`
   - Respuesta: `{"status": "ok"}`

## Proceso de Desarrollo
El desarrollo se basó en el API de red nativa de Zephyr. Los pasos principales fueron:
1. **Configuración de Red (Capa 2 y 3):** Se configuró el ESP32 en modo *Station* (Cliente DHCP) modificando los archivos Kconfig (`prj.conf`) para provisionar búferes de memoria adecuados para paquetes web.
2. **Máquina de Estados de Conexión:** Se implementaron Callbacks (`net_mgmt_event_callback`) para escuchar los eventos de la capa Wi-Fi (`NET_EVENT_WIFI_CONNECT_RESULT`) y de capa IP (`NET_EVENT_IPV4_ADDR_ADD`), gestionando la sincronización de hilos mediante un semáforo (`k_sem_take`).
3. **Servidor HTTP:** Se levantaron recursos dinámicos (`HTTP_RESOURCE_TYPE_DYNAMIC`) procesando las tramas HTTP por *chunks* (pedazos) y utilizando la librería `json_obj_parse` de Zephyr.

## Problemas Encontrados y Soluciones

Durante el desarrollo nos topamos con desafíos interesantes propios de los sistemas embebidos y de la actualización a Zephyr 4.4:

### 1. Bloqueo (Deadlock) en el Semáforo de IP
- **Problema:** El hilo principal se quedaba congelado esperando la IP (`k_sem_take(&ipv4_ready, K_FOREVER)`), a pesar de que el Wi-Fi se conectaba exitosamente.
- **Causa:** Había un conflicto lógico. El `prj.conf` forzaba una IP estática, pero el evento en C (`ipv4_event_handler`) bloqueaba el semáforo si la IP no venía explícitamente etiquetada como dinámica (`NET_ADDR_DHCP`).
- **Solución:** Se purgaron las configuraciones de IP estática del `prj.conf` y se configuró correctamente como Cliente DHCP (`CONFIG_NET_DHCPV4=y`), permitiendo que el router asignara la IP y liberara el semáforo de manera asíncrona.

### 2. Bucle infinito en respuestas HTTP (JSON infinito)
- **Problema:** Al hacer un GET a `/api/sensor`, el ESP32 respondía el JSON infinitas veces y nunca cerraba la conexión.
- **Causa:** En Zephyr, las peticiones HTTP dinámicas vuelven a invocar el manejador (`sensor_handler`) repetidamente consultando si hay más datos. Al asignarle la longitud de 21 bytes en cada iteración, Zephyr asumía que siempre había datos nuevos.
- **Solución:** Se implementó una bandera estática (`static bool sent`). En la primera llamada se envían los datos y se activa la bandera. En las llamadas subsecuentes se devuelve `response_ctx->body_len = 0`, lo que notifica a Zephyr el fin de la transmisión y cierra la conexión elegantemente.

### 3. Aislamiento de Redes Móviles (AP Isolation) y Timeouts
- **Problema:** Errores de "Connection Refused" rápidos al usar `curl` desde el PC hacia el ESP32 cuando el host era un celular Android. Además, desconexiones tras tiempos cortos de inactividad.
- **Causa:** Android implementa *AP Isolation* en su Hotspot bloqueando el tráfico local por seguridad. Sumado a esto, los gestores de red desconectan nodos IoT inactivos (Modem Sleep) rápidamente.
- **Solución:** Se utilizó un Hotspot desplegado desde un sistema Linux (PC), permitiendo el tráfico directo sin aislamiento. Adicionalmente, las pruebas se coordinaron bajo los tiempos de ventana activa de la sesión Wi-Fi para evitar timeouts.

## Compilación y Ejecución
```bash
# Compilar
west build -p auto -b esp32_devkitc/esp32/procpu .

# Flashear y Monitorear
west flash && west espressif monitor