Run with Split Logs
`cd build-ninja\frontends\windows && .\neosurf-windows.exe -split-logs`

Run with Split Logs and Custom URL
`cd build-ninja\frontends\windows && .\neosurf-windows.exe -split-logs https://google.com`

Log Files Location
`build-ninja\frontends\windows\neosurf-logs\`

Log files are `.txt` with different severity levels:
- `ns-error.txt` - Errors only
- `ns-warning.txt` - Warnings and above
- `ns-info.txt` - Info and above
- `ns-debug.txt` - Debug and above (only in debug builds)
- `ns-deepdebug.txt` - Deepdebug and above (only in debug builds)
