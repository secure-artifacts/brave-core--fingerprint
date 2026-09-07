@echo off
REM Local development environment for this Brave Core checkout.
REM Usage from Command Prompt:
REM   cd /d C:\Users\tomgr\Desktop\brave-core--fingerprint
REM   dev-env.cmd
REM   pnpm run help
REM
REM Brave build scripts expect C:\Users\tomgr\src\brave; that path is a junction
REM pointing back to this Desktop checkout.

set "BRAVE_CORE_ROOT=%~dp0"
set "BRAVE_RUN_DIR=C:\Users\tomgr\src\brave"
set "BRAVE_NODE_DIR=%BRAVE_CORE_ROOT%.devtools\node-v24.16.0-win-x64"
set "PATH=%BRAVE_NODE_DIR%;%PATH%"
set "vs2022_install=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "PYTHONPATH="
doskey pnpm=cd /d "%BRAVE_RUN_DIR%" $T "%BRAVE_NODE_DIR%\node.exe" "%BRAVE_NODE_DIR%\node_modules\pnpm\bin\pnpm.cjs" $*
doskey brave-cd=cd /d "%BRAVE_RUN_DIR%"
node --version
"%BRAVE_NODE_DIR%\node.exe" "%BRAVE_NODE_DIR%\node_modules\pnpm\bin\pnpm.cjs" --version
