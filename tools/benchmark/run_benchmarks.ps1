param(
	[ValidateRange(5, 10000)]
	[int]$Samples = 101
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildDir = Join-Path $repoRoot 'build/perf'
$releaseDir = Join-Path $buildDir 'Release'

Push-Location $repoRoot
try {
	& cmake -S tools/benchmark -B build/perf -DCMAKE_BUILD_TYPE=Release
	if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
	& cmake --build build/perf --config Release --target runtime_perf runtime_perf_baseline rotation_perf -j 4
	if ($LASTEXITCODE -ne 0) { throw 'Benchmark build failed.' }

	& (Join-Path $releaseDir 'runtime_perf.exe') $Samples | Set-Content -Encoding ascii (Join-Path $buildDir 'scheduler_current_amplified.csv')
	if ($LASTEXITCODE -ne 0) { throw 'Current scheduler benchmark failed.' }
	& (Join-Path $releaseDir 'runtime_perf_baseline.exe') $Samples | Set-Content -Encoding ascii (Join-Path $buildDir 'scheduler_baseline_amplified.csv')
	if ($LASTEXITCODE -ne 0) { throw 'Baseline scheduler benchmark failed.' }
	& (Join-Path $releaseDir 'rotation_perf.exe') $Samples | Set-Content -Encoding ascii (Join-Path $buildDir 'rotation_current.csv')
	if ($LASTEXITCODE -ne 0) { throw 'Rotation benchmark failed.' }

	$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
	$cache = Get-Content (Join-Path $buildDir 'CMakeCache.txt')
	$instanceEntry = $cache | Where-Object { $_ -match '^CMAKE_GENERATOR_INSTANCE:INTERNAL=' } | Select-Object -First 1
	$instancePath = if ($instanceEntry) { ($instanceEntry -split '=', 2)[1] } else { $null }
	$toolsetRoot = if ($instancePath) { Join-Path $instancePath 'VC/Tools/MSVC' } else { $null }
	$toolsetDir = if ($toolsetRoot -and (Test-Path -LiteralPath $toolsetRoot)) {
		Get-ChildItem -LiteralPath $toolsetRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
	} else { $null }
	$toolsetVersion = if ($toolsetDir) { $toolsetDir.Name } else { 'unknown' }
	$compilerVersion = if ($toolsetVersion -match '^14\.(\d+)') { "19.$($Matches[1])" } else { $toolsetVersion }
	$skyrimRunning = [bool](Get-Process SkyrimSE -ErrorAction SilentlyContinue)
	$metadata = [ordered]@{
		date_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
		commit = (& git rev-parse HEAD).Trim()
		cpu = $processor.Name.Trim()
		physical_cores = $processor.NumberOfCores
		logical_processors = $processor.NumberOfLogicalProcessors
		compiler = "MSVC $compilerVersion (toolset $toolsetVersion)"
		generator = 'Visual Studio 17 2022'
		samples = $Samples
		optimization = 'Release /O2'
		skyrim_se_process_running = $skyrimRunning
		measurement_scope = 'Host CPU timings with test fakes; not Skyrim engine wall time.'
	}
	$metadata | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $buildDir 'benchmark_metadata.json')
}
finally {
	Pop-Location
}
