<#
.SYNOPSIS
Runs one L2OFF server in the current console window.

.DESCRIPTION
The server output is shown in the window and appended to
build\windows-msvc\Release\<Name>\console.log (UTF-8).

With -Supervise, the server is started again every time it exits. This is
used for L2NPC: it ends with exit code 3 when L2Server (or CacheD) closes its
connection, and must be started again once L2Server listens on its NPC port.
Press Ctrl+C in the window to stop the server and the supervision; the
window waits until the server has shut down. stop-all.ps1 does the same.
#>
param(
    [Parameter(Mandatory)]
    [ValidateSet('AuthD', 'AuthGateD', 'CacheD', 'L2Server', 'L2NPC')]
    [string] $Name,

    # Start only once this local TCP port is listening (L2NPC: L2Server's NPC port).
    [int] $WaitForPort = 0,

    [switch] $Supervise
)

$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root "build\windows-msvc\Release\$Name"
$exe = Join-Path $bin "$Name.exe"
$consoleLog = Join-Path $bin 'console.log'

if (-not (Test-Path $exe)) {
    Write-Host "Executável não encontrado: $exe. Compile o projeto primeiro." -ForegroundColor Red
    return
}

# Game data: scripts\data-set.txt holds "2018" (script, html_USA and
# html_KOREA in the repo root, GeoData) or "gf" (script_gf, html_gf and
# GeoData_gf, built by tools\gfport\build_script_gf.py). Without the file,
# 2018 is used.
$dataSet = '2018'
$dataSetFile = Join-Path $PSScriptRoot 'data-set.txt'
if (Test-Path $dataSetFile) { $dataSet = "$(Get-Content $dataSetFile -TotalCount 1)".Trim() }
switch ($dataSet) {
    '2018' { $scriptsDir = Join-Path $root 'script'; $htmlDir = $root; $geoDir = Join-Path $root 'GeoData' }
    'gf'   { $scriptsDir = Join-Path $root 'script_gf'; $htmlDir = Join-Path $root 'html_gf'; $geoDir = Join-Path $root 'GeoData_gf' }
    default {
        Write-Host "Conjunto de dados desconhecido em ${dataSetFile}: '$dataSet' (use 2018 ou gf)." -ForegroundColor Red
        return
    }
}
if ($Name -in 'CacheD', 'L2Server', 'L2NPC') {
    Write-Host "Dados: $dataSet ($scriptsDir)" -ForegroundColor Cyan
}

$serverArgs = @('--config-dir', (Join-Path $root "$Name\etc"))
switch ($Name) {
    'CacheD'   { $serverArgs += @('--scripts-dir', $scriptsDir) }
    'L2Server' { $serverArgs += @('--scripts-dir', $scriptsDir, '--html-dir', $htmlDir, '--geo-dir', $geoDir) }
    'L2NPC'    { $serverArgs += @('--scripts-dir', $scriptsDir, '--geo-dir', $geoDir) }
}

# A server that exits this many times in a row, each within a minute, is
# not started again: something is wrong and a restart loop would only hide it.
$maxQuickExits = 5
$quickExitSeconds = 60
$restartDelaySeconds = 10

function Set-Title([string] $State) {
    $host.UI.RawUI.WindowTitle = "$Name - $State"
}

function Test-Listening([int] $Port) {
    [bool](Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
}

function Write-Status([string] $Message, [ConsoleColor] $Color = 'Cyan') {
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $Message" -ForegroundColor $Color
}

# Copies the server output to the window and to console.log while it runs.
# This is one .NET call on purpose: on Ctrl+C, a PowerShell pipeline kills a
# native command at once, but a running .NET call is never interrupted. The
# server receives the same Ctrl+C, shuts down cleanly, and PowerShell stops
# only after the call returns. The server keeps the console input, so its
# console commands still work.
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;

public static class L2ServerRunner {
    public static int Run(string name, string exe, string arguments, string workingDirectory, string logPath) {
        var info = new ProcessStartInfo(exe, arguments) {
            UseShellExecute = false,
            WorkingDirectory = workingDirectory,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = new UTF8Encoding(false),
            StandardErrorEncoding = new UTF8Encoding(false),
        };
        using (var log = new StreamWriter(logPath, true, new UTF8Encoding(false)))
        {
            log.AutoFlush = true;
            log.WriteLine("===== {0:yyyy-MM-dd HH:mm:ss} starting {1} =====", DateTime.Now, name);
            using (var process = Process.Start(info))
            {
                var gate = new object();
                Action<StreamReader> copy = reader => {
                    string line;
                    while ((line = reader.ReadLine()) != null) {
                        lock (gate) {
                            Console.WriteLine(line);
                            log.WriteLine(line);
                        }
                    }
                };
                var errors = new Thread(() => copy(process.StandardError)) { IsBackground = true };
                errors.Start();
                copy(process.StandardOutput);
                errors.Join();
                process.WaitForExit();
                log.WriteLine("===== {0:yyyy-MM-dd HH:mm:ss} {1} exited with code {2} =====", DateTime.Now, name, process.ExitCode);
                return process.ExitCode;
            }
        }
    }
}
'@

# Runs the server until it exits and returns its exit code.
function Invoke-Server {
    Set-Title 'rodando (fechar esta janela derruba o servidor)'
    $arguments = ($serverArgs | ForEach-Object { "`"$_`"" }) -join ' '
    [L2ServerRunner]::Run($Name, $exe, $arguments, $bin, $consoleLog)
}

function Wait-ForPort {
    if ($WaitForPort -le 0 -or (Test-Listening $WaitForPort)) { return }
    Set-Title "aguardando a porta $WaitForPort"
    Write-Status "Aguardando a porta $WaitForPort ficar disponível (L2Server iniciado)..."
    while (-not (Test-Listening $WaitForPort)) { Start-Sleep -Seconds 2 }
}

function Wait-ForOtherInstance {
    $other = Get-Process -Name $Name -ErrorAction SilentlyContinue
    if (-not $other) { return }
    Set-Title 'aguardando a instância atual terminar'
    Write-Status "$Name já está rodando (PID $($other.Id -join ', ')). A supervisão começa quando ele terminar."
    $other | Wait-Process
}

try {
    if (-not $Supervise) {
        Wait-ForPort
        $code = Invoke-Server
        Write-Status "$Name terminou (código $code)." Yellow
        return
    }

    $quickExits = 0
    while ($true) {
        Wait-ForOtherInstance
        Wait-ForPort
        $startedAt = Get-Date
        $code = Invoke-Server
        $ranFor = (Get-Date) - $startedAt

        if ($code -eq 3) {
            Write-Status "$Name perdeu a conexão com o L2Server ou o CacheD (código 3)." Yellow
        } else {
            Write-Status "$Name terminou com o código $code." Yellow
        }

        if ($ranFor.TotalSeconds -lt $quickExitSeconds) { $quickExits++ } else { $quickExits = 0 }
        if ($quickExits -ge $maxQuickExits) {
            Write-Status "$Name terminou $quickExits vezes seguidas em menos de $quickExitSeconds s. O reinício automático foi suspenso; veja $consoleLog." Red
            return
        }

        Set-Title "reiniciando em $restartDelaySeconds s"
        Write-Status "Reiniciando $Name em $restartDelaySeconds s (Ctrl+C cancela)..."
        Start-Sleep -Seconds $restartDelaySeconds
    }
} finally {
    Set-Title 'PARADO (pode fechar esta janela)'
}
