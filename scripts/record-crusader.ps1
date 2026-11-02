param(
    [string]$GameExe = "${PSScriptRoot}\..\build\crusader.exe",
    [string[]]$GameArgs = @("tools/release", "--new-game", "--skip-movies", "--keyboard-mouse"),
    [string]$OutputPath = "${PSScriptRoot}\..\recordings\crusader-recording.mp4",
    [string]$Python = 'D:\development\miniconda3\python.exe',
    [double]$TrimStartSeconds = 5,
    [double]$TrimSeconds = 5
)

$ErrorActionPreference = 'Stop'
$inv = [System.Globalization.CultureInfo]::InvariantCulture

$ffmpeg = 'C:\Users\ddsan\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.WinGet.Source_8wekyb3d8bbwe\ffmpeg-9.0.2-full_build\bin\ffmpeg.exe'
$ffprobe = Join-Path (Split-Path $ffmpeg) 'ffprobe.exe'
$audioScript = Join-Path $PSScriptRoot 'record_loopback.py'

if (-not (Test-Path $ffmpeg)) { throw "FFmpeg not found at: $ffmpeg" }
if (-not (Test-Path $GameExe)) { throw "Game executable not found at: $GameExe" }

$recordingsDir = [System.IO.Path]::GetFullPath((Split-Path $OutputPath -Parent))
New-Item -ItemType Directory -Path $recordingsDir -Force | Out-Null
$OutputPath = Join-Path $recordingsDir (Split-Path $OutputPath -Leaf)

$videoTmp = Join-Path $recordingsDir 'tmp-video.mkv'
$audioTmp = Join-Path $recordingsDir 'tmp-audio.wav'
$audioStartFile = Join-Path $recordingsDir 'tmp-audio-start.txt'
foreach ($f in @($videoTmp, $audioTmp, $audioStartFile, $OutputPath)) {
    if (Test-Path $f) { Remove-Item $f -Force }
}

function Start-Piped([string]$file, [string]$arguments) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $file
    $psi.Arguments = $arguments
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    return [System.Diagnostics.Process]::Start($psi)
}

# Stops a process that quits when it reads a line on stdin ('q' for ffmpeg, any line for the audio script).
function Stop-Piped($proc, [string]$what) {
    if ($null -eq $proc -or $proc.HasExited) { return }
    $proc.StandardInput.WriteLine('q')
    $proc.StandardInput.Close()
    if (-not $proc.WaitForExit(30000)) {
        Write-Warning "$what did not stop in time; killing it."
        $proc.Kill()
    }
}

Write-Host "Launching game..."
$gameProcess = Start-Process -FilePath $GameExe -ArgumentList $GameArgs -PassThru -NoNewWindow

# Audio: system output (game sound) via WASAPI loopback, written to a WAV.
$audioProcess = $null
$haveAudio = $false
if ((Test-Path $Python) -and (Test-Path $audioScript)) {
    $audioProcess = Start-Piped $Python "-u `"$audioScript`" `"$audioTmp`" `"$audioStartFile`""
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Test-Path $audioStartFile) -and -not $audioProcess.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    $haveAudio = Test-Path $audioStartFile
    if (-not $haveAudio) {
        Write-Warning "Audio capture did not start; recording video only."
        Stop-Piped $audioProcess 'Audio capture'
    }
}
else {
    Write-Warning "Python or record_loopback.py not found; recording video only."
}

# Video: screen with wall-clock timestamps preserved (-copyts) so it can be aligned with the audio.
Write-Host "Starting screen capture..."
$ffmpegArgs = '-hide_banner -loglevel error -f gdigrab -framerate 30 -i desktop ' +
    '-c:v libx264 -preset ultrafast -pix_fmt yuv420p -copyts -y "' + $videoTmp + '"'
$videoProcess = Start-Piped $ffmpeg $ffmpegArgs

Write-Host "Recording until the game exits..."
Wait-Process -InputObject $gameProcess

Write-Host "Game closed. Stopping capture..."
Stop-Piped $videoProcess 'FFmpeg'
if ($haveAudio) { Stop-Piped $audioProcess 'Audio capture' }

if (-not (Test-Path $videoTmp)) { throw "No video was recorded." }

function Get-Probe([string]$path, [string]$entry) {
    $text = & $ffprobe -v error -show_entries "format=$entry" -of default=noprint_wrappers=1:nokey=1 $path 2>$null |
        Select-Object -First 1
    $value = 0.0
    if (-not [double]::TryParse($text, [System.Globalization.NumberStyles]::Float, $inv, [ref]$value)) {
        throw "Could not read $entry from $path"
    }
    return $value
}

$videoStart = Get-Probe $videoTmp 'start_time'
$videoDuration = Get-Probe $videoTmp 'duration'
$outDuration = $videoDuration - $TrimStartSeconds - $TrimSeconds
if ($outDuration -le 0) { throw "Recording is only $([math]::Round($videoDuration, 1)) s; too short to trim." }

$inputs = @('-ss', $TrimStartSeconds.ToString($inv), '-i', $videoTmp)
$maps = @('-map', '0:v:0')
$codecs = @('-c:v', 'libx264', '-preset', 'veryfast', '-crf', '20', '-pix_fmt', 'yuv420p', '-vf', 'fps=30')

if ($haveAudio -and (Test-Path $audioTmp)) {
    # Both clocks are epoch seconds: where the trimmed video starts, measured on the audio timeline.
    $audioStart = [double]::Parse((Get-Content $audioStartFile -Raw).Trim(), $inv)
    $audioSeek = ($videoStart + $TrimStartSeconds) - $audioStart
    Write-Host ("Audio/video offset: {0:N3} s" -f $audioSeek)
    if ($audioSeek -ge 0) {
        $inputs += @('-ss', $audioSeek.ToString($inv), '-i', $audioTmp)
    }
    else {
        $inputs += @('-itsoffset', (-$audioSeek).ToString($inv), '-i', $audioTmp)
    }
    $maps += @('-map', '1:a:0')
    $codecs += @('-c:a', 'aac', '-b:a', '192k')
}

& $ffmpeg -hide_banner -loglevel error -y @inputs @maps @codecs -t $outDuration.ToString($inv) -movflags +faststart $OutputPath
if ($LASTEXITCODE -ne 0) { throw "Final FFmpeg encode failed (exit code $LASTEXITCODE)." }

foreach ($f in @($videoTmp, $audioTmp, $audioStartFile)) {
    if (Test-Path $f) { Remove-Item $f -Force }
}

Write-Host "Recording saved to: $OutputPath"
