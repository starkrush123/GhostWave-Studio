param([string]$PortablePath='ci-output/smoke')
$ErrorActionPreference='Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'UI smoke testing is allowed only in GitHub Actions.' }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms
$Portable=(Resolve-Path $PortablePath).Path
Copy-Item ci-output/native/home/smoke.sn "$Portable/ghostwave.sn"
$App=Start-Process "$Portable/GhostWaveStudio.exe" -WorkingDirectory $Portable -PassThru
try {
    $Deadline=[DateTime]::UtcNow.AddSeconds(20)
    do { Start-Sleep -Milliseconds 200; $App.Refresh() } while (!$App.MainWindowHandle -and !$App.HasExited -and [DateTime]::UtcNow -lt $Deadline)
    if (!$App.MainWindowHandle) { throw 'Portable GUI did not create a window.' }
    $Window=[System.Windows.Automation.AutomationElement]::FromHandle($App.MainWindowHandle)
    $Deadline=[DateTime]::UtcNow.AddSeconds(20)
    do {
        $Items=$Window.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
        $Rows=@($Items | ForEach-Object { @{name=$_.Current.Name; role=$_.Current.ControlType.ProgrammaticName; focusable=$_.Current.IsKeyboardFocusable; enabled=$_.Current.IsEnabled} })
        if ($Rows | Where-Object { $_.name -eq 'Audio' }) { break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $Deadline)
    @{passed=$false; window=$Window.Current.Name; controls=$Rows} | ConvertTo-Json -Depth 8 | Set-Content ci-output/ui-results.json -Encoding utf8
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
    function Find-Control($Root,$Name,$Role) {
        $Found=$Root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -eq $Name -and (!$Role -or $_.Current.ControlType.ProgrammaticName -eq "ControlType.$Role") } | Select-Object -First 1
        if (!$Found) { throw "Accessible control missing: $Name $Role" }
        return $Found
    }
    function Find-Dialog($Name) {
        $Condition=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty,$App.Id)
        $Deadline=[DateTime]::UtcNow.AddSeconds(20)
        do {
            $Found=[System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,$Condition) | Where-Object { $_.Current.Name -eq $Name } | Select-Object -First 1
            if ($Found) { return $Found }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $Deadline)
        throw "Accessible dialog missing: $Name"
    }
    $Button.SetFocus()
    [System.Windows.Forms.SendKeys]::SendWait('{F1}')
    $Guide=Find-Dialog 'Keyboard guide'
    $null=Find-Control $Guide 'Close' 'Button'
    [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
    $Advanced=Find-Control $Window 'Advanced' 'Button'
    $Advanced.SetFocus()
    [System.Windows.Forms.SendKeys]::SendWait(' ')
    $Dialog=Find-Dialog 'Advanced'
    foreach ($Name in @('Timing variation','Trim duration','Check at startup')) { $null=Find-Control $Dialog $Name 'CheckBox' }
    $null=Find-Control $Dialog 'Cancel' 'Button'
    [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
    $Tab=Find-Control $Window 'Lyrics' 'TabItem'
    $Selection=$Tab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    $Selection.Select()
    $Input=Find-Control $Window 'Source lyrics' 'Edit'
    $Input.SetFocus()
    [System.Windows.Forms.SendKeys]::SendWait('Hello beautiful world')
    $Process=Find-Control $Window 'Process lyrics' 'Button'
    $Process.SetFocus()
    [System.Windows.Forms.SendKeys]::SendWait(' ')
    $Report=Find-Dialog 'Compare lyrics'
    $null=Find-Control $Report 'Copy result' 'Button'
    [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
    $null=Find-Control $Window 'Result' 'Edit'
    if (!$Process.Current.IsEnabled) { throw 'Lyrics processing did not restore the controls.' }
    @{passed=$true; screen_reader_manual_check='pending'; controls=$Rows; dialogs=@('Keyboard guide','Advanced','Compare lyrics'); lyrics_processed=$true} | ConvertTo-Json -Depth 8 | Set-Content ci-output/ui-results.json -Encoding utf8
} finally {
    if (!$App.HasExited) { $null=$App.CloseMainWindow(); if (!$App.WaitForExit(5000)) { $App.Kill(); throw 'GUI did not close cleanly.' } }
    Remove-Item "$Portable/ghostwave.sn" -ErrorAction SilentlyContinue
}
