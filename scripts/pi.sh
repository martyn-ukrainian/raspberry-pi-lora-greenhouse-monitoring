#!/usr/bin/env bash
# SSH shortcut до Pi. Читає хост з scripts/pi.env або з env-змінних,
# з fallback на дефолти.
#
# Приклад pi.env:
#   PI_USER=agro
#   PI_HOST=agro-pi.local
#
# Використання:
#   ./scripts/pi.sh                    # інтерактивний SSH
#   ./scripts/pi.sh systemctl status agro-server   # разова команда

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
env_file="$script_dir/pi.env"
if [ -f "$env_file" ]; then
  # shellcheck disable=SC1090
  source "$env_file"
fi

PI_USER="${PI_USER:-agro}"
PI_HOST="${PI_HOST:-agro-pi.local}"

if [ $# -eq 0 ]; then
  exec ssh "$PI_USER@$PI_HOST"
else
  exec ssh "$PI_USER@$PI_HOST" "$@"
fi
