# build.ps1 — build all c-upnp-igd targets on Windows (MinGW/gcc)
# Run from repo root: .\build.ps1

$ErrorActionPreference = "Stop"

$CC      = "gcc"
$CFLAGS  = @("-O2", "-Wall", "-Wno-format-truncation", "-Wno-stringop-truncation")
$LIBS    = @("-lws2_32", "-liphlpapi")
$INC     = "-I."
$SRCS    = "upnp.c"

$targets = @(
    @{ out = "example/map_port.exe"; src = "example/map_port.c" },
    @{ out = "test/test_upnp.exe";   src = "test/test_upnp.c"   },
    @{ out = "test/test_live.exe";   src = "test/test_live.c"   }
)

foreach ($t in $targets) {
    $args = $CFLAGS + $INC + "-o" + $t.out + $t.src + $SRCS + $LIBS
    Write-Host "  building $($t.out)..."
    & $CC @args 2>&1 | Where-Object { $_ -notmatch "pragma" }
    if ($LASTEXITCODE -ne 0) {
        Write-Error "FAILED: $($t.out)"
    }
}

Write-Host ""
Write-Host "Done. Binaries:"
Get-ChildItem example/*.exe, test/*.exe | Format-Table Name, LastWriteTime, Length -AutoSize
