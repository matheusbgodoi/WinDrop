#!/bin/bash
# Installs, or with --remove takes away, the timed Wi-Fi sniffer on the Mac:
#   sudo bash install-sniffer.sh [--remove]
# It puts two root-owned files in /usr/local/winawdl and one sudoers rule that
# lets this account run that one script without a password. Nothing else.
set -eu
home=/usr/local/winawdl
rule=/etc/sudoers.d/winawdl-sniff
here=$(cd "$(dirname "$0")" && pwd)

[[ $(id -u) -eq 0 ]] || { echo "run it with sudo"; exit 1; }
if [[ ${1:-} == --remove ]]; then
  rm -rf "$home" "$rule"
  echo "sniffer removed"
  exit 0
fi

user=${SUDO_USER:?run it with sudo from your own account}
install -d -o root -g wheel -m 755 "$home" "$home/capture"
install -o root -g wheel -m 755 "$here/winawdl-chan" "$here/winawdl-sniff" "$home/"
tmp=$(mktemp)
echo "$user ALL=(root) NOPASSWD: $home/winawdl-sniff" >"$tmp"
visudo -cf "$tmp" >/dev/null
install -o root -g wheel -m 440 "$tmp" "$rule"
rm -f "$tmp"
echo "sniffer installed"
