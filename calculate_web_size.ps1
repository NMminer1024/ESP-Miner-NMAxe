# 检查多个可能的路径
$possiblePaths = @(
    "data",                          # 编译后的数据目录
    "src\http_server\axe-os\dist",   # Angular dist 目录
    "www",                           # 可能的web目录
    "web"                            # 可能的web目录
)

$webDir = $null
foreach ($path in $possiblePaths) {
    if (Test-Path $path) {
        $webDir = $path
        break
    }
}

# 如果没找到编译后的目录，检查附件中提到的压缩包位置
if (-not $webDir) {
    $webDir = "src\http_server\axe-os"
    Write-Host "Warning: Using source directory, this may include development files" -ForegroundColor Yellow
}

Write-Host "Calculating SPIFFS files size in: $webDir" -ForegroundColor Green
Write-Host ("=" * 50)

if (Test-Path $webDir) {
    # 首先查找 .gz 压缩包（这些是真正会被打包到SPIFFS的文件）
    $gzFiles = Get-ChildItem $webDir -Recurse -File -Filter "*.gz"
    
    if ($gzFiles.Count -gt 0) {
        Write-Host "Found compressed web files (.gz):" -ForegroundColor Green
        $gzTotalSize = ($gzFiles | Measure-Object -Property Length -Sum).Sum
        $files = $gzFiles
        Write-Host "These are the files that will be included in SPIFFS.bin" -ForegroundColor Cyan
    } else {
        # 如果没有找到.gz文件，查找dist目录或其他编译输出
        $distDir = Join-Path $webDir "dist"
        if (Test-Path $distDir) {
            Write-Host "No .gz files found, using dist directory: $distDir" -ForegroundColor Yellow
            $files = Get-ChildItem $distDir -Recurse -File
        } else {
            # 最后才使用过滤后的源文件
            Write-Host "No dist directory found, filtering source files" -ForegroundColor Yellow
            $files = Get-ChildItem $webDir -Recurse -File | Where-Object { 
                $_.FullName -notmatch "\.angular\\cache" -and
                $_.FullName -notmatch "node_modules" -and 
                $_.FullName -notmatch "\.git" -and
                $_.FullName -notmatch "src\\app" -and
                $_.FullName -notmatch "\.ts$" -and
                $_.FullName -notmatch "\.scss$" -and
                $_.FullName -notmatch "package\.json" -and
                $_.FullName -notmatch "angular\.json" -and
                $_.FullName -notmatch "tsconfig"
            }
        }
    }
    $totalSize = ($files | Measure-Object -Property Length -Sum).Sum
    
    if ($totalSize -eq $null) { $totalSize = 0 }
    
    $sizeKB = [math]::Round($totalSize / 1KB, 2)
    $sizeMB = [math]::Round($totalSize / 1MB, 2)
    
    # 计算建议的 SPIFFS 大小（2倍 + 64KB 对齐）
    $suggested = [math]::Round($totalSize * 2 / 1KB / 64) * 64
    if ($suggested -lt 64) { $suggested = 64 }
    
    $suggestedHex = [Convert]::ToString($suggested * 1024, 16).ToUpper()
    
    Write-Host "File count: $($files.Count)" -ForegroundColor Cyan
    Write-Host "Total size: $totalSize bytes ($sizeKB KB, $sizeMB MB)" -ForegroundColor Cyan
    Write-Host "Suggested SPIFFS size: $suggested KB (0x$suggestedHex bytes)" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Partition table config:" -ForegroundColor Green
    Write-Host "spiffs, data, spiffs, 0x410000, 0x$suggestedHex," -ForegroundColor White -BackgroundColor DarkBlue
    
    # 列出最大的几个文件
    if ($files.Count -gt 0) {
        Write-Host ""
        Write-Host "Largest files:" -ForegroundColor Green
        $files | Sort-Object Length -Descending | Select-Object -First 10 | ForEach-Object {
            $relPath = $_.FullName.Replace((Get-Location).Path + "\", "")
            $fileSize = [math]::Round($_.Length / 1KB, 2)
            Write-Host "  $relPath - $fileSize KB" -ForegroundColor Gray
        }
    }
    
} else {
    Write-Host "Directory not found: $webDir" -ForegroundColor Red
    Write-Host "Available directories in src\http_server:" -ForegroundColor Yellow
    if (Test-Path "src\http_server") {
        Get-ChildItem "src\http_server" -Directory | ForEach-Object {
            Write-Host "  $($_.Name)" -ForegroundColor Gray
        }
    }
    
    # 也检查其他可能的位置
    Write-Host ""
    Write-Host "Checking other possible locations:" -ForegroundColor Yellow
    $possiblePaths = @(
        "src\http_server\http_server\axe-os",
        "data",
        "www",
        "web"
    )
    
    foreach ($path in $possiblePaths) {
        if (Test-Path $path) {
            Write-Host "  Found: $path" -ForegroundColor Green
        }
    }
}

Write-Host ""
Write-Host "Press any key to continue..." -ForegroundColor Gray
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")