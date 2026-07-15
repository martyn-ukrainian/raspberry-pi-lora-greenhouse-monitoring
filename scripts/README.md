# scripts/

Утиліти для деплою і роботи з Pi.

## `pi.sh` — SSH-шорткат до Raspberry Pi

Стукає в `agro@agro-pi.local`. Хост можна переопреділити через `pi.env`
(файл гітнориться — можеш тримати там свої значення).

### Використання

```bash
# інтерактивна SSH-сесія:
./scripts/pi.sh

# разова команда без відкриття шела:
./scripts/pi.sh systemctl status agro-server
./scripts/pi.sh journalctl -u agro-server -f
./scripts/pi.sh sudo systemctl restart agro-server
```

### Кастомізація

Скопіюй приклад:

```bash
cp scripts/pi.env.example scripts/pi.env
```

Онови під себе якщо hostname/user відрізняються:

```
PI_USER=agro
PI_HOST=192.168.0.42
```

### Глобальний alias

Щоб команду `pi` можна було викликати з будь-якої папки, додай у
`~/.zshrc` (Mac) або `~/.bashrc` (Linux):

```bash
alias pi='/Users/martyn/development/agro/scripts/pi.sh'
```

Перезавантаж shell:

```bash
source ~/.zshrc
```

Тепер:

```bash
pi                              # SSH
pi journalctl -u agro-server -f # follow-лог
```

## SSH-авторизація без пароля

Скрипт **не зберігає пароль** — це навмисно. Правильний спосіб — SSH-ключ.
Раз налаштував → щоразу заходиш без пароля.

```bash
# на Mac, якщо ключа ще нема:
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519 -N ""

# скопіювати публічний ключ на Pi (введеш пароль востаннє):
ssh-copy-id agro@agro-pi.local

# тест:
ssh agro@agro-pi.local
# без запиту паролю → готово
```

## Довідник команд на Pi

Детальний cheatsheet усіх Pi-операцій (логи, рестарт, deploy, БД,
здоровʼя) — у `docs/pi-cheatsheet.md`.
