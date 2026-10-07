param(
    [Parameter(Mandatory = $true)]
    [string]$PluginRoot
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$artwork = Join-Path $PluginRoot 'assets\artwork'
$output = Join-Path $PluginRoot 'assets\native'
$config = Get-Content -LiteralPath (Join-Path $PluginRoot 'config\players-hud-slider.toml') -Raw
$nativePanelScale = 1.6
New-Item -ItemType Directory -Path $output -Force | Out-Null

function Get-ElementInt([string]$Section, [string]$Name, [int]$Default) {
    $sectionPattern = '(?ms)^\[hud-player-slider\.' +
        [regex]::Escape($Section) + '\]\s*(.*?)(?=^\[|\z)'
    $sectionMatch = [regex]::Match($config, $sectionPattern)
    if (!$sectionMatch.Success) { return $Default }
    $match = [regex]::Match($sectionMatch.Groups[1].Value,
        "(?m)^$([regex]::Escape($Name))\s*=\s*(-?\d+)\s*$")
    if (!$match.Success) { return $Default }
    return [int]$match.Groups[1].Value
}

function Write-SpA1(
    [System.Drawing.Bitmap]$Bitmap,
    [string]$Path,
    [int]$FrameWidth = 0
) {
    if ($FrameWidth -le 0) { $FrameWidth = $Bitmap.Width }
    if (($Bitmap.Width % $FrameWidth) -ne 0) {
        throw "Sprite atlas width must be a multiple of its frame width: $Path"
    }
    $frameCount = [int]($Bitmap.Width / $FrameWidth)
    $header = [byte[]]::new(40)
    [System.Text.Encoding]::ASCII.GetBytes('SpA1').CopyTo($header, 0)
    [BitConverter]::GetBytes([uint16]31).CopyTo($header, 4)
    [BitConverter]::GetBytes([uint16]$FrameWidth).CopyTo($header, 6)
    [BitConverter]::GetBytes([uint32]$Bitmap.Width).CopyTo($header, 8)
    [BitConverter]::GetBytes([uint32]$Bitmap.Height).CopyTo($header, 12)
    [BitConverter]::GetBytes([uint32]$frameCount).CopyTo($header, 20)

    $rgba = [byte[]]::new($Bitmap.Width * $Bitmap.Height * 4)
    $offset = 0
    for ($y = 0; $y -lt $Bitmap.Height; $y++) {
        for ($x = 0; $x -lt $Bitmap.Width; $x++) {
            $pixel = $Bitmap.GetPixel($x, $y)
            $rgba[$offset++] = $pixel.R
            $rgba[$offset++] = $pixel.G
            $rgba[$offset++] = $pixel.B
            $rgba[$offset++] = $pixel.A
        }
    }

    $stream = [System.IO.File]::Create($Path)
    try {
        $stream.Write($header, 0, $header.Length)
        $stream.Write($rgba, 0, $rgba.Length)
    } finally {
        $stream.Dispose()
    }
}

function Convert-ButtonStates(
    [string]$InputName,
    [string]$OutputName,
    [int]$FrameWidth,
    [int]$FrameHeight
) {
    $source = [System.Drawing.Bitmap]::new((Join-Path $artwork $InputName))
    $atlas = [System.Drawing.Bitmap]::new(
        $FrameWidth * 4, $FrameHeight,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        if (($source.Width % 2) -ne 0 -or ($source.Height % 2) -ne 0) {
            throw "Button state artwork must be a two-by-two sheet: $InputName"
        }
        $cellWidth = [int]($source.Width / 2)
        $cellHeight = [int]($source.Height / 2)
        $cells = [System.Drawing.Rectangle[]]@(
            [System.Drawing.Rectangle]::new(0, 0, $cellWidth, $cellHeight),
            [System.Drawing.Rectangle]::new($cellWidth, 0, $cellWidth, $cellHeight),
            [System.Drawing.Rectangle]::new(0, $cellHeight, $cellWidth, $cellHeight),
            [System.Drawing.Rectangle]::new($cellWidth, $cellHeight, $cellWidth, $cellHeight))
        $centers = [System.Collections.Generic.List[object]]::new()
        for ($frame = 0; $frame -lt $cells.Count; $frame++) {
            $cell = $cells[$frame]
            [long]$weightedX = 0
            [long]$weightedY = 0
            [long]$alphaTotal = 0
            for ($y = $cell.Y; $y -lt $cell.Bottom; $y++) {
                for ($x = $cell.X; $x -lt $cell.Right; $x++) {
                    $alpha = $source.GetPixel($x, $y).A
                    if ($alpha -gt 16) {
                        $localX = $x - $cell.X
                        $localY = $y - $cell.Y
                        $weightedX += [long]$localX * $alpha
                        $weightedY += [long]$localY * $alpha
                        $alphaTotal += $alpha
                    }
                }
            }
            if ($alphaTotal -eq 0) {
                throw "Button state artwork contains an empty frame: $InputName"
            }
            $centers.Add([pscustomobject]@{
                X = $weightedX / $alphaTotal
                Y = $weightedY / $alphaTotal
            })
        }

        $graphics = [System.Drawing.Graphics]::FromImage($atlas)
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

            # The approved 2x2 sheet is row-major: normal, hover, pressed, disabled.
            for ($frame = 0; $frame -lt $cells.Count; $frame++) {
                $offsetX = [int][Math]::Round(
                    ($centers[0].X - $centers[$frame].X) * $FrameWidth / $cellWidth,
                    [MidpointRounding]::AwayFromZero)
                $offsetY = [int][Math]::Round(
                    ($centers[0].Y - $centers[$frame].Y) * $FrameHeight / $cellHeight,
                    [MidpointRounding]::AwayFromZero)
                $state = [System.Drawing.Bitmap]::new(
                    $FrameWidth, $FrameHeight,
                    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
                $stateGraphics = [System.Drawing.Graphics]::FromImage($state)
                try {
                    $stateGraphics.Clear([System.Drawing.Color]::Transparent)
                    $stateGraphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                    $stateGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $stateGraphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                    $destination = [System.Drawing.Rectangle]::new(
                        $offsetX, $offsetY, $FrameWidth, $FrameHeight)
                    $stateGraphics.DrawImage(
                        $source, $destination, $cells[$frame],
                        [System.Drawing.GraphicsUnit]::Pixel)
                    $graphics.DrawImageUnscaled($state, $frame * $FrameWidth, 0)
                } finally {
                    $stateGraphics.Dispose()
                    $state.Dispose()
                }
            }
        } finally {
            $graphics.Dispose()
        }
        Write-SpA1 $atlas (Join-Path $output $OutputName) $FrameWidth
    } finally {
        $atlas.Dispose()
        $source.Dispose()
    }
}

function Convert-Png([string]$InputName, [string]$OutputName, [int]$Width = 0, [int]$Height = 0) {
    $source = [System.Drawing.Bitmap]::new((Join-Path $artwork $InputName))
    $targetWidth = if ($Width -gt 0) { $Width } else { $source.Width }
    $targetHeight = if ($Height -gt 0) { $Height } else { $source.Height }
    $bitmap = [System.Drawing.Bitmap]::new(
        $targetWidth, $targetHeight,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($source, 0, 0, $targetWidth, $targetHeight)
        } finally {
            $graphics.Dispose()
        }
        Write-SpA1 $bitmap (Join-Path $output $OutputName)
    } finally {
        $bitmap.Dispose()
        $source.Dispose()
    }
}

