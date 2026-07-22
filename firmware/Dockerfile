# Спільний образ для збірки PlatformIO-прошивок (ESP32/Heltec).
# Заливка на плату (upload) все одно робиться з хоста — Docker на macOS
# не прокидає USB-порти в контейнер.
FROM python:3.11-slim

RUN pip install --no-cache-dir platformio

WORKDIR /project

ENTRYPOINT ["pio"]
CMD ["run"]
