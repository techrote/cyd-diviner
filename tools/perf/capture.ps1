param(
    [string]$Python = "python",
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$CaptureArguments
)
$ErrorActionPreference = "Stop"
& $Python (Join-Path $PSScriptRoot "capture.py") @CaptureArguments
exit $LASTEXITCODE
