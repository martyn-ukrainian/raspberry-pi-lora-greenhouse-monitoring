# Залежності та модулі — довідник

> Файл щоб пам'ятати **що і навіщо ми встановили**, і **які stdlib-речі використали вперше**.
> Формат кожного пункту: *що це* → *навіщо в цьому проекті* → *ключова ідея* → *підводні камені*.

## TL;DR — карта того що стоїть у `server/`

**Прямі залежності** (`uv add ...`):

| Пакет | Група | Роль у Agro |
|---|---|---|
| `fastapi[standard]` | prod | HTTP-сервер + автогенерований OpenAPI + весь стандартний тулінг |
| `sqlmodel` | prod | ORM для SQLite (Pydantic + SQLAlchemy в одному класі) |
| `pyyaml` | prod | Читає `thresholds.yaml`, `messages.yaml` |
| `httpx` | dev | HTTP-клієнт для запитів до Telegram Bot API |
| `pytest` | dev | Фреймворк для тестів |
| `ruff` | dev | Лінтер + форматер |

**Транзитивно приходять** (не додавали руками, але імпортуємо):

| Пакет | Звідки прийшов | Роль |
|---|---|---|
| `pydantic` | fastapi + sqlmodel | Дата-валідація, `BaseModel`, `model_validator` |
| `pydantic-settings` | fastapi[standard] | Читає `.env` в `settings.py` |
| `starlette` | fastapi | ASGI-framework, на якому побудований FastAPI |
| `uvicorn[standard]` | fastapi[standard] | ASGI-сервер (`fastapi run` під капотом) |
| `jinja2` | fastapi[standard] | Шаблонізатор HTML — ми не використовуємо, але тягнеться |
| `python-multipart` | fastapi[standard] | Парсить `multipart/form-data` — не використовуємо |
| `email-validator` | fastapi[standard] | Валідація email — не використовуємо |

Все інше — SQLAlchemy, typing-extensions тощо — це залежності залежностей, ти їх не бачиш у коді.

---

## Прямі залежності

### `fastapi` + `[standard]`

**Що це.** Веб-фреймворк на Python для будування HTTP-API. Побудований поверх Starlette (ASGI-фреймворк) і Pydantic (валідація). Головна фіча — вивчає type hints і з них будує документацію + валідацію запиту.

**Навіщо в Agro.** `POST /measurements` приймає JSON від gateway, валідує, кидає в БД, запускає alert-check. `GET /health` — простий пінг для моніторингу.

**Що дає `[standard]`.** Це "extra" — пачка додаткових залежностей для звичайних сценаріїв:
- `uvicorn[standard]` — ASGI-сервер щоб запустити app
- `httpx` — HTTP-клієнт (те саме що ми ще й у dev-групі маємо)
- `python-multipart`, `jinja2`, `email-validator` — сценарії які ми поки не юзаємо
- `fastapi-cli` — команди `fastapi run` / `fastapi dev`

**Ключова ідея.** Function signature = контракт API. Пишеш `def create(payload: MeasurementIn) -> Measurement`, і FastAPI сам парсить request body у `MeasurementIn`, валідує його, серіалізує відповідь.

**Gotchas.**
- `Depends()` — це dependency injection. `settings: Annotated[Settings, Depends(get_settings)]` означає "перед тим як зайти в handler, викликай `get_settings()` і подай результат сюди".
- `[standard]` тягне багато чого. Можна взяти чистий `fastapi` без extra якщо хочеш мінімальні залежності — але тоді `uvicorn`, `httpx` треба ставити руками.

### `sqlmodel`

**Що це.** ORM від автора FastAPI. Гібрид: один клас одночасно є Pydantic-моделлю (для валідації JSON) і SQLAlchemy-моделлю (для БД). Замість двох окремих класів — один.

**Навіщо в Agro.** `Measurement` — саме такий гібрид. Той самий клас і валідує вхідний JSON, і мапиться на таблицю в SQLite.

**Ключова ідея.**

```python
class Measurement(SQLModel, table=True):
    id: int | None = Field(default=None, primary_key=True)
    node_id: str
    temperature: float
```

