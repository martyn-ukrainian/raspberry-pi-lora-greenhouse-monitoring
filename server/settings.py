"""
Центральна конфігурація серверу agro.

Значення завантажуються з файлу `.env` (через pydantic-settings)
і можуть бути перевизначені реальними змінними середовища (`os.environ`).
Пріоритет: змінні середовища > `.env` > дефолти в цьому файлі.

Що ВХОДИТЬ сюди:
    - URL зовнішніх сервісів (база, Telegram API, webhook-и)
    - Секрети (токени, ключі, паролі)
    - Порти й хости
    - Рівень логгінгу (INFO / DEBUG / WARNING)
    - Робочі пороги, які оператор може захотіти підправити
      (алертні межі температури, вологості повітря, вологості ґрунту)

Що НЕ входить сюди:
    - Runtime-обʼєкти (engine, app, роутери, логгери) — вони живуть
      у своїх модулях і будуються ЗІ значень з settings.
    - Ідентичність системи (SERVICE_NAME, VERSION) — це частина коду,
      не конфіг.
    - Магічні числа з чистим кодовим змістом (HTTP-статуси, SECONDS_IN_HOUR).
    - Класи-моделі даних (Measurement, User, тощо).

Правило великого пальця:
    "Чи захоче оператор змінити це без правки коду —
     через `.env` на Raspberry Pi?"
        Так → сюди.
        Ні  → лишити модульною константою там де використовується.
"""

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8")

    database_url: str = "sqlite:///agro.db"


settings = Settings()
