# Windows Setup and Board Flashing

Short recovery and flashing guide for Autotrainer firmware 1.2.5 on Windows PowerShell.

Validated with Zephyr 3.7.0, Zephyr SDK 0.16.8, Python 3.11, West 1.5.0, pyOCD 0.45.1, and STM32G474RETx pellet and magnet boards.

> **Scope.** This is the native-Windows path. The supported Cerebellum Lab workflow
> (`tools/reachaq-firmware`, `tools/flash_pellet_module.sh`) is bash-only - see
> [Pellet firmware release and deployment](../docs/pellet-firmware-release-and-deployment.md).
> These steps were validated end to end against tag `v1.2.5`. Later tags use the same
> Zephyr SDK 0.16.8 but have not been re-validated on Windows.

## 1. One-time host installation

Use a normal, non-administrator PowerShell. This avoids the Chocolatey permission and lock errors encountered under `C:\ProgramData\chocolatey`.

```powershell
$WingetOptions = @(
    "--exact",
    "--scope", "user",
    "--accept-package-agreements",
    "--accept-source-agreements"
)

winget install --id Git.Git @WingetOptions
winget install --id Python.Python.3.11 @WingetOptions
winget install --id Kitware.CMake --version 4.4.2 @WingetOptions
winget install --id Ninja-build.Ninja --version 1.13.2 @WingetOptions
winget install --id oss-winget.gperf --version 3.1 @WingetOptions
winget install --id oss-winget.dtc --version 1.6.1 @WingetOptions
```

Close PowerShell, open a new one, and check the installations:

```powershell
git --version
py -3.11 --version
cmake --version
ninja --version
gperf --version
dtc --version
```

## 2. Workspace and Python environment

Set the reusable paths:

```powershell
$WorkspaceRoot = Join-Path $env:USERPROFILE "Documents\reachAQ-hardware-v1.2.5"
$VenvRoot = Join-Path $env:USERPROFILE ".venvs\reachaq-hardware-v1.2.5"
$SdkRoot = Join-Path $env:USERPROFILE "zephyr-sdk-0.16.8"
```

Clone the tagged release. Skip `git clone` if `$WorkspaceRoot` already exists.

```powershell
git clone --branch v1.2.5 --single-branch `
    https://github.com/Cerebellum-Lab/reachAQ-hardware.git `
    $WorkspaceRoot

cd $WorkspaceRoot
git describe --tags --exact-match HEAD
```

The last command must print `v1.2.5`.

Create and activate the dedicated environment:

```powershell
New-Item -ItemType Directory -Force -Path (Split-Path $VenvRoot -Parent) | Out-Null
py -3.11 -m venv $VenvRoot
& "$VenvRoot\Scripts\Activate.ps1"

python -m pip install --upgrade pip setuptools wheel
python -m pip install west==1.5.0 py7zr==1.1.3
```

If activation is blocked:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
& "$VenvRoot\Scripts\Activate.ps1"
```

Initialize West and install the repository requirements:

```powershell
cd $WorkspaceRoot

if (-not (Test-Path "$WorkspaceRoot\.west")) {
    west init -l firmware
}

west update
python -m pip install -r .\deps\zephyr\scripts\requirements.txt
python -m pip install -r .\firmware\requirements.txt
python -m pip install pyocd==0.45.1

west zephyr-export
west config build.sysbuild true
```

Create the Windows `python3` command required by the v1.2.5 CMake files:

```powershell
$PythonExe = "$VenvRoot\Scripts\python.exe"
$Python3Exe = "$VenvRoot\Scripts\python3.exe"

if (-not (Test-Path $Python3Exe)) {
    New-Item -ItemType HardLink -Path $Python3Exe -Target $PythonExe | Out-Null
}

python3 --version
```

## 3. Zephyr SDK 0.16.8

This Zephyr 3.7 workspace does not provide `west sdk install`. Install the minimal SDK and ARM toolchain directly:

```powershell
$SdkDownloadRoot = Join-Path $env:TEMP "reachaq-zephyr-sdk-0.16.8"
$SdkBundle = Join-Path $SdkDownloadRoot "zephyr-sdk-0.16.8_windows-x86_64_minimal.7z"
$ArmToolchain = Join-Path $SdkDownloadRoot "toolchain_windows-x86_64_arm-zephyr-eabi.7z"

New-Item -ItemType Directory -Force -Path $SdkDownloadRoot | Out-Null
New-Item -ItemType Directory -Force -Path $SdkRoot | Out-Null

Invoke-WebRequest `
    -Uri "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_windows-x86_64_minimal.7z" `
    -OutFile $SdkBundle

Invoke-WebRequest `
    -Uri "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/toolchain_windows-x86_64_arm-zephyr-eabi.7z" `
    -OutFile $ArmToolchain

python -m py7zr x $SdkBundle $SdkRoot
python -m py7zr x $ArmToolchain $SdkRoot
cmake -P "$SdkRoot\cmake\zephyr_sdk_export.cmake"
```

