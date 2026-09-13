param(
    [Parameter(Mandatory=$true)][string]$Output
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$directory = Split-Path -Parent $Output
[IO.Directory]::CreateDirectory($directory) | Out-Null
$size = 256
$bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.Clear([Drawing.Color]::Transparent)

$circle = [Drawing.RectangleF]::new(10, 10, 236, 236)
$clip = [Drawing.Drawing2D.GraphicsPath]::new()
$clip.AddEllipse($circle)
$graphics.SetClip($clip)
$graphics.FillRectangle([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(105, 112, 120)), 0, 0, $size, $size)
$green = [Drawing.PointF[]]@([Drawing.PointF]::new(0,0), [Drawing.PointF]::new($size,0), [Drawing.PointF]::new(0,$size))
$graphics.FillPolygon([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(35, 190, 105)), $green)
$graphics.ResetClip()

$outline = [Drawing.Pen]::new([Drawing.Color]::White, 12)
$graphics.DrawEllipse($outline, $circle)
$mark = [Drawing.Pen]::new([Drawing.Color]::White, 18)
$mark.StartCap = [Drawing.Drawing2D.LineCap]::Round
$mark.EndCap = [Drawing.Drawing2D.LineCap]::Round
$graphics.DrawLine($mark, 82, 128, 174, 128)
$graphics.DrawLine($mark, 128, 82, 128, 174)

$iconHandle = $bitmap.GetHicon()
$icon = [Drawing.Icon]::FromHandle($iconHandle).Clone()
$stream = [IO.File]::Open($Output, [IO.FileMode]::Create)
$icon.Save($stream)
$stream.Dispose()
$icon.Dispose(); $mark.Dispose(); $outline.Dispose(); $clip.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