`table=True` каже "це справжня таблиця". Без цього прапорця клас працює тільки як Pydantic-модель (типово для request/response).

**Session** — це "робочий сеанс" з БД. Всередині нього ти робиш `session.add()`, `session.commit()`. Session — не thread-safe, тому в FastAPI кожен request отримує свій через `Depends`.

**Gotchas.**
- `session.refresh(obj)` після `commit` — щоб побачити authoritative значення з БД (id, defaults).
- `select(Measurement).where(...)` повертає statement, а не результат. Треба `session.exec(stmt).all()` щоб виконати.
- `Field(default=None, primary_key=True)` — тип `int | None`, бо до вставки `id=None`, а після — БД дає число.

### `pyyaml`

**Що це.** Парсер YAML-файлів у Python-структури (dict/list/scalar).

**Навіщо в Agro.** `thresholds.yaml` і `messages.yaml` — конфігурація не в коді. Один рядок `yaml.safe_load(open(path))` віддає dict.

**Ключова ідея.** YAML — це людино-читабельний JSON з відступами замість дужок. Читаємо через `safe_load` (не `load`), бо `load` може виконувати довільний Python — це загроза безпеки на недовіреному вводі.

**Gotchas.** Тип "yes"/"no"/"on"/"off" у YAML 1.1 парситься як bool. Число `1.0` → float, `1` → int. Дати у форматі `2026-01-01` — це `date` об'єкт, не string.

### `httpx` (dev + через fastapi[standard])

**Що це.** Сучасний HTTP-клієнт на Python. Синтаксис як у `requests`, але підтримує async, HTTP/2, і краще типізований.

**Навіщо в Agro.** У `telegram.py` — робимо POST на Telegram Bot API. У тестах — можна крутити `TestClient` (він на httpx).

**Ключова ідея.** `httpx.post(url, json=data, timeout=5)` — синхронно. `async with httpx.AsyncClient() as client: await client.post(...)` — асинхронно.

**Gotchas.**
- Без `timeout` httpx чекає **вічно** — це серйозна пастка в проді.
- `response.raise_for_status()` — кидає виключення на 4xx/5xx. Якщо не викликати — доведеться руками перевіряти `response.status_code`.

### `pytest`

**Що це.** Стандартний фреймворк для тестів. Знаходить файли `test_*.py`, функції `def test_*(...)`, запускає їх, перевіряє `assert`-и.

**Навіщо в Agro.** `tests/test_alerts.py` — юніт-тести для alert-state-machine.

**Ключова ідея.** Не потрібен клас-обгортка як у `unittest`. `assert x == y` — і pytest сам покаже красиву різницю на падінні.

**Fixture** — це функція яка налаштовує стан для тесту:
```python
@pytest.fixture(autouse=True)
def clear_state():
    _reset()  # перед кожним тестом
    yield
    _reset()  # після кожного тесту
```

`autouse=True` — застосовується автоматично. Без нього fixture треба явно передавати у тест-функцію як аргумент.

**Gotchas.**
- `pytest --tb=short` — коротший traceback на падіннях.
- `pytest -k "test_dwell"` — запустити тільки тести з "test_dwell" у назві.
- `pytest -x` — зупинитись на першій помилці.

### `ruff`

**Що це.** Швидкий (написаний на Rust) лінтер для Python. Замінює одразу flake8, isort, pyupgrade, autoflake і купу інших плагінів.

**Навіщо в Agro.** `.pre-commit-config.yaml` запускає `ruff-check` на кожному коміті. `pyproject.toml [tool.ruff.lint]` вибирає які правила застосовувати.

**Правила які ми ввімкнули** (з `pyproject.toml`):

| Код | Значення |
|---|---|
| `E` | pycodestyle errors — порушення PEP 8 (форматування, пробіли) |
| `W` | pycodestyle warnings — застереження PEP 8 |
| `F` | Pyflakes — реальні баги: невживані змінні, невизначені імена, неправильні імпорти |
| `I` | isort — порядок імпортів (stdlib → third-party → local) |
| `UP` | pyupgrade — сучасний Python-синтаксис замість застарілого (`typing.List` → `list`, `X | Y` замість `Union`) |
| `B` | flake8-bugbear — типові пастки (mutable default args `def f(x=[])`, `for` без break у finally) |
| `SIM` | flake8-simplify — натяки на спрощення (`if not x` замість `if x == False`) |

