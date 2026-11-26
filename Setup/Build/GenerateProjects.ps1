#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$engineRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$solution = Join-Path $engineRoot 'Kurisu.slnx'

# Premake breaks on non-ASCII paths, so generate through an ASCII junction.
# A junction, unlike subst, does not broadcast a drive change to every window,
# which made the console wait for a busy Visual Studio before closing.
$aliasRoot = Join-Path $env:ProgramData 'KurisuProjectGen'
$aliasCreated = $false

function Remove-Alias {
    $item = Get-Item -LiteralPath $aliasRoot -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return }
    if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Not a junction, refusing to remove: $aliasRoot"
    }
    # Non-recursive delete removes only the link, never the repository behind it.
    [IO.Directory]::Delete($aliasRoot)
}

try {
    if ($aliasRoot -match '[^\x00-\x7F]') {
        throw "Generation path is not ASCII: $aliasRoot"
    }

    # A previous run that was killed may have left the junction behind.
    Remove-Alias
    New-Item -ItemType Junction -Path $aliasRoot -Target $engineRoot | Out-Null
    $aliasCreated = $true

    Push-Location -LiteralPath $aliasRoot
    try {
        & "$aliasRoot\Setup\Build\premake5.exe" --file=Setup/Build/premake5.lua vs2026

        if ($LASTEXITCODE -ne 0) {
            throw "Premake failed: $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }

    # premake5.lua keeps paths relative, so this is only a safety net for absolute ones.
    $aliasForward = $aliasRoot.Replace('\', '/')
    $projectDirectory = Join-Path $engineRoot 'Build\Projects'

    # Premake skips files whose content is unchanged, and Visual Studio asks to
    # reload any file whose timestamp moves. Write back only real changes, with
    # the same BOM, so the next run still matches what premake produces.
    function Update-GeneratedFile([string]$Path, [string[]]$From, [string[]]$To) {
        $bytes = [IO.File]::ReadAllBytes($Path)
        $hasBom = $bytes.Length -ge 3 -and
            $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
        $original = [IO.File]::ReadAllText($Path)
        $content = $original
        for ($i = 0; $i -lt $From.Count; ++$i) {
            $content = $content.Replace($From[$i], $To[$i])
        }
        if ($content.Contains($aliasRoot) -or $content.Contains($aliasForward)) {
            throw "Temporary path reference remains in $(Split-Path -Leaf $Path)."
        }
        if ($content -ne $original) {
            [IO.File]::WriteAllText($Path, $content, [Text.UTF8Encoding]::new($hasBom))
        }
        return $content
    }

    Get-ChildItem -LiteralPath $projectDirectory `
        -Filter '*.vcxproj*' -File | ForEach-Object {
        [void](Update-GeneratedFile $_.FullName `
            @("$aliasRoot\", "$aliasForward/", $aliasRoot, $aliasForward) `
            @('$(ProjectDir)..\..\', '$(ProjectDir)../../', '$(ProjectDir)..\..', '$(ProjectDir)../..'))
    }

    if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
        throw "Solution was not generated: $solution"
    }

    # Solution paths are relative to the repository, not to ProjectDir.
    $solutionContent = Update-GeneratedFile $solution `
        @("$aliasRoot\", "$aliasForward/") @('', '')

    [xml]$solutionDocument = $solutionContent
    $projectReferences = $solutionDocument.SelectNodes(
        '//Project/@Path | //BuildDependency/@Project')
    foreach ($projectReference in $projectReferences) {
        $projectPath = Join-Path $engineRoot $projectReference.Value
        if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
            throw "Solution references a missing project: $projectPath"
        }
    }
    Write-Host "Solution: $solution"
    Write-Host 'Open the root Kurisu.slnx in Visual Studio. Project files are under Build\Projects.'
    exit 0
}
catch {
    [Console]::Error.WriteLine("Project generation failed: $($_.Exception.Message)")
    exit 1
}
finally {
    if ($aliasCreated) { Remove-Alias }
}
