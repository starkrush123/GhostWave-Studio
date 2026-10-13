$ErrorActionPreference='Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'UI smoke testing is allowed only in GitHub Actions.' }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms
$Portable=(Resolve-Path ci-output/smoke).Path
Copy-Item ci-output/native/home/smoke.sn "$Portable/ghostwave.sn"
$App=Start-Process "$Portable/GhostWaveStudio.exe" -WorkingDirectory $Portable -PassThru
try {
    $Deadline=[DateTime]::UtcNow.AddSeconds(20)
    do { Start-Sleep -Milliseconds 200; $App.Refresh() } while (!$App.MainWindowHandle -and !$App.HasExited -and [DateTime]::UtcNow -lt $Deadline)
    if (!$App.MainWindowHandle) { throw 'Portable GUI did not create a window.' }
    $Window=[System.Windows.Automation.AutomationElement]::FromHandle($App.MainWindowHandle)
    $Items=$Window.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
    $Rows=@($Items | ForEach-Object { @{name=$_.Current.Name; role=$_.Current.ControlType.ProgrammaticName; focusable=$_.Current.IsKeyboardFocusable; enabled=$_.Current.IsEnabled} })
    foreach ($Name in @('Audio','Lyrics','Process audio','Preset','Format')) { if (!($Rows | Where-Object { $_.name -eq $Name })) { throw "Accessible control missing: $Name" } }
    $Focusable=@($Rows | Where-Object { $_.focusable -and $_.enabled })
    if ($Focusable.Count -lt 6) { throw 'Too few keyboard accessible controls.' }
    $Button=$Items | Where-Object { $_.Current.Name -eq 'Process audio' } | Select-Object -First 1
    $Button.SetFocus()
    Start-Sleep -Milliseconds 200
    if (![System.Windows.Automation.AutomationElement]::FocusedElement.Current.HasKeyboardFocus) { throw 'Keyboard focus failed.' }
    [System.Windows.Forms.SendKeys]::SendWait('{TAB}')
    Start-Sleep -Milliseconds 200
    if ([System.Windows.Automation.AutomationElement]::FocusedElement.Current.Name -eq 'Process audio') { throw 'Tab did not move focus.' }
    @{passed=$true; screen_reader_manual_check='pending'; controls=$Rows} | ConvertTo-Json -Depth 8 | Set-Content ci-output/ui-results.json -Encoding utf8
} finally {
    if (!$App.HasExited) { $null=$App.CloseMainWindow(); if (!$App.WaitForExit(5000)) { $App.Kill(); throw 'GUI did not close cleanly.' } }
    Remove-Item "$Portable/ghostwave.sn" -ErrorAction SilentlyContinue
}
