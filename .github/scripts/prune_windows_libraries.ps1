param(
    [Parameter(Mandatory = $true)]
    [string] $LibrariesPath
)

$ErrorActionPreference = 'Stop'
$workspace = (Get-Item -LiteralPath $env:GITHUB_WORKSPACE).FullName
$libraries = (Get-Item -LiteralPath $LibrariesPath).FullName
$expected = [System.IO.Path]::GetFullPath((Join-Path $workspace $LibrariesPath))
if ($libraries -ne $expected -or -not $libraries.StartsWith("$workspace\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Library path must stay inside the workspace: $libraries"
}
$extensions = @('.lib', '.a', '.exe', '.dll', '.h', '.hpp', '.inc', '.cmake', '.pc')
Get-ChildItem -LiteralPath $libraries -File -Recurse | ForEach-Object {
    $relative = [System.IO.Path]::GetRelativePath($libraries, $_.FullName).Replace('\', '/')
    if ($_.Extension.ToLowerInvariant() -notin $extensions -and
        $relative -notmatch '(^|/)(include|objects-[^/]+|cache_keys|patches|nv-codec-headers)(/|$)') {
        Remove-Item -LiteralPath $_.FullName
    }
}
