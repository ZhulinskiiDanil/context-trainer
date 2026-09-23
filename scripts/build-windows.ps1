[CmdletBinding()]
param(
    [string]$GeodeSdk = $env:GEODE_SDK,
    [string]$BuildDirectory,
    [string]$Generator = 'Visual Studio 18 2026',
    [ValidateRange(1, 64)]
    [int]$Parallel = 4,
    [string]$DependencyCacheDirectory,
    [string]$CodegenBinary,
    [switch]$TestsOnly,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$sourceDirectory = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) {
    $buildName = if ($TestsOnly) { 'build-tests' } else { 'build' }
    $BuildDirectory = Join-Path $sourceDirectory $buildName
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$cmakeArguments = @('-S', $sourceDirectory, '-B', $BuildDirectory, '-G', $Generator)
if ($Generator -like 'Visual Studio*') {
    $cmakeArguments += @('-A', 'x64')
}

if ($TestsOnly) {
    $cmakeArguments += '-DCONTEXT_TESTS_ONLY=ON'
} else {
    if (-not $GeodeSdk) {
        throw 'Pass -GeodeSdk or set the GEODE_SDK environment variable.'
    }
    $cmakeArguments += @('-DCONTEXT_TESTS_ONLY=OFF', "-DGEODE_SDK=$($GeodeSdk.Replace('\', '/'))")

    if ($DependencyCacheDirectory) {
        $dependencyRoot = (Resolve-Path -LiteralPath $DependencyCacheDirectory).Path
        foreach ($package in @('result', 'json', 'nontype_functional', 'fmt', 'asp2', 'arc', 'TulipHook')) {
            $packageSource = Join-Path $dependencyRoot "$($package.ToLower())-src"
            if (-not (Test-Path -LiteralPath $packageSource -PathType Container)) {
                throw "Missing cached package: $packageSource"
            }
            $cmakeArguments += "-DCPM_${package}_SOURCE=$($packageSource.Replace('\', '/'))"
        }
        $bindingsSource = Join-Path $dependencyRoot 'bindings-src'
        $cmakeArguments += "-DGEODE_BINDINGS_REPO_PATH=$($bindingsSource.Replace('\', '/'))"

        $cpmCache = Join-Path (Split-Path -Parent $dependencyRoot) 'cmake'
        if (Test-Path -LiteralPath $cpmCache -PathType Container) {
            $destination = Join-Path $BuildDirectory 'cmake'
            New-Item -ItemType Directory -Path $destination -Force | Out-Null
            Get-ChildItem -LiteralPath $cpmCache -Filter 'CPM_*.cmake' |
                Copy-Item -Destination $destination
        }
    }

    if ($CodegenBinary) {
        $codegenSource = (Resolve-Path -LiteralPath $CodegenBinary).Path
        $codegenDirectory = Join-Path $BuildDirectory 'bindings/codegen'
        New-Item -ItemType Directory -Path $codegenDirectory -Force | Out-Null
        Copy-Item -LiteralPath $codegenSource -Destination (Join-Path $codegenDirectory 'Codegen.exe')
        $cmakeArguments += '-DSKIP_BUILDING_CODEGEN=ON'
    }
}

& cmake @cmakeArguments
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)." }
if ($ConfigureOnly) { return }

& cmake --build $BuildDirectory --config Release --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE)." }
if ($TestsOnly) {
    & ctest --test-dir $BuildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Training tests failed ($LASTEXITCODE)." }
}
