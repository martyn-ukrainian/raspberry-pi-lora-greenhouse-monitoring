import httpx

from bot_users import BotUserRepository
from database import engine
from logger import get_logger, setup_logging
from settings import settings

logger = get_logger(__name__)

TELEGRAM_API = f"https://api.telegram.org/bot{settings.telegram_token}"

repo = BotUserRepository(engine)


def build_status(client) -> str:
    ghs = client.get(f"{settings.server_url}/greenhouses").json()
    lines = []

    for gh in ghs:
        m = client.get(
            f"{settings.server_url}/measurements/latest",
            params={"node_id": gh["node_id"]},
        ).json()

        if m is None:
            lines.append(f"🏠 {gh['label']}: ще нема вимірів")
            continue

        t, h, s = m["air_temperature"], m["air_humidity"], m["soil_moisture"]
        lines.append(
            f"🏠 <b>{gh['label']}</b>\n"
            f"🌡 {t}°C  💧 {h}%  🌱 {s}%"
        )

    return "\n\n".join(lines)


def run() -> None:
    offset = None

    # timeout клієнта має бути більший за long-poll timeout (30)
    with httpx.Client(timeout=35.0) as client:
        logger.info("Telegram bot started (long polling)")
        while True:
            resp = client.get(
                f"{TELEGRAM_API}/getUpdates",
                params={"offset": offset, "timeout": 30},
            )

            for update in resp.json()["result"]:
                # зсуваємо offset ДО обробки — щоб поганий апдейт не зациклився
                offset = update["update_id"] + 1

                try:
                    message = update.get("message")
                    if message is None:
                        continue

                    from_user = message.get("from")
                    if from_user is None:
                        continue

                    user_id = from_user["id"]
                    chat_id = message["chat"]["id"]

                    # записуємо КОЖНОГО, хто написав — це «книга» підключень
                    repo.seen(
                        user_id=user_id,
                        chat_id=chat_id,
                        username=from_user.get("username"),
                        first_name=from_user.get("first_name"),
                        last_name=from_user.get("last_name"),
                    )

                    command = message.get("text", "").split("@")[0]
                    is_admin = str(chat_id) == settings.telegram_chat_id

                    if command == "/get":
                        if is_admin or repo.is_allowed(user_id):
                            reply = build_status(client)
                        else:
                            reply = "⛔️ Немає доступу. Адмін має тебе підтвердити."
                        client.post(
                            f"{TELEGRAM_API}/sendMessage",
                            json={
                                "chat_id": chat_id,
                                "text": reply,
                                "parse_mode": "HTML",
                            },
                        )
                except Exception:
                    # один збій не має вбивати бота — лог і далі
                    uid = update.get("update_id")
                    logger.exception("Failed to handle update %s", uid)


if __name__ == "__main__":
    setup_logging()
    try:
        run()
    except KeyboardInterrupt:
        logger.info("Telegram bot stopped")
