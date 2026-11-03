#!/usr/bin/env bash
# Run on the board after deploying the package.
set -euo pipefail
[[ $EUID == 0 ]] || { echo 'Run as root' >&2; exit 1; }
package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$package_dir" != *[[:space:]%]* ]] || { echo 'Unsupported whitespace or percent in package path' >&2; exit 1; }
unit=/etc/systemd/system/xunjing.service
mkdir -p "$package_dir/results"
if systemctl is-active --quiet xunjing.service; then
  echo 'Stop xunjing.service before reinstalling' >&2
  exit 1
fi
if pgrep -f '^([^[:space:]]*/)?sdicamera[+]' >/dev/null ||
   ! flock -n "$package_dir/results/observation.lock" true; then
  echo 'Detection or summary is still running; wait for it to finish' >&2
  exit 1
fi
backup="$package_dir/results/service_install_$(date -u +%Y%m%dT%H%M%SZ)_$$"
mkdir -p "$backup"
if [[ -f "$unit" ]]; then cp -a "$unit" "$backup/xunjing.service.before"; fi
if [[ -f /etc/default/xunjing ]]; then cp -a /etc/default/xunjing "$backup/"; fi
cat >"$backup/xunjing.service" <<EOF
[Unit]
Description=Xunjing SDI observation and saved behavior statistics
After=local-fs.target
RequiresMountsFor=$package_dir
StartLimitIntervalSec=300
StartLimitBurst=3

[Service]
Type=simple
User=root
WorkingDirectory=$package_dir
Environment=PYTHONUNBUFFERED=1
EnvironmentFile=-/etc/default/xunjing
ExecStart=/bin/bash $package_dir/scripts/run_service.sh
# Signal the launcher; it stops detection and saves statistics before exiting.
KillMode=mixed
KillSignal=SIGINT
TimeoutStopSec=120s
SendSIGKILL=yes
Restart=on-failure
RestartSec=10
StandardInput=null
StandardOutput=journal
StandardError=journal
SyslogIdentifier=xunjing
UMask=0022

[Install]
WantedBy=multi-user.target
EOF
systemd-analyze verify "$backup/xunjing.service"
install -m 644 "$backup/xunjing.service" "$unit"
mkdir -p /usr/local/bin /root/result_json
if [[ -e /usr/local/bin/qwen ]]; then cp -a /usr/local/bin/qwen "$backup/qwen.before"; fi
cat >"$backup/qwen" <<EOF
#!/usr/bin/env bash
exec /bin/bash '$package_dir/scripts/qwen.sh' "\$@"
EOF
install -m 755 "$backup/qwen" /usr/local/bin/qwen
systemctl daemon-reload
systemctl enable xunjing.service
echo "Enabled xunjing.service; backup: $backup"
echo 'Start: systemctl start xunjing; Stop and save JSON: systemctl stop xunjing'
echo 'Summarize manually: qwen --config /root/result_json/observation_....json'
echo 'Logs: journalctl -u xunjing -f -o cat'
