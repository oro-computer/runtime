#!/usr/bin/env bash
set -euo pipefail
public_root="$1"
mkdir -m700 "$public_root/tpm" "$public_root/token" "$public_root/soft-token"
swtpm socket --tpm2 --tpmstate "dir=$public_root/tpm" \
  --ctrl "type=unixio,path=$public_root/tpm.sock.ctrl" \
  --server "type=unixio,path=$public_root/tpm.sock" --flags not-need-init &
public_tpm_pid=$!
public_manager_pid=""
trap 'kill "$public_tpm_pid" ${public_manager_pid:+"$public_manager_pid"} 2>/dev/null || true' EXIT
for attempt in {1..100}; do
  [[ -S "$public_root/tpm.sock" ]] && break
  sleep 0.02
done
export TPM2TOOLS_TCTI="swtpm:path=$public_root/tpm.sock"
tpm2_startup -c
public_root_option=()
if [[ "$(id -u)" == 0 ]]; then public_root_option=(--allow-root); fi
tpm2-abrmd --session --flush-all "${public_root_option[@]}" \
  --tcti="$TPM2TOOLS_TCTI" >"$public_root/resource-manager.log" 2>&1 &
public_manager_pid=$!
for attempt in {1..100}; do
  if gdbus call --session --dest com.intel.tss2.Tabrmd --object-path /com/intel/tss2/Tabrmd \
    --method org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1; then break; fi
  sleep 0.05
done
export TPM2TOOLS_TCTI=tabrmd:bus_type=session
export TPM2_PKCS11_TCTI="$TPM2TOOLS_TCTI"
export TPM2_PKCS11_STORE="$public_root/token"
python3 -m tpm2_pkcs11.tpm2_ptool init --path="$TPM2_PKCS11_STORE"
python3 -m tpm2_pkcs11.tpm2_ptool addtoken --pid=1 --label=public-runtime-test \
  --sopin=public-test-so-pin --userpin=public-test-user-pin --path="$TPM2_PKCS11_STORE"
for public_family in tpm2 pkcs11; do
  mkdir -m700 "$public_root/$public_family"
  ORO_PUBLIC_PROVIDER_FAMILY="$public_family" timeout 100 "$public_root/protected-keys" \
    "$public_root/$public_family" "$public_root/public-pkcs11.so" "$public_family"
done
printf 'directories.tokendir = %s\nobjectstore.backend = file\n' "$public_root/soft-token" >"$public_root/softhsm.conf"
export SOFTHSM2_CONF="$public_root/softhsm.conf"
softhsm2-util --init-token --free --label=public-software-test --so-pin=public-test-so-pin --pin=public-test-user-pin
timeout 20 "$public_root/protected-keys" "$public_root/soft-token" /usr/lib/softhsm/libsofthsm2.so software
