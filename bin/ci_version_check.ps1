$runtimeHome = $null
$appDataBase = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { "$env:USERPROFILE\AppData\Local" }
$defaultOroHome = "$appDataBase\Programs\oro"

if ($env:ORO_HOME -and $env:ORO_HOME.Trim() -ne "") {
  $runtimeHome = $env:ORO_HOME
} else {
  $runtimeHome = $defaultOroHome
}

$BIN_PATH = "$runtimeHome\bin"

if ($env:Path -notlike "*$BIN_PATH*") {
  $new_path = "$BIN_PATH;$env:Path"
  $env:Path = $new_path
}

$cliBin = if ($env:ORO_CLI_BIN -and $env:ORO_CLI_BIN.Trim() -ne "") { $env:ORO_CLI_BIN } else { "oroc" }
if (-not (Get-Command $cliBin -ErrorAction SilentlyContinue)) {
  Write-Output "Version check failed: 'oroc' is not available on PATH"
  Exit 1
}

$VERSION_CLI = & $cliBin -v 2>&1 | Select-Object -First 1 | % ToString
$VERSION_TXT = $(type VERSION.txt) 2>&1 | % ToString
$VERSION_GIT = $(git rev-parse --short=8 HEAD) 2>&1 | % ToString
$VERSION_EXPECTED = "$VERSION_TXT ($VERSION_GIT)"

if ($VERSION_CLI -eq $VERSION_EXPECTED) {
  Write-Output "Version check has passed"
} else {
  Write-Output "Version check has failed"
  Write-Output "Expected: $VERSION_EXPECTED"
  Write-Output "Got: $VERSION_CLI"
  Exit 1
}

$BASE_LIST = $VERSION_TXT -split '\.'

$V_MAJOR = $BASE_LIST[0]
$V_MINOR = $BASE_LIST[1]

function Assert-VersionMatch {
    param(
        [string] $Package,
        [string] $Observed
    )

    if ($Observed -ne $VERSION_TXT) {
        Write-Output "Version of $Package is not in sync with Oro Runtime"
        Write-Output "Expected: $VERSION_TXT"
        Write-Output "$Package version is $Observed"
        exit 1
    }
}

$VERSION_NODE_PRIMARY = npm show ./npm/packages/@orocomputer/runtime-node version

Assert-VersionMatch "@orocomputer/runtime-node" $VERSION_NODE_PRIMARY