**Ключова ідея.** `ruff check` — знайти проблеми. `ruff check --fix` — виправити автоматично те що можна. `ruff format` — це форматер (окрема команда, не з lint-правилами).

**Gotchas.**
- Правила B і SIM бувають агресивними — читай коментар до warning-у, іноді треба залишити як є.
- `# noqa: B008` — глушить конкретне правило на конкретному рядку. Не зловживай.

---

## Транзитивні які ми імпортуємо в коді

### `pydantic`

**Що це.** Бібліотека для дата-валідації через type hints. Ти пишеш `class X(BaseModel): name: str; age: int`, і Pydantic на створенні `X(name="a", age=5)` перевіряє типи, кастить, і кидає `ValidationError` на несумісному вводі.

**Навіщо в Agro.** У `thresholds.py` — `SensorThresholds(BaseModel)` з `model_validator` перевіряє що `min ≤ max`. У `alerts.py` — модель `Alert` для типізованого payload у нотифайер.

**Ключова ідея.**

```python
class SensorThresholds(BaseModel):
    min: float
    max: float

    @model_validator(mode="after")
    def check_range(self):
        if self.min > self.max:
            raise ValueError("min > max")
        return self
```

`mode="after"` — валідатор бачить вже готовий об'єкт з `self.min`/`self.max`. `mode="before"` — сирі dict-and раніше валідації полів.

**Gotchas.**
- Pydantic v2 — не сумісний з v1 повністю. Ми на v2 (`pydantic v2.13`), тому `@validator` (старий) → `@field_validator` (новий). `model_validator` замість `root_validator`.
- `BaseModel` за замовчуванням immutable ("frozen") немає — можна змінювати атрибути. Якщо хочеш immutable — `class Config: frozen = True`.

### `pydantic-settings`

**Що це.** Продовження Pydantic для конфігурації з env-змінних та `.env` файлів.

**Навіщо в Agro.** `settings.py` — типізований доступ до `DATABASE_URL`, `TELEGRAM_TOKEN`. Замість `os.environ["FOO"]` (без валідації, без типізації) — `settings.database_url` (int/str/URL/pydantic-type, гарантовано присутній).

**Ключова ідея.**

```python
class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8")
    database_url: str
    telegram_token: str
    telegram_chat_id: int

settings = Settings()  # crashe при старті якщо чогось нема
```

**Fail-fast:** якщо `.env` без `TELEGRAM_TOKEN` — сервер не стартує. Це добре — краще впасти зразу, ніж мовчки не слати алерти.

**Gotchas.**
- `.env` **ніколи не в git**. Додай у `.gitignore`. У нас це вже наболіла тема — треба перевірити.
- `env_prefix="AGRO_"` — якщо хочеш іменувати змінні `AGRO_DATABASE_URL` для уникнення конфліктів у контейнерах.

### `starlette`

**Що це.** ASGI-фреймворк (async serving gateway interface) на якому побудований FastAPI. Обробляє HTTP, WebSocket, middleware.

**Навіщо в Agro.** Не імпортуємо напряму, але FastAPI усе робить через нього. `Response`, `Request`, middleware — все звідти.

**Ключова ідея.** ASGI — це наступник WSGI (Flask/Django) з підтримкою async і WebSockets. Якщо колись бачиш "your ASGI app" — це воно.

### `uvicorn[standard]`

**Що це.** ASGI-сервер — крутить твоє FastAPI-додаток. Аналог Gunicorn для WSGI-додатків.

**Навіщо в Agro.** Коли ти пишеш `fastapi run main.py` — під капотом стартує uvicorn.

**`[standard]` тягне:** `httptools` (швидший HTTP-parser), `websockets` (для WS), `watchfiles` (для auto-reload у dev), `uvloop` (швидший asyncio event loop, тільки не на Windows).

**Gotchas.** У проді `fastapi run` — це одне worker-а. Для multiple workers — `uvicorn main:app --workers 4` або Gunicorn з UvicornWorker.

