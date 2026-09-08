#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
if [[ -z "$ORO_ENV_FILENAME" ]]; then
  ORO_ENV_FILENAME=".oro.env"
fi
export ORO_ENV_FILENAME

declare log_and_run_sudo_granted=0

# BSD stat has no version argument or reliable identifier
if stat --help 2>&1 | grep "usage: stat" >/dev/null; then
  stat_format_arg="-f"
  stat_mtime_spec="%m"
  stat_size_spec="%z"
  _sha512sum="shasum -a512"
else
  # GNU_STAT
  stat_format_arg="-c"
  stat_mtime_spec="%Y"
  stat_size_spec="%s"
  _sha512sum="sha512sum"
fi

function stat_mtime () {
  stat $stat_format_arg $stat_mtime_spec "$1" 2>/dev/null
  local rc=$?
  if (( rc != 0 )); then
    echo 0
  fi
  return $rc
}

function latest_mtime() {
  local latest=0

  while (( $# > 0 )); do
    declare arg="$1"; shift
    mtime="$(stat_mtime "$arg")"
    (( mtime > latest )) && latest=$mtime
  done

  echo "$latest"
}

function copy_if_newer() {
  local source="$1"
  local destination="$2"

  if [[ -z "$source" ]] || [[ -z "$destination" ]]; then
    return 1
  fi

  if [[ ! -e "$source" ]]; then
    return 1
  fi

  if [[ ! -e "$destination" ]] || (( $(stat_mtime "$source") > $(stat_mtime "$destination") )); then
    cp -fp "$source" "$destination"
  fi
}

function stat_size () {
  stat $stat_format_arg $stat_size_spec "$1" 2>/dev/null
}

function sha512sum() {
  # Can't figure out a better way of escaping $_sha512sum for use in a call than using sh -c
  sh -c "$_sha512sum $1|cut -d' ' -f1"
}

function escape_path() {
  local r=$1
  local host="$(host_os)"
  if [[ "$host" == "Win32" ]]; then
    r=${r//\\/\\\\}
  fi
  echo "$r"
}

function unix_path() {
  local p=${1//\"/}
  local dont_escape_space="$2"
  p="$(escape_path "$p")"
  local host="$(host_os)"
  if [[ "$host" == "Win32" ]]; then
    p="$(cygpath -u "$p")"
    # when testing paths we shouldn't escape spaces
    # TODO(mribbons): make this the default behaviour under Win32, review other uses of unix_path()
    if [[ -z "$dont_escape_space" ]]; then
      # cygpath doesn't escape spaces
      p="${p//\ /\\ }"
    fi
    echo  "$p"
    return
  fi
  echo "$p"
}

function native_path() {
  local host="$(host_os)"
  if [[ "$host" == "Win32" ]]; then
    local p="$(cygpath -w "$1")"
    if [[ "$p" == *"\\ "* ]]; then
      # string contains escaped space, quote it and de-escape
      p="\"${p//\\ /\ }\""
    fi
    echo "$p"
    return
  fi
  echo "$1"
}

function quiet () {
  # Support spaces in command by quoting initial argument
  # Just quoting first argument on calling side doesn't work
  declare command="$1"; shift
  declare quiet_output=""
  declare quiet_rc=0
  if [ -n "$VERBOSE" ]; then
    echo "$command" "$@"
    if [[ "$(host_os)" != "Win32" ]]; then
      eval "$command $@"
    else
      "$command" "$@"
    fi
  else
    if [[ "$(host_os)" != "Win32" ]]; then
      quiet_output="$(eval "$command $@" 2>&1)" || quiet_rc=$?
    else
      quiet_output="$("$command" "$@" 2>&1)" || quiet_rc=$?
    fi

    if (( quiet_rc != 0 )) && [[ -n "$quiet_output" ]]; then
      printf '%s\n' "$quiet_output" >&2
    fi

    return "$quiet_rc"
  fi

  return $?
}

# Always logs to terminal, but respects VERBOSE for command output
function log_and_run () {
  if [[ "$*" =~ "sudo "* ]] && (( !log_and_run_sudo_granted )); then
    local args=($@)
    if ! prompt_yn "'oroc' would like to use 'sudo' to run: ${args[*]:1}"; then
      return 1
    fi

    log_and_run_sudo_granted=1
  fi

  write_log "h" "$@"

  if [ -n "$VERBOSE" ]; then
    "$@"
  else
    "$@" > /dev/null 2>&1
  fi

  return $?
}

function die {
  local status=$1
  if (( status != 0 )); then
    for pid in "${pids[@]}"; do
      kill TERM $pid >/dev/null 2>&1
      kill -9 $pid >/dev/null 2>&1
      wait "$pid" 2>/dev/null
    done
    write_log "h" "$2 - please report (https://discord.gg/YPV32gKCsH)"
    exit 1
  fi
}

function onsignal () {
  local status=${1:-$?}
  for pid in "${pids[@]}"; do
    kill TERM $pid >/dev/null 2>&1
    kill -9 $pid >/dev/null 2>&1
  done
  exit $status
}

function set_cpu_cores() {
  if [[ -z "$CPU_CORES" ]]; then
    if [[ "Darwin" = "$(uname -s)" ]]; then
      CPU_CORES="$(sysctl -n hw.logicalcpu 2>/dev/null)"
      [[ -z "$CPU_CORES" ]] && CPU_CORES="$(sysctl -n hw.ncpu 2>/dev/null)"
      [[ -z "$CPU_CORES" ]] && CPU_CORES="$(sysctl -a 2>/dev/null | grep machdep.cpu.core_count | cut -f2 -d' ' | head -n 1)"
    else
      CPU_CORES="$(grep 'processor' /proc/cpuinfo 2>/dev/null | wc -l)"
    fi
  fi

  if [[ -z "$CPU_CORES" ]] || ! [[ "$CPU_CORES" =~ ^[0-9]+$ ]] || (( CPU_CORES < 1 )); then
    CPU_CORES=1
  fi

  echo $CPU_CORES
}

function host_os() {
  local host=""

  if [[ -n $1 ]]; then
    host=$1
  else
    host="$(uname -s)"
  fi

  if [[ "$host" = "Linux" ]]; then
    if [ -n "$WSL_DISTRO_NAME" ] || uname -r | grep 'Microsoft'; then
      write_log "h" "not ok - WSL is not supported."
      return 1
    fi
  elif [[ "$host" == *"MINGW64_NT"* ]]; then
    host="Win32"
  elif [[ "$host" == *"MSYS_NT"* ]]; then
    host="Win32"
  fi

  echo "$host"
}

function host_arch() {
  uname -m | sed 's/aarch64/arm64/g'
}

function use_bin_ext() {
  # binaries and scripts have extensions under Win32
  local os="$2"
  [[ -z "$os" ]] && os="$(host_os)"
  [[ "$os" == "Win32" ]] && echo "$1"
}

function build_env_data() {
  echo "CXX=\"$(escape_path "$(native_path "$CXX")")\""
  echo "ANDROID_HOME=\"$(escape_path "$ANDROID_HOME")\""
  echo "JAVA_HOME=\"$(escape_path "$JAVA_HOME")\""
  echo "ANDROID_SDK_MANAGER=\"$(escape_path "$ANDROID_SDK_MANAGER")\""
  echo "GRADLE_HOME=\"$(escape_path "$GRADLE_HOME")\""
  # Reserved for local SDK manager workarounds; current command-line tools do
  # not require removed Java EE modules.
  echo "ANDROID_SDK_MANAGER_JAVA_OPTS=\"\""
  echo "ANDROID_SDK_MANAGER_ACCEPT_LICENSES=\"$ANDROID_SDK_MANAGER_ACCEPT_LICENSES\""
}

function update_env_data() {
  read_env_data
  local vars=()
  local kvp
  local fail=0
  local base64
  while (( $# > 0 )); do
    declare arg="$1"; shift
    if [[ "$arg" == "--b64" ]]; then 
      base64=1
    else
      if [[ -n "$base64" ]]; then
        arg="$(echo "$arg" | base64 --decode)"
      fi
      IFS='=' read -ra kvp <<< "$arg"
      if [[ "${#kvp[@]}" != "2" ]]; then
        echo >&2 "Invalid key=pair: $arg"; fail=1
      else
        vars+=("${kvp[0]}=$(escape_path "${kvp[1]}")")
      fi
      base64=""
    fi
  done

  (( fail )) && return $fail

  for kvp in "${vars[@]}"; do
    write_log "d" "# eval \"$kvp\""
    eval "$kvp"
  done

  write_env_data
  return 0
}

function _resolve_env_file_for_read() {
  if [[ -f "$root/$ORO_ENV_FILENAME" ]]; then
    echo "$ORO_ENV_FILENAME"
    return
  fi

  echo "$ORO_ENV_FILENAME"
}

function read_env_data() {
  local file="$(_resolve_env_file_for_read)"
  if [[ -f "$root/$file" ]]; then
    # shellcheck disable=SC1090
    source "$root/$file"
  fi
}

function write_env_data() {
  # Maintain mtime on $ORO_ENV_FILENAME, only update if changed
  local temp=$(mktemp)
  local new_hash
  local old_hash
  build_env_data > "$temp"

  if [[ ! -f "$root/$ORO_ENV_FILENAME" ]]; then
    mv "$temp" "$root/$ORO_ENV_FILENAME"
  else
    old_hash=$(sha512sum "$root/$ORO_ENV_FILENAME")
    new_hash=$(sha512sum "$temp")

    if [[ "$old_hash" != "$new_hash" ]]; then
      mv "$temp" "$root/$ORO_ENV_FILENAME"
    else
      rm -f "$temp"
    fi
  fi
}

function prompt() {
  write_log "h" "$1"
  local return=$2
  local prompt_input
  # effectively stores $prompt_input in $2 by reference, rather than using echo to return which would prevent echo "$1" going to stdout
  read -rp '> ' prompt_input
  eval "$return=\"$prompt_input\""
  write_log "f" "prompt_input: $2"
}

function prompt_yn() {
  local r
  local return=$2

  # echo "prompt_yn, $PROMPT_DEFAULT_YN, $1"

  if [[ -n "$PROMPT_DEFAULT_YN" ]]; then
    write_log "h" "$1 [y/N]"
    write_log "h" "default: $PROMPT_DEFAULT_YN"
    r="$PROMPT_DEFAULT_YN"
  else
    prompt "$1 [y/N]" r
  fi

  if [[ "$(lower "$r")" == "y" ]]; then
    return 0
  fi
  return 1
}

function prompt_new_path() {
  local text="$1"
  local return=$3
  local exists_message=$4
  local input=""
  local unix_input=""

  if [ -n "$2" ]; then
    write_log "h" "$text"
    if prompt_yn "Use suggested default path: ""$2""?"; then
      input="$2"
    fi
  fi

  while true; do
    if [ -z "$input" ]; then
      prompt "$text (Press Enter to go back)" input
    fi
    # manually expand ~, doesn't happen in scripts
    input=${input//\~/$HOME}
    write_log "h" "Entered: $input"
    # remove any quote characters
    input=${input//\"/}
    unix_input="$(unix_path "$input")"
    if [ -e "$unix_input" ]; then
      if [ "$exists_message" == "!CAN_EXIST!" ]; then
        input="$(native_path "$(abs_path "$unix_input")")"
        eval "$return=$(escape_path "$input")"
        return 0
      fi

      unix_input="$(abs_path "$unix_input")"
      write_log "h" "# \"$input\" already exists, please choose a new path."
      if [ -n "$exists_message" ]; then
        write_log "h" "# $exists_message"
      fi
      input=""
    elif [ -z "$input" ]; then
      if prompt_yn "Cancel entering new path?"; then
        return
      fi
    else
      write_log "h" "ok - Create: $unix_input"
      if ! mkdir -p "$unix_input"; then
        write_log "h" "not ok - Create $unix_input failed."
        input=""
      else
        input="$(native_path "$(abs_path "$unix_input")")"
        eval "$return=$(escape_path "$input")"
        return 0
      fi
    fi
  done
}

function abs_path() {
  local test="$1"
  local basename=""
  if [ -f "$1" ]; then
    test="$(dirname "$1")"
    basename="/$(basename "$1")"
  elif [ ! -e "$1" ]; then
    return 1
  fi

  local p="$(sh -c "cd '$test'; pwd")$basename"
  # mingw sh returns incorrect escape slash if path contains spaces, swap / for \
  p="${p///\ /\\ }"
  echo "$p"
}

download_to_tmp() {
  local uri="$1"
  local expected_checksum="${2:-}"
  local tmp="$(mktemp -d)"
  local output="$tmp/$(basename "$uri")"

  if [ -n "${ORO_ANDROID_REPO:-}" ]; then
    echo >&2 cp "$ORO_ANDROID_REPO/$(basename "$uri")" "$output"
    if ! cp "$ORO_ANDROID_REPO/$(basename "$uri")" "$output"; then
      rm -rf "$tmp"
      return 1
    fi
  else
    if ! curl --fail --location --silent --show-error "$uri" --output "$output"; then
      rm -rf "$tmp"
      return 1
    fi
  fi

  if [ -n "$expected_checksum" ]; then
    local observed_checksum
    if [[ "$expected_checksum" =~ ^[0-9a-f]{64}$ ]]; then
      if command -v sha256sum >/dev/null 2>&1; then
        observed_checksum="$(sha256sum "$output" | awk '{print $1}')"
      elif command -v shasum >/dev/null 2>&1; then
        observed_checksum="$(shasum -a 256 "$output" | awk '{print $1}')"
      fi
    elif [[ "$expected_checksum" =~ ^[0-9a-f]{40}$ ]]; then
      if command -v sha1sum >/dev/null 2>&1; then
        observed_checksum="$(sha1sum "$output" | awk '{print $1}')"
      elif command -v shasum >/dev/null 2>&1; then
        observed_checksum="$(shasum -a 1 "$output" | awk '{print $1}')"
      fi
    else
      echo >&2 "not ok - expected checksum for $uri must be SHA-256 or SHA-1"
      rm -rf "$tmp"
      return 1
    fi

    if [ -z "${observed_checksum:-}" ]; then
      echo >&2 "not ok - no supported checksum utility is available to verify $uri"
      rm -rf "$tmp"
      return 1
    fi

    if [ "$observed_checksum" != "$expected_checksum" ]; then
      echo >&2 "not ok - checksum mismatch for $uri"
      rm -rf "$tmp"
      return 1
    fi
  fi

  echo "$output"
}

function unpack() {
  local archive=$1
  local dest=$2
  local command=""

  if [[ "$archive" == *".tar.gz" ]]; then
    command="tar -xf"
  elif [[ "$archive" == *".gz" ]]; then
    command="gzip -d";
  elif [[ "$archive" == *".bz2" ]]; then
    command="bzip2 -d"
  elif [[ "$archive" == *".zip" ]]; then
    command="unzip"
  fi

  if ! cd "$dest"; then
    return $?
  fi

  $command "$archive"
  return 0
}

function get_top_level_archive_dir() {
  local archive=$1
  local head=""

  if [[ "$archive" == *".tar.gz" ]] || [[ "$archive" == *".tgz" ]]; then
    tar -tf "$archive" | while read -r line; do
      for head in $(echo "$line" | tr "/" "\n"); do
        if [[ "$head" == "." ]]; then
          : continue
        else
          echo "$head"
          return
        fi
      done
    done
    return $?
  elif [[ "$archive" == *".gz" ]] || [[ "$archive" == *".bz2" ]]; then
    "$(basename "${archive%.*}")"
  elif [[ "$archive" == *".bz2" ]]; then
    "$(basename "${archive%.*}")"
  elif [[ "$archive" == *".zip" ]]; then
    head=$(unzip -Z1 "$archive" | head -n1)
    echo "${head//\//}" # remove trailing slash
  fi

  return $?
}

function lower() {
  echo "$1"|tr '[:upper:]' '[:lower:]'
}

function version() {
  echo "$@" | awk -F. '{ printf("%d%03d%03d%03d\n", $1,$2,$3,$4); }';
}

# Logging
_loghome=$HOME
if [ -n "$LOCALAPPDATA" ]; then
  _loghome="$LOCALAPPDATA"
fi

if [ -n "$ORO_INSTALL_SH_LOGFILE" ]; then
  logfile="$ORO_INSTALL_SH_LOGFILE"
else
  logfile="$_loghome/oro_install_sh.log"

  # Some environments (CI, sandboxes) restrict writes outside the project.
  # Fall back to a repo-local log file when HOME isn't writable.
  if ! ( : >> "$logfile" ) 2>/dev/null; then
    if [ -d "$root/audit" ]; then
      logfile="$root/audit/oro_install_sh.log"
    else
      logfile="$root/oro_install_sh.log"
    fi
    : >> "$logfile" 2>/dev/null || logfile=""
  fi
fi

function write_log() {
  local wh=""
  local type=$1
  shift
  # local message=$2

  if [[ -n "$DEBUG" ]] && ( [[ "$type" == "d" ]] || [[ "$type" == "v" ]] ); then
    wh="1"
  elif [[ -n "$VERBOSE" ]] && [[ "$type" == "v" ]]; then
    wh="1"
  elif [[ "$type" == "h" ]]; then
    wh="1"
  elif [[ "$type" == "f" ]]; then
    unset wh
  fi

  if [[ -n "$wh" ]]; then
    echo "$@"
  fi

  write_log_file "$@"
}

function write_log_file() {
  if [[ -z "$logfile" ]]; then
    return 0
  fi

  echo "$1" >> "$logfile" 2>/dev/null || true
}

function determine_package_manager () {
  local package_manager=""
  [[ -z "$package_manager" ]] && [[ "$(uname -s)" == "Darwin" ]] && command -v brew >/dev/null 2>&1 && package_manager="brew install"
  [[ -z "$package_manager" ]] && command -v apt >/dev/null 2>&1 && package_manager="apt install"
  [[ -z "$package_manager" ]] && command -v yum >/dev/null 2>&1 && package_manager="yum install"
  [[ -z "$package_manager" ]] && command -v pacman >/dev/null 2>&1 && package_manager="pacman -S"
  [[ -z "$package_manager" ]] && package_manager="<your package manager> install"
  echo "$package_manager"
}

function find_cxx () {
  local compiler=""
  local compiler_path=""

  for compiler in clang++ clang++-{26..14} g++; do
    compiler_path="$(command -v "$compiler" 2>/dev/null)" || continue

    if [[ ! -f "$compiler_path" ]] || [[ ! -x "$compiler_path" ]]; then
      continue
    fi

    # ROCm ships a private LLVM toolchain for GPU workloads. It must only be
    # used when the caller selects it explicitly through CXX.
    if [[ "$compiler_path" == /opt/rocm/* ]] || [[ "$compiler_path" == /opt/rocm-*/* ]]; then
      continue
    fi

    echo "$compiler_path"
    return 0
  done

  return 1
}

function determine_cxx () {
  local package_manager="$(determine_package_manager)"

  read_env_data

  if [ ! "$CXX" ]; then
    CXX="$(find_cxx)" || CXX=""
    if [[ -z "$CXX" ]]; then
      echo >&2 "not ok - missing build tools, try \"sudo $package_manager clang\""
      return 1
    fi

    if [ "$host" = "Win32" ]; then
      # POSIX doesn't handle quoted commands
      # Quotes inside variables don't escape spaces, quotes only help on the line we are executing
      # Make a temp link
      write_log "h" "Win32: linking clang to /tmp"
      CXX_TMP=$(mktemp)
      rm $CXX_TMP
      ln -s "$CXX" $CXX_TMP
      CXX=$CXX_TMP
      # Make tmp.etc look like clang++.etc, makes clang output look correct
      CXX=$(echo $CXX|sed 's/tmp\./clang++\./')
      mv $CXX_TMP $CXX
    fi

    if [ ! "$CXX" ]; then
      echo "not ok - could not determine \$CXX environment variable"
      return 1
    fi

    write_log "v" "warn - using '$CXX' as CXX"
  fi

  CC="${CXX/++/}"

  export CXX
  export CC
  # win32 has partically escaping requirements, this is handled by install.ps1
  if [[ "$host" != "Win32" ]]; then
    update_env_data "CXX=$CXX"
    update_env_data "CC=$CC"
  fi
}

function linux_build_dependencies_available() {
  local cxx="${CXX:-}"
  local dependency=""
  local missing=()
  local pkg_config_modules=(
    dbus-1
    gio-unix-2.0
    gtk+-3.0
    libsoup-3.0
    libsystemd
    webkit2gtk-4.1
  )

  if ! command -v pkg-config >/dev/null 2>&1; then
    missing+=("pkg-config")
  fi

  if [[ -z "$cxx" ]]; then
    cxx="$(find_cxx)" || cxx=""
  fi

  if [[ -z "$cxx" ]]; then
    missing+=("C++ compiler")
  elif [[ "$cxx" == */* ]]; then
    if [[ ! -f "$cxx" ]] || [[ ! -x "$cxx" ]]; then
      missing+=("C++ compiler ($cxx)")
    elif ! printf '#include <string>\nint main() { std::string value; return value.size(); }\n' |
      "$cxx" -std=c++20 -x c++ - -o /dev/null >/dev/null 2>&1; then
      missing+=("working C++20 compiler and linker ($cxx)")
    fi
  elif ! command -v "$cxx" >/dev/null 2>&1; then
    missing+=("C++ compiler ($cxx)")
  elif ! printf '#include <string>\nint main() { std::string value; return value.size(); }\n' |
    "$cxx" -std=c++20 -x c++ - -o /dev/null >/dev/null 2>&1; then
    missing+=("working C++20 compiler and linker ($cxx)")
  fi

  if command -v pkg-config >/dev/null 2>&1; then
    for dependency in "${pkg_config_modules[@]}"; do
      if ! pkg-config --exists "$dependency" 2>/dev/null; then
        missing+=("pkg-config:$dependency")
      fi
    done
  fi

  if (( ${#missing[@]} > 0 )); then
    write_log "h" "Missing Linux build dependencies: ${missing[*]}"
    return 1
  fi

  if [[ -z "${CXX:-}" ]]; then
    CXX="$cxx"
  fi

  return 0
}

function first_time_experience_setup() {
  export BUILD_ANDROID="1"
  local target="$1"

  if [[ -n "$NO_ANDROID" ]]; then
    unset BUILD_ANDROID
  fi

  if [ -z "$target" ] || [[ "$target" == "linux" ]]; then
    if [[ "$(host_os)" == "Linux" ]]; then
      local package_manager="$(determine_package_manager)"
      if linux_build_dependencies_available; then
        write_log "v" "ok - Linux build dependencies are already installed."
      else
        echo "Installing $(host_os) dependencies..."

        if [[ "$package_manager" == "apt install" ]]; then
          log_and_run sudo apt update || return $?
          log_and_run sudo apt install -y   \
            build-essential                 \
            clang-14                        \
            git                             \
            libgtk-3-dev                    \
            libwebkit2gtk-4.1-dev           \
            libc++abi-14-dev                \
            libc++-14-dev                   \
            libdbus-1-dev                   \
            libsoup-3.0-dev                 \
            libsystemd-dev                  \
            pkg-config                      \
            || return $?
        elif [[ "$package_manager" == "pacman -S" ]]; then
          log_and_run sudo pacman -Syu      \
            clang-14                        \
            base-devel                      \
            git                             \
            libc++1-14                      \
            libc++abi-14                    \
            libdbus                         \
            libsystemd                      \
            pkgconf                         \
            webkit2gtk-4.1                  \
            || return $?
        elif [[ "$package_manager" == "dnf install" ]]; then
          log_and_run sudo dnf install      \
            automake                        \
            clang14-devel                   \
            clang14-libs                    \
            dbus-devel                      \
            gcc                             \
            gcc-c++                         \
            kernel-devel                    \
            libcxx-devel                    \
            libcxxabi-devel                 \
            libsystemd-devel                \
            make                            \
            webkit2gtk4.1-devel             \
            || return $?
        elif [[ "$package_manager" == "yum install" ]]; then
          echo "warn - yum package manager is not suppored yet. Please try to install dependencies manually."
          exit 1
        fi
      fi
    fi
  fi

  determine_cxx || return $?

  if [[ "$target" == "android" ]]; then
    ## Android is not supported on linux-arm64, return early
    if [[ "$(host_os)" == "Linux" ]] && [[ "$(host_arch)" == "arm64"  ]]; then
      return 0
    fi

    "$root/bin/android-functions.sh" --android-fte || return $?
  fi
}

function main() {
  while (( $# > 0 )); do
    declare arg="$1"; shift
    case "$arg" in
      --fte)
        first_time_experience_setup "$@"
        return $?
        ;;
      --update-env-data)
        update_env_data "$@"
        return $?
        ;;
      *)
        echo >&2 "not ok - unknown functions.sh argument: $arg"
        return 64
        ;;
    esac
  done
  return 0
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  main "$@"
  exit $?
fi
