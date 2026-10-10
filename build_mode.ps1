# Windows PowerShell version of build_mode.sh.
# Build (and optionally flash) one app mode in its own build folder.
#   .\build_mode.ps1 hwtest            build only
#   .\build_mode.ps1 walkie COM5       build, flash and open the serial monitor
# Modes: hwtest, loopback, radiotest, walkie
# Run it from an ESP-IDF PowerShell window (where idf.py works).
param(
    [Parameter(Mandatory = $true)][ValidateSet("hwtest", "loopback", "radiotest", "walkie")][string]$Mode,
    [string]$Port = ""
)
$ErrorActionPreference = "Stop"
$build = "build_$Mode"
$idfArgs = @("-B", $build, "-D", "SDKCONFIG=$build/sdkconfig", "-D", "SDKCONFIG_DEFAULTS=sdkconfig.defaults;modes/$Mode.cfg")
if ($Port -ne "") {
    idf.py @idfArgs -p $Port build flash monitor
} else {
    idf.py @idfArgs build
}
