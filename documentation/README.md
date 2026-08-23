# Agro — документація / Documentation

Сайт: **https://martyn-ukrainian.github.io/raspberry-pi-lora-greenhouse-monitoring/**
(збирається з цієї теки автоматично при пуші в `main`).

| 🇺🇦 Українська | 🇬🇧 English |
|---|---|
| [00 Карта проєкту](uk/00-roadmap.md) | _after the Ukrainian version is settled_ |
| [01 Огляд](uk/01-overview.md) | |
| [02 Архітектура](uk/02-architecture.md) | |
| [03 Залізо](uk/03-hardware.md) | |
| [04 Прошивки](uk/04-firmware.md) | |
| [05 Сервер](uk/05-server.md) | |
| [06 Клієнти](uk/06-clients.md) | |
| [07 Хмарна телеметрія калібрування](uk/07-cloud-telemetry.md) | |
| [08 Експлуатація](uk/08-operations.md) | |
| [Дослідження: стрічка часу](uk/research/index.md) | |
| [Гайди](uk/guides/index.md) · [Калібрувальний стенд](uk/guides/soil-calibration-bench.md) | |
| [Журнал рішень](uk/decisions.md) | |

Структура й правила — у карті проєкту, розділ 6.
Робочі логи й специфікації лишаються в [`../docs/`](../docs/); тут — довідник,
дослідження, гайди й рішення. Картинки — `assets/`.

## Локально

```bash
uv venv .venv-docs
uv pip install --python .venv-docs -r requirements-docs.txt
.venv-docs/bin/mkdocs serve        # http://127.0.0.1:8000
```

Конфіг — `mkdocs.yml` у корені репо. Мови розкладені по теках: `uk/` — основна,
`en/` — поки порожня і в `mkdocs.yml` стоїть `build: false`; коли переклади
зʼявляться, перемикач мов вмикається однією зміною прапорця.
Спільні картинки — `assets/`, вони не залежать від мови.
