"""
Фабрика notifier-ів. Цетралізоване місце вибору "якою реалізацією слати"
Наразі - TelegramNotifier. Потім (при переході на Model Б) - TelegramHubNotifier
або інша реалізація, залежно від конфіга.
"""

from functools import lru_cache

from notifiers.base import Notifier
from notifiers.telegram import TelegramNotifier
from settings import settings


@lru_cache(maxsize=1)
def get_notifier() -> Notifier:
    return TelegramNotifier(
        token=settings.telegram_token,
        chat_id=settings.telegram_chat_id,
    )
