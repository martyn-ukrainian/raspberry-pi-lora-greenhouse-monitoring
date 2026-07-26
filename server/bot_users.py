"""
Реєстр користувачів Telegram-бота.

Бот бачить людину лише коли вона йому напише (перше повідомлення — зазвичай
/start). На кожному вхідному апдейті робимо upsert відправника сюди — так
зʼявляється «книга» всіх, хто підключився.

`allow` керує доступом: False (типово) — людина в списку, але команди
ігноруються; True — може робити /get і отримувати сповіщення. Рішення про
доступ переживає повторні звернення: upsert оновлює chat_id/username, але
`allow` НЕ чіпає (це рішення адміна).
"""

from datetime import UTC, datetime

from sqlmodel import Field, Session, SQLModel, select


class BotUser(SQLModel, table=True):  # type: ignore[call-arg]
    user_id: int = Field(primary_key=True)  # Telegram user id — стабільна особа
    chat_id: int  # куди слати відповідь (у приватному чаті == user_id)
    username: str | None = Field(default=None)
    first_name: str | None = Field(default=None)
    last_name: str | None = Field(default=None)
    allow: bool = Field(default=False)  # доступ до команд і сповіщень
    first_seen: datetime = Field(default_factory=lambda: datetime.now(UTC))
    last_seen: datetime = Field(default_factory=lambda: datetime.now(UTC))


class BotUserRepository:
    def __init__(self, engine):
        self.engine = engine

    def seen(
        self,
        user_id: int,
        chat_id: int,
        username: str | None,
        first_name: str | None = None,
        last_name: str | None = None,
    ) -> BotUser:
        """Записати нового або оновити наявного. `allow` і `first_seen` лишаємо як є."""
        with Session(self.engine) as session:
            user = session.get(BotUser, user_id)
            if user is None:
                user = BotUser(
                    user_id=user_id,
                    chat_id=chat_id,
                    username=username,
                    first_name=first_name,
                    last_name=last_name,
                )
            else:
                user.chat_id = chat_id
                user.username = username
                user.first_name = first_name
                user.last_name = last_name
                user.last_seen = datetime.now(UTC)
            session.add(user)
            session.commit()
            session.refresh(user)
        return user

    def is_allowed(self, user_id: int) -> bool:
        with Session(self.engine) as session:
            user = session.get(BotUser, user_id)
            return bool(user and user.allow)

    def list_all(self) -> list[BotUser]:
        with Session(self.engine) as session:
            return list(session.exec(select(BotUser)).all())

    def set_allow(self, user_id: int, allow: bool) -> BotUser | None:
        """Увімкнути/вимкнути доступ (для Фази 2 — команда /approve)."""
        with Session(self.engine) as session:
            user = session.get(BotUser, user_id)
            if user is None:
                return None
            user.allow = allow
            session.add(user)
            session.commit()
            session.refresh(user)
        return user
