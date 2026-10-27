# Read local configuration as data. Never evaluate shell expressions or publish values.
function Read-ThyLocalEnvironment {
    [CmdletBinding()]
    param([string]$Path = (Join-Path $PSScriptRoot '../.env'))
    $values = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return ,$values }
    $lineNumber = 0
    foreach ($raw in [IO.File]::ReadAllLines([IO.Path]::GetFullPath($Path), [Text.Encoding]::UTF8)) {
        $lineNumber++
        $line = $raw.Trim()
        if (-not $line -or $line.StartsWith('#')) { continue }
        $match = [regex]::Match($line, '^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$')
        if (-not $match.Success) { throw ('Invalid local environment syntax at line ' + $lineNumber) }
        $name = $match.Groups[1].Value
        if ($values.ContainsKey($name)) { throw ('Duplicate local environment key at line ' + $lineNumber) }
        $value = $match.Groups[2].Value.Trim()
        if ($value.StartsWith("'") -or $value.StartsWith('"')) {
            $quote = $value.Substring(0, 1)
            $end = $value.IndexOf($quote, 1, [StringComparison]::Ordinal)
            if ($end -lt 0) { throw ('Unterminated local environment value at line ' + $lineNumber) }
            $tail = $value.Substring($end + 1).Trim()
            if ($tail -and -not $tail.StartsWith('#')) { throw ('Unexpected local environment text at line ' + $lineNumber) }
            $value = $value.Substring(1, $end - 1)
        } else {
            $value = [regex]::Replace($value, '\s+#.*$', '').TrimEnd()
        }
        $values.Add($name, $value)
    }
    return ,$values
}

function Resolve-ThyLocalValue {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [string]$Value = '',
        [switch]$Explicit,
        [Parameter(Mandatory=$true)]$Configuration,
        [string]$Default = '',
        [switch]$Required,
        [string]$Pattern = '',
        [switch]$AsPath
    )
    if (-not $Explicit) {
        $inherited = [Environment]::GetEnvironmentVariable($Name, 'Process')
        if (-not [string]::IsNullOrWhiteSpace($inherited)) { $Value = $inherited }
        elseif ($Configuration.ContainsKey($Name) -and -not [string]::IsNullOrWhiteSpace($Configuration[$Name])) { $Value = $Configuration[$Name] }
        else { $Value = $Default }
    }
    if ($Required -and [string]::IsNullOrWhiteSpace($Value)) { throw ('Set ' + $Name + ' in .env, the process environment, or its command-line option') }
    if ($Value -and $Pattern -and $Value -cnotmatch $Pattern) { throw ('Invalid local configuration for ' + $Name) }
    if ($Value -and $AsPath) {
        if (-not [IO.Path]::IsPathRooted($Value)) { $Value = Join-Path (Join-Path $PSScriptRoot '..') $Value }
        $Value = [IO.Path]::GetFullPath($Value)
    }
    return $Value
}
