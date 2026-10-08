#!/usr/bin/env bash
# Inventario de todo lo que puede meterse en cada operación de archivo, proceso o socket:
# cifrado, indexado, SIP, MDM, extensiones de seguridad (EDR/antivirus/firewall).
# uso: inventory.sh <workdir>
W=$1

echo "== sistema"
uname -a
if [ "$(uname -s)" != Darwin ]; then
  head -2 /etc/os-release
  echo "cpus: $(nproc)"
  free -g | head -2
  echo "== volumen de trabajo"
  df -T "$W"
  exit 0
fi

sw_vers
sysctl -n machdep.cpu.brand_string hw.ncpu hw.memsize
echo "== volumen de trabajo"
df -h "$W"
diskutil info "$(df "$W" | tail -1 | awk '{print $1}')" |
  grep -E 'File System Personality|Encrypted|FileVault|Solid State|Protocol'
echo "== FileVault"
fdesetup status
echo "== SIP"
csrutil status
echo "== Gatekeeper"
spctl --status
echo "== Spotlight en el volumen de trabajo"
mdutil -s "$(df "$W" | tail -1 | awk '{print $NF}')" 2>&1 | tail -1
echo "== MDM"
profiles status -type enrollment 2>&1
echo "== system extensions (acá aparecen EDR, antivirus, firewalls, VPNs)"
systemextensionsctl list 2>&1
echo "== kexts de terceros"
kextstat 2>/dev/null | grep -v com.apple | tail -n +2
echo "== procesos de seguridad conocidos corriendo"
pgrep -il 'sentinel|falcon|crowdstrike|kandji|jamf|defender|wdav|code42|cyberhaven|netskope|zscaler|sophos|esets_daemon|littlesnitch|lulu|santa|osquery|elastic|carbonblack|cylance|forcepoint|intune' | sort -u