$trackWidth = [Math]::Max(1, [int][Math]::Round((Get-ElementInt `
    -Section 'track' -Name 'width' -Default 1280) * 0.8 * $nativePanelScale))
$trackHeight = [Math]::Max(1, [int][Math]::Round((Get-ElementInt `
    -Section 'track' -Name 'height' -Default 34) * (1440.0 / 1801.0) * $nativePanelScale))
$tooltipWidth = [Math]::Max(1, [int][Math]::Round((Get-ElementInt `
    -Section 'tooltip' -Name 'width' -Default 118) * 0.8 * $nativePanelScale))
$tooltipHeight = [Math]::Max(1, [int][Math]::Round((Get-ElementInt `
    -Section 'tooltip' -Name 'height' -Default 48) * (1440.0 / 1801.0) * $nativePanelScale))
$markerWidth = [Math]::Max(1, [int][Math]::Round((Get-ElementInt `
    -Section 'marker' -Name 'width' -Default 54) * 0.8 * $nativePanelScale))
$markerHeight = [Math]::Max(1, [int][Math]::Round((Get-ElementInt `
    -Section 'marker' -Name 'height' -Default 72) * (1440.0 / 1801.0) * $nativePanelScale))
$buttonWidth = [Math]::Max(1, [int][Math]::Round((Get-ElementInt `
    -Section 'button' -Name 'width' -Default 58) * 0.8 * $nativePanelScale))
$buttonHeight = [Math]::Max(1, [int][Math]::Round((Get-ElementInt `
    -Section 'button' -Name 'height' -Default 58) * (1440.0 / 1801.0) * $nativePanelScale))
Convert-Png 'hud-player-slider-track.png' 'native-track.sprite' $trackWidth $trackHeight
Convert-Png 'hud-player-slider-marker.png' 'native-marker.sprite' $markerWidth $markerHeight
Convert-Png 'hud-player-slider-tooltip.png' 'native-tooltip.sprite' $tooltipWidth $tooltipHeight
Convert-ButtonStates 'hud-player-slider-button-states.png' `
    'native-button.sprite' $buttonWidth $buttonHeight

Get-ChildItem -LiteralPath $output -Filter 'native-*.sprite' |
    Select-Object Name, Length |
    Format-Table -AutoSize
