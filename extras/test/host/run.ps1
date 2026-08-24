# Builds and runs the host-side SmartMatrix unit tests.
# Requires clang++ (any C++17 compiler works; adjust $clang if yours differs).
$ErrorActionPreference = "Stop"
$clang = "C:\Program Files\LLVM\bin\clang++.exe"
$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$src   = Join-Path $here "..\..\..\src"
$out   = Join-Path $here "test_bitplane.exe"

& $clang -std=c++17 -Wall -Wextra -I $src (Join-Path $here "test_bitplane.cpp") -o $out
if ($LASTEXITCODE -ne 0) { Write-Error "compile failed"; exit 1 }
& $out
exit $LASTEXITCODE