---

## Stdlib що ми використали

Це не пакети — це вбудований Python. Але для JS-фону деякі речі бачив уперше.

### `functools.lru_cache`

**Що це.** Декоратор який кешує результат виклику функції за аргументами. Наступний виклик з тими ж аргументами — миттєвий, з кешу.

**Навіщо в Agro.**

```python
@lru_cache
def get_settings() -> Settings:
    return Settings()
```

Settings читає `.env` один раз. Далі кожен `Depends(get_settings)` бере з кешу, а не парсить `.env` заново на кожен request.

**Ключова ідея.** `lru` = "least recently used" — кеш обмеженого розміру, старе витісняється. Без аргументу `maxsize=128` за замовчуванням.

**Gotchas.** Кеш живе весь час процесу. Якщо `.env` змінився — треба рестарт, або `get_settings.cache_clear()`.

### `pathlib.Path`

**Що це.** OOP-шлях до файлів. Замість `os.path.join(a, b)` — `Path(a) / b`. Замість `os.path.exists(p)` — `Path(p).exists()`.

**Навіщо в Agro.** У `logger.py` — `Path("logs")`, у `messages.py` — `Path(__file__).parent / "config" / "messages.yaml"`.

**Ключова ідея.** `Path` — cross-platform. Оператор `/` — це "join path", працює на Windows і Unix однаково. `.parent`, `.name`, `.suffix` — інтуїтивні атрибути.

**Gotchas.** Path в JSON не серіалізується напряму — треба `str(path)`.

### `logging` + `logging.handlers.TimedRotatingFileHandler`

**Що це.** Стандартний логер Python. `TimedRotatingFileHandler` — пише в файл, і о заданому часу (`when="midnight"`) починає новий файл, старий перейменовує.

**Навіщо в Agro.** `logger.py` — файлові логи з rotation. 14 файлів (2 тижні), потім старі видаляє.

**Ключова ідея.**

```python
handler = TimedRotatingFileHandler(
    "logs/app.log",
    when="midnight",       # обертати опівночі
    interval=1,            # кожен 1 день
    backupCount=14,        # тримати 14 старих файлів
)
```

Логер — це ієрархія: `logging.getLogger("agro.alerts")` — дочка `logging.getLogger("agro")` — дочка root. Handler на батьку — застосовується до дітей.

**Gotchas.**
- Не роби `print()` для логів у сервері — воно йде в stdout без метаданих (time, level, module).
- Різні модулі — `logging.getLogger(__name__)`. Тоді в логу видно звідки повідомлення.

### `typing.Protocol`

**Що це.** Duck-typing з type-check-ами. Клас яким наслідуєш `Protocol` — це "інтерфейс" у сенсі TypeScript-у. Будь-який об'єкт з підходящою сигнатурою підходить, без явного `inherit`.

**Навіщо в Agro.**

```python
class Notifier(Protocol):
    def send(self, alert: Alert) -> None: ...
```

Далі функція `def broadcast(notifiers: list[Notifier]): ...` приймає будь-який об'єкт з методом `send`. `TelegramNotifier` не наслідує `Notifier` явно — просто має правильну сигнатуру, і mypy/ruff це бачать.

**Ключова ідея.** У JS/TS це нативний паттерн (structural typing). У Python традиційно nominal (`class B(A)`), а Protocol приносить structural.

**Gotchas.** Protocol перевіряється тільки type checker-ом (mypy). На runtime він не форсить нічого — можна передати неправильний об'єкт, і впаде тільки коли викличеш метод.

### `typing.Annotated`

**Що це.** Спосіб додати метадату до типу. `Annotated[int, "positive"]` — тип все ще int, але з додатковою міткою яку хтось (FastAPI, Pydantic) може прочитати.

**Навіщо в Agro.**

```python
def create(
    payload: MeasurementIn,
    session: Annotated[Session, Depends(get_session)],
): ...
```

`Annotated[Session, Depends(...)]` каже FastAPI: "тип `Session`, і візьми його через `Depends(get_session)`". Метадата — це `Depends()`.

