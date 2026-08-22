"""
Тести журналу подій заліза: розбір бітмаска з прошивки, утримання 14 днів
і маршрутизація пакетів в usb_adapter.
"""

from datetime import UTC, datetime, timedelta

import pytest
from sqlmodel import Session, SQLModel, create_engine, select

from events import (
    EVENT_RETENTION_DAYS,
    SOURCE_GATEWAY,
    SOURCE_NODE,
    EventCreate,
    EventRepository,
    NodeEvent,
    decode_flags,
    event_name,
)


@pytest.fixture
def repo() -> EventRepository:
    engine = create_engine("sqlite://")
    SQLModel.metadata.create_all(engine)
    return EventRepository(engine)


# --- розбір бітмаска ---------------------------------------------------------


def test_decode_flags_empty_mask_is_no_events() -> None:
    assert decode_flags(0) == []


def test_decode_flags_single_bit() -> None:
    # EV_LORA_TX_FAIL = 6 -> 1 << 6
    assert decode_flags(1 << 6) == [6]


def test_decode_flags_multiple_bits_arrive_together() -> None:
    """
    Головний сенс липкого бітмаска: поки передача не проходить, вузол копить
    події, і в одному вдалому пакеті приїжджає вся пачка.
    """
    mask = (1 << 2) | (1 << 6) | (1 << 10)

    assert decode_flags(mask) == [2, 6, 10]


def test_decode_flags_ignores_bit_zero() -> None:
    # EV_NONE = 0 — це "подій нема", не подія з кодом 0.
    assert decode_flags(1 << 0) == []


def test_event_name_uses_separate_namespaces() -> None:
    """Код 2 у вузла і в шлюза означає різні речі — простори не спільні."""
    assert event_name(SOURCE_NODE, 2) == "reset_brownout"
    assert event_name(SOURCE_GATEWAY, 2) == "rx_failed"


def test_event_name_unknown_code_does_not_raise() -> None:
    assert event_name(SOURCE_NODE, 14) == "unknown_14"


# --- сховище й утримання -----------------------------------------------------


def test_create_stores_decoded_name(repo: EventRepository) -> None:
    stored = repo.create(EventCreate(node_id="greenhouse-1", code=11, context=417))

    assert stored.name == "battery_critical"
    assert stored.source == SOURCE_NODE
    assert stored.context == 417


def test_purge_removes_only_rows_older_than_retention(
    repo: EventRepository,
) -> None:
    now = datetime.now(UTC)
    with Session(repo.engine) as session:
        session.add(
            NodeEvent(
                node_id="greenhouse-1",
                source=SOURCE_NODE,
                code=5,
                name="lora_init_failed",
                timestamp=now - timedelta(days=EVENT_RETENTION_DAYS + 1),
            )
        )
        session.add(
            NodeEvent(
                node_id="greenhouse-1",
                source=SOURCE_NODE,
                code=5,
                name="lora_init_failed",
                timestamp=now - timedelta(days=EVENT_RETENTION_DAYS - 1),
            )
        )
        session.commit()

    removed = repo.purge_expired()

    assert removed == 1
    with Session(repo.engine) as session:
        survivors = list(session.exec(select(NodeEvent)).all())
    assert len(survivors) == 1


def test_create_triggers_purge(repo: EventRepository) -> None:
    """Ротація не потребує cron — вставка сама прибирає прострочене."""
    with Session(repo.engine) as session:
        session.add(
            NodeEvent(
                node_id="greenhouse-1",
                source=SOURCE_NODE,
                code=5,
                name="lora_init_failed",
                timestamp=datetime.now(UTC) - timedelta(days=30),
            )
        )
        session.commit()

    repo.create(EventCreate(node_id="greenhouse-1", code=6))

    assert len(repo.list_recent()) == 1


# --- маршрутизація в usb_adapter --------------------------------------------


class FakeResponse:
    def raise_for_status(self) -> None:
        pass


class FakeClient:
    """Замість httpx.Client — просто збирає, що адаптер спробував відправити."""

    def __init__(self) -> None:
        self.posts: list[tuple[str, dict]] = []

    def post(self, url: str, json: dict) -> FakeResponse:
        self.posts.append((url, json))
        return FakeResponse()


NODES = {0: "greenhouse-1", 255: "gateway"}


def _handle(line: str) -> FakeClient:
    from usb_adapter import handle_line

    client = FakeClient()
    handle_line(client, NODES, line)  # type: ignore[arg-type]
    return client


def test_healthy_packet_forwards_measurement_only() -> None:
    line = (
        '{"type":"measurement","node_id":0,"air_temperature":22.5,'
        '"air_humidity":60.0,"soil_moisture":45,"vbat":3.9,"boot":12,'
        '"rssi":-80,"snr":9.5}'
    )

    posts = _handle(line).posts

    assert [url for url, _ in posts] == ["/measurements"]


def test_err_bitmask_becomes_one_event_per_code() -> None:
    # EV_RST_BROWNOUT(2) + EV_LORA_TX_FAIL(6) накопичились і доїхали разом.
    line = (
        '{"type":"measurement","node_id":0,"air_temperature":22.5,'
        '"air_humidity":60.0,"soil_moisture":45,"vbat":3.9,"boot":12,'
        '"err":68,"eseq":5,"rssi":-80,"snr":9.5}'
    )

    posts = _handle(line).posts

    assert [url for url, _ in posts] == ["/events", "/events", "/measurements"]
    assert [body["code"] for url, body in posts if url == "/events"] == [2, 6]
    # context = номер прокидання: видно, що подія свіжа, а не торішня.
    assert all(body["context"] == 12 for url, body in posts if url == "/events")


def test_missing_air_fields_drop_measurement_but_keep_event() -> None:
    """
    Раніше прошивка слала тут 0.0 і сервер писав це в базу як справжні
    -0 °C. Тепер полів просто нема, вимір відсівається, причина лишається.
    """
    line = (
        '{"type":"measurement","node_id":0,"soil_moisture":45,"vbat":3.9,'
        '"boot":12,"err":256,"eseq":3,"rssi":-80,"snr":9.5}'
    )

    posts = _handle(line).posts

    assert [url for url, _ in posts] == ["/events"]
    assert posts[0][1]["code"] == 8  # EV_SHT_NAN


def test_gateway_event_routed_to_gateway_namespace() -> None:
    line = '{"type":"event","node_id":255,"code":3,"detail":40}'

    posts = _handle(line).posts

    assert posts == [
        (
            "/events",
            {
                "node_id": "gateway",
                "source": SOURCE_GATEWAY,
                "code": 3,
                "context": 40,
            },
        )
    ]


def test_garbage_line_is_skipped_without_raising() -> None:
    assert _handle("Sleeping 15 min").posts == []


def test_gateway_boot_event_is_named() -> None:
    """Шлюз рапортує старт кодом 4, detail = esp_reset_reason()."""
    assert event_name(SOURCE_GATEWAY, 4) == "boot"


def test_gateway_boot_packet_routed() -> None:
    line = '{"type":"event","node_id":255,"code":4,"detail":1}'

    posts = _handle(line).posts

    assert posts[0][1]["code"] == 4
    assert posts[0][1]["context"] == 1