Install and check STM32G4 flashing support:

```powershell
pyocd pack install stm32g4
pyocd list --targets | Select-String stm32g474retx
```

## 4. Start a new session

After the one-time setup, each new PowerShell needs only:

```powershell
$WorkspaceRoot = Join-Path $env:USERPROFILE "Documents\reachAQ-hardware-v1.2.5"
$VenvRoot = Join-Path $env:USERPROFILE ".venvs\reachaq-hardware-v1.2.5"
$SdkRoot = Join-Path $env:USERPROFILE "zephyr-sdk-0.16.8"

& "$VenvRoot\Scripts\Activate.ps1"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkRoot
```

The prompt should include `(reachaq-hardware-v1.2.5)`. A remaining Conda `(base)` indicator is acceptable.

## 5. Build firmware once

Pellet:

```powershell
cd "$WorkspaceRoot\firmware\pellet_module"
west build -p always --sysbuild -b cerebellumlab_pellet_module
```

Magnet:

```powershell
cd "$WorkspaceRoot\firmware\magnet_module"
west build -p always --sysbuild -b cerebellumlab_magnet_module
```

Signed applications:

```text
firmware\pellet_module\build\pellet_module\zephyr\zephyr.signed.hex
firmware\magnet_module\build\magnet_module\zephyr\zephyr.signed.hex
```

Nonfatal `No SOURCES given to Zephyr library` warnings can be ignored when the build completes successfully.

## 6. Connect the programmer

1. Plug the TC2070-NL into the STLINK-V3MINIE.
2. Seat the pogo pins squarely on the board footprint and attach GRIP-14.
3. Connect the STLINK USB cable.
4. Apply normal power to the target board. The programmer does not supply target power through `T_VCC`.
5. Check detection:

```powershell
pyocd list
```

`STLINK-V3` must appear. Its displayed target may remain `n/a`; the West runner supplies `stm32g474retx`.

## 7. Flash brand-new boards

Keep each board powered and the GRIP-14 seated through both commands.

### Pellet board

```powershell
cd "$WorkspaceRoot\firmware\pellet_module"
west flash -d .\build\mcuboot -r pyocd
west flash -d .\build\pellet_module -r pyocd
```

Power off and disconnect the pellet board before connecting the magnet board.

### Magnet board

```powershell
cd "$WorkspaceRoot\firmware\magnet_module"
west flash -d .\build\mcuboot -r pyocd
west flash -d .\build\magnet_module -r pyocd
```

Successful results for both boards:

```text
MCUboot:            0x08000000
Signed application: 0x08010000
Erasing:            100%
Programming:        100%
```

After both commands succeed, power the board off, remove the programmer, and power-cycle the board.

## 8. Debugging tips

| Error or symptom | Fix |
| --- | --- |
| Chocolatey access denied or lock error | Use the user-scope `winget` commands in Section 1. |
| `CMake is not installed or cannot be found` | Open a new PowerShell, then run `Get-Command cmake` and `cmake --version`. |
| `west: unknown command "packages"` | Use both `pip install -r` commands in Section 2. |
| `west: unknown command "sdk"` | Install SDK 0.16.8 manually using Section 3. |
| Microsoft Store opens for `python3` | Recreate `$VenvRoot\Scripts\python3.exe` using the hard-link commands in Section 2. |
| `pyocd list` shows no probe | Check STLINK USB and its USB data cable. |
| `Get IDCODE error` | Power the board and reseat the TC2070-NL/GRIP-14. |
| `no runners.yaml found in ...\build\zephyr` | Use the explicit child paths shown in Section 7, not plain `west flash`. |
| More than one STLINK is connected | Add `--dev-id <probe-unique-id>` to each `west flash` command. |
| Build directory came from another PC | Run the matching pristine `west build -p always --sysbuild -b ...` command from Section 5. |

## References

- [Zephyr 3.7.0 Getting Started Guide](https://docs.zephyrproject.org/3.7.0/develop/getting_started/index.html)
- [Zephyr SDK 0.16.8 downloads](https://github.com/zephyrproject-rtos/sdk-ng/releases/tag/v0.16.8)
- [STLINK-V3MINIE user manual](https://www.st.com/resource/en/user_manual/um2910-stlinkv3minie-debuggerprogrammer-tiny-probe-for-stm32-microcontrollers-stmicroelectronics.pdf)