**Ключова ідея.** Це модерніший спосіб writing `session: Session = Depends(get_session)`. Обидва працюють, але `Annotated` — офіційно рекомендований, бо не змішує default value з dependency.

**Gotchas.** У функціях з дефолтами: `x: Annotated[int, Depends(...)] = 5` — дефолт `5` це справжній default. `x: int = Depends(...)` — тут `Depends()` займає місце default, і `x` не має справжнього default-а.

### `dataclasses.dataclass`

**Що це.** Декоратор який автоматично додає `__init__`, `__repr__`, `__eq__` до класу з полів.

**Навіщо в Agro.** `Alert` у `alerts.py` — простий data-контейнер, не потребує валідації Pydantic. Легкий і без залежностей.

**Ключова ідея.**

```python
@dataclass
class Alert:
    node_id: str
    sensor: str
    value: float
```

Без dataclass довелось би писати `__init__` руками.

**Коли dataclass vs BaseModel:**
- `@dataclass` — швидкий data-контейнер, без валідації, без залежностей. Оптимальний коли тобі просто structure.
- `BaseModel` (Pydantic) — з валідацією, серіалізацією в JSON, коли треба обробляти external input.

### `datetime.UTC`

**Що це.** Готовий tz-aware timezone-об'єкт для UTC (з Python 3.11+). До нього — `timezone.utc`.

**Навіщо в Agro.** `datetime.now(UTC)` — правильний спосіб отримати timestamp у БД. `datetime.now()` без tz — "naive", БД не знає в якій timezone число.

**Ключова ідея.** У ML/backend завжди зберігай **UTC**. Локальні timezone-и — тільки на UI-layer при показі юзеру.

**Gotchas.**
- `datetime.utcnow()` — **застаріле**. Повертає naive datetime, що є багом. Ruff `UP`-правило це підсвітить.
- SQLite не має власного `TIMESTAMP` типу з tz. Зберігається як ISO-string. Треба явно `datetime.fromisoformat()` при читанні.

---

## Що стоїть але не імпортуємо (транзитивні)

Тобі не треба їх пам'ятати — просто знай що вони є в `uv.lock` бо їх тягне FastAPI/SQLModel:

- **sqlalchemy** — ORM під капотом SQLModel. Всі `session.exec()`/`select()` — це воно.
- **jinja2** — шаблонізатор HTML. FastAPI тягне, ми не юзаємо (у нас чистий JSON API).
- **python-multipart** — парсер `multipart/form-data`. Треба для upload файлів через API, ми не робимо.
- **email-validator** — валідує emailи (Pydantic `EmailStr`). Не юзаємо.
- **typing-extensions** — backport нових typing-фіч на старі Python-и. Sqlmodel/pydantic самі імпортують.
- **httptools/uvloop/watchfiles/websockets** — під капотом `uvicorn[standard]`.
- **starlette** — вже описано, ASGI-framework під FastAPI.

---

## Як швидко пригадати ці модулі

- **Що поставлено:** `uv tree --depth 2` у `server/`.
- **Що імпортується в коді:** `grep -rh "^import\|^from" server/*.py` (стосується прямих використань).
- **Версії:** `uv.lock` — точні версії, локк-файл. `pyproject.toml` — мінімальні межі (`>=0.139.0`).
- **Оновити пакет:** `uv add fastapi@latest`.
- **Видалити:** `uv remove pyyaml`.
- **Що новеньке в екосистемі:** `uv outdated` покаже застарілі версії.

---

## Що ще варто вивчити (out of scope for now)

Це не встановлене в Agro, але для інших Python-проектів колись стане в пригоді:

- **`asyncio`** — стандартний async runtime. Стосується коли пишеш `async def` handler у FastAPI.
- **`aiosqlite`** — async-драйвер SQLite. Треба якщо перехочеш зробити FastAPI повністю async.
- **`alembic`** — міграції для SQLAlchemy/SQLModel. Не потрібне поки БД одна таблиця, але для eventual "додати колонку без drop-у" — треба.
- **`structlog`** — структурне логування (JSON, key-value). Наш `logging` теж може, але `structlog` зручніший для аналізу logs у продакшені.
