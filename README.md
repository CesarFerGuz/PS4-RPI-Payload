# PS4 Remote Package Installer

Payload para la consola PS4 que permite instalar juegos y aplicaciones (archivos `.pkg`) de forma remota a través de la red local, eliminando la necesidad de pasar los archivos a una memoria USB.

## ✨ ¿Qué puede hacer este Payload?
* **Instalación de PKGs por Red:** Recibe un enlace web (URL) e inicia la descarga directamente en el sistema de la consola.
* **Inicio Instantáneo:** Los juegos aparecen en la pantalla de inicio al instante, listos para descargar, sin quedarse congelados en "Preparando 0%".
* **Inteligencia Automática:** No necesitas decirle al payload qué juego estás instalando. El payload lee el archivo de la nube por sí solo y averigua el título, el peso y el ícono original.
* **Gestión Completa de Descargas:** Permite pausar, reanudar o cancelar cualquier instalación en curso remotamente.
* **Limpieza de Almacenamiento:** Si cancelas un juego, el payload se encarga de borrar de forma segura los archivos temporales para que no pierdas valiosos GBs en tu disco duro ("espacio fantasma").
* **Alertas en Pantalla:** Muestra notificaciones nativas en tu TV para avisarte si la descarga inició, si te quedaste sin espacio, o si el juego ya estaba instalado.

---

## 🚀 Instalación y Uso
1. Descarga el archivo `rpi_payload.bin` desde la pestaña de **[Releases](https://github.com/CesarFerGuz/PS4-plugin-remote-compiler/releases)**.
2. Asegúrate de tener tu PS4 con Jailbreak (ej. GoldHEN) y el **BinLoader** activado (puerto `9090`).
3. Inyecta el payload `rpi_payload.bin` a tu consola usando tu método favorito (Inyector Web, Netcat, o tu propio ESP32).
4. ¡Listo! El payload quedará ejecutándose silenciosamente en segundo plano en el puerto `12800` a la espera de instrucciones.

---

## 🔌 API para Desarrolladores (Puerto 12800)

Si estás creando una app de celular, un cliente de PC o usando un puente (como un ESP32), puedes controlar el payload enviando paquetes JSON al puerto `12800` de la consola.

*(El payload lee las peticiones mediante palabras clave en el cuerpo TCP).*

### 1. Iniciar Instalación
Solo envía la URL del PKG. El payload se encargará de extraer el resto.

**Petición:**
```bash
curl -X POST http://<IP_PS4>:12800/api/install \
  -H "Content-Type: application/json" \
  -d '{"url": "http://192.168.1.100/juego.pkg"}'
```

**Respuestas:**
* **Éxito:** `{"status":"success","task_id":215,"title":"Nombre del Juego"}`
* **Fallo:** `{"status":"fail","error":-2137456619,"message":"Error al registrar"}` *(El campo `error` contiene el código nativo de PS4)*.

---

### 2. Controles de Descarga
Envía el `task_id` obtenido en el paso anterior junto con el comando deseado en formato JSON.

* **Pausar:** `{"pause_task": 215}`
* **Reanudar:** `{"resume_task": 215}`
* **Detener (Solo red):** `{"stop_task": 215}`
* **Desenlistar y Borrar del Disco:** `{"unregister_task": 215}`

> ⚠️ **Nota para cancelar un juego:** Para abortar y recuperar el almacenamiento, debes enviar primero `stop_task`, esperar al menos 1.5 segundos a que la consola cierre los archivos, y finalmente enviar `unregister_task`.

---

### 3. Progreso en Tiempo Real
Obtén métricas en tiempo real de una instalación activa.

**Petición:**
```bash
curl -X POST http://<IP_PS4>:12800/api/get_task_progress \
  -d '{"task_id": 215}'
```

**Respuesta:**
```json
{
  "status": "success",
  "length": 1234567890,
  "transferred": 500000000,
  "rest_sec": 120,
  "preparing_percent": 100,
  "error": 0
}
```

---

## 🏗️ Compilación Automática
El proyecto está automatizado con GitHub Actions. Cada vez que se actualiza el código, el payload `rpi_payload.bin` se compila con el PS4-Payload-SDK y queda disponible en la pestaña de artefactos para ser inyectado en la consola.
