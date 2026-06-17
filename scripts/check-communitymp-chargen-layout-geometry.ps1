[CmdletBinding()]
param(
    [string]$SourceRoot = "",
    [switch]$FailOnIssue
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

function Get-LayoutXml {
    param([string]$RelativePath)

    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Layout was not found: $path"
    }

    [xml](Get-Content -LiteralPath $path -Raw)
}

function Get-SourceText {
    param([string]$RelativePath)

    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Source file was not found: $path"
    }

    Get-Content -LiteralPath $path -Raw
}

function Get-Rect {
    param([System.Xml.XmlElement]$Widget)

    if (-not $Widget.HasAttribute("position")) {
        return $null
    }

    $parts = $Widget.GetAttribute("position") -split "\s+" | Where-Object { $_ -ne "" }
    if ($parts.Count -ne 4) {
        throw "Invalid position on widget '$($Widget.GetAttribute("name"))': $($Widget.GetAttribute("position"))"
    }

    $x = [int]$parts[0]
    $y = [int]$parts[1]
    $w = [int]$parts[2]
    $h = [int]$parts[3]

    [pscustomobject]@{
        X = $x
        Y = $y
        W = $w
        H = $h
        R = $x + $w
        B = $y + $h
        Area = $w * $h
    }
}

function Get-WidgetLabel {
    param([System.Xml.XmlElement]$Widget)

    $name = $Widget.GetAttribute("name")
    if ($name -eq "") {
        $name = "<anonymous>"
    }

    "$($Widget.GetAttribute("type"))/$name"
}

function Add-Issue {
    param(
        [System.Collections.Generic.List[string]]$Issues,
        [string]$Message
    )

    $Issues.Add($Message)
}

function Test-WidgetTree {
    param(
        [string]$RelativePath,
        [System.Xml.XmlElement]$Widget,
        [System.Collections.Generic.List[string]]$Issues
    )

    $rect = Get-Rect $Widget
    if ($null -ne $rect) {
        if ($rect.W -le 0 -or $rect.H -le 0) {
            Add-Issue $Issues "${RelativePath}: $(Get-WidgetLabel $Widget) has non-positive geometry $($Widget.GetAttribute("position"))"
        }
    }

    foreach ($child in @($Widget.SelectNodes("Widget"))) {
        if ($null -eq $child) {
            continue
        }

        $childRect = Get-Rect $child
        if ($null -ne $rect -and $null -ne $childRect -and $Widget.GetAttribute("type") -notin @("HBox", "VBox")) {
            if ($childRect.X -lt 0 -or $childRect.Y -lt 0 -or $childRect.R -gt $rect.W -or $childRect.B -gt $rect.H) {
                Add-Issue $Issues "${RelativePath}: $(Get-WidgetLabel $child) escapes parent $(Get-WidgetLabel $Widget): child=$($child.GetAttribute("position")) parentSize=$($rect.W)x$($rect.H)"
            }
        }

        Test-WidgetTree $RelativePath $child $Issues
    }
}

function Get-FirstWidgetByName {
    param(
        [xml]$Xml,
        [string]$Name
    )

    $Xml.SelectSingleNode("//Widget[@name='$Name']")
}

function Assert-RootSize {
    param(
        [string]$RelativePath,
        [xml]$Xml,
        [int]$ExpectedWidth,
        [int]$ExpectedHeight,
        [System.Collections.Generic.List[string]]$Issues
    )

    $root = [System.Xml.XmlElement]$Xml.MyGUI.Widget
    $rect = Get-Rect $root
    if ($null -eq $rect -or $rect.W -ne $ExpectedWidth -or $rect.H -ne $ExpectedHeight) {
        Add-Issue $Issues "${RelativePath}: root expected ${ExpectedWidth}x${ExpectedHeight}, got $($root.GetAttribute("position"))"
    }

    if ($root.GetAttribute("name") -ne "_Main") {
        Add-Issue $Issues "${RelativePath}: root widget must be named _Main for WindowBase layout binding"
    }

    if ($root.GetAttribute("type") -notin @("Window", "Widget")) {
        Add-Issue $Issues "${RelativePath}: root widget must be Window or Widget, got $($root.GetAttribute("type"))"
    }

    if ($root.GetAttribute("layer") -notin @("Modal", "Windows")) {
        Add-Issue $Issues "${RelativePath}: root widget must live on a modal/window layer, got $($root.GetAttribute("layer"))"
    }
}

function Assert-WidgetAspect {
    param(
        [string]$RelativePath,
        [xml]$Xml,
        [string]$Name,
        [double]$MinRatio,
        [double]$MaxRatio,
        [int]$MinArea,
        [System.Collections.Generic.List[string]]$Issues
    )

    $widget = Get-FirstWidgetByName $Xml $Name
    if ($null -eq $widget) {
        Add-Issue $Issues "${RelativePath}: expected preview widget '$Name' was not found"
        return
    }

    $rect = Get-Rect $widget
    $ratio = $rect.W / [double]$rect.H
    if ($ratio -lt $MinRatio -or $ratio -gt $MaxRatio -or $rect.Area -lt $MinArea) {
        Add-Issue $Issues "${RelativePath}: $Name ratio/area out of range: ratio=$([math]::Round($ratio, 3)) area=$($rect.Area)"
    }
}

function Assert-HeadingGutters {
    param(
        [string]$RelativePath,
        [xml]$Xml,
        [string[]]$HeadingNames,
        [int]$MinGap,
        [System.Collections.Generic.List[string]]$Issues
    )

    $rects = @()
    foreach ($name in $HeadingNames) {
        $widget = Get-FirstWidgetByName $Xml $name
        if ($null -eq $widget) {
            Add-Issue $Issues "${RelativePath}: expected heading '$name' was not found"
            return
        }
        $rects += Get-Rect $widget
    }

    for ($i = 0; $i -lt $rects.Count - 1; ++$i) {
        $gap = $rects[$i + 1].X - $rects[$i].R
        if ($gap -lt $MinGap) {
            Add-Issue $Issues "${RelativePath}: heading gutter between $($HeadingNames[$i]) and $($HeadingNames[$i + 1]) is $gap, expected at least $MinGap"
        }
    }
}

function Assert-MwBoxSiblingOverlap {
    param(
        [string]$RelativePath,
        [xml]$Xml,
        [System.Collections.Generic.List[string]]$Issues
    )

    $root = [System.Xml.XmlElement]$Xml.MyGUI.Widget
    $boxes = @()
    $index = 0
    foreach ($child in @($root.SelectNodes("Widget"))) {
        if ($null -eq $child -or -not $child.HasAttribute("position")) {
            continue
        }

        if ($child.GetAttribute("skin") -eq "MW_Box") {
            $boxes += [pscustomobject]@{
                Index = $index
                Label = Get-WidgetLabel $child
                Rect = Get-Rect $child
            }
        }
        ++$index
    }

    for ($i = 0; $i -lt $boxes.Count; ++$i) {
        for ($j = $i + 1; $j -lt $boxes.Count; ++$j) {
            $a = $boxes[$i].Rect
            $b = $boxes[$j].Rect
            $overlapW = [math]::Max(0, [math]::Min($a.R, $b.R) - [math]::Max($a.X, $b.X))
            $overlapH = [math]::Max(0, [math]::Min($a.B, $b.B) - [math]::Max($a.Y, $b.Y))
            $overlapArea = $overlapW * $overlapH
            if ($overlapArea -gt 0) {
                Add-Issue $Issues "${RelativePath}: top-level MW_Box siblings overlap by $overlapArea px^2 ($($boxes[$i].Label) #$($boxes[$i].Index), $($boxes[$j].Label) #$($boxes[$j].Index))"
            }
        }
    }
}

function Get-PropertyValue {
    param(
        [System.Xml.XmlElement]$Widget,
        [string]$Key
    )

    $property = $Widget.SelectSingleNode("Property[@key='$Key']")
    if ($null -eq $property) {
        return $null
    }

    $property.GetAttribute("value")
}

function Assert-StaticTextFit {
    param(
        [string]$RelativePath,
        [xml]$Xml,
        [System.Collections.Generic.List[string]]$Issues
    )

    $averageCharacterWidth = 8.0
    $lineHeight = 18

    foreach ($widget in @($Xml.SelectNodes("//Widget[@type='TextBox']"))) {
        if ($null -eq $widget) {
            continue
        }

        $caption = Get-PropertyValue $widget "Caption"
        if ([string]::IsNullOrEmpty($caption) -or $caption.Contains("#{")) {
            continue
        }

        $rect = Get-Rect $widget
        if ($null -eq $rect) {
            continue
        }

        $wordWrap = Get-PropertyValue $widget "WordWrap"
        $wrapEnabled = $wordWrap -eq "true" -or $wordWrap -eq "1"
        $maxCharactersPerLine = [math]::Max(1, [math]::Floor($rect.W / $averageCharacterWidth))
        $estimatedLines = if ($wrapEnabled) { [math]::Ceiling($caption.Length / $maxCharactersPerLine) } else { 1 }
        $requiredHeight = [int]($estimatedLines * $lineHeight)
        $requiredWidth = [int][math]::Ceiling($caption.Length * $averageCharacterWidth)

        if (($wrapEnabled -and $rect.H -lt $requiredHeight) -or (-not $wrapEnabled -and $rect.W -lt $requiredWidth)) {
            Add-Issue $Issues "${RelativePath}: static caption on $(Get-WidgetLabel $widget) may clip; textLength=$($caption.Length) rect=$($rect.W)x$($rect.H) estimatedLines=$estimatedLines"
        }
    }
}

function Assert-CharacterSelectActionButtons {
    param(
        [string]$RelativePath,
        [xml]$Xml,
        [System.Collections.Generic.List[string]]$Issues
    )

    $actionButtons = Get-FirstWidgetByName $Xml "ActionButtons"
    if ($null -eq $actionButtons) {
        Add-Issue $Issues "${RelativePath}: character select layout is missing the ActionButtons alignment strip"
        return
    }

    $actionRect = Get-Rect $actionButtons
    if ($actionRect.X -ne 894 -or $actionRect.Y -ne 544 -or $actionRect.W -ne 236 -or $actionRect.H -ne 34 -or $actionButtons.GetAttribute("align") -ne "Left Top") {
        Add-Issue $Issues "${RelativePath}: ActionButtons expected fixed local geometry 894 544 236 34 Left Top, got $($actionButtons.GetAttribute("position")) $($actionButtons.GetAttribute("align"))"
    }

    $deleteButton = [System.Xml.XmlElement]$actionButtons.SelectSingleNode("Widget[@name='DeleteButton']")
    $selectButton = [System.Xml.XmlElement]$actionButtons.SelectSingleNode("Widget[@name='SelectButton']")
    if ($null -eq $deleteButton -or $null -eq $selectButton) {
        Add-Issue $Issues "${RelativePath}: ActionButtons must own DeleteButton and SelectButton"
        return
    }

    foreach ($button in @($deleteButton, $selectButton)) {
        if ($button.GetAttribute("type") -ne "Button" -or $button.GetAttribute("skin") -ne "CommunityMP_CharacterActionButton") {
            Add-Issue $Issues "${RelativePath}: $($button.GetAttribute("name")) must use the centered CommunityMP_CharacterActionButton skin"
        }
    }

    $deleteRect = Get-Rect $deleteButton
    $selectRect = Get-Rect $selectButton
    if ($deleteRect.X -ne 0 -or $deleteRect.Y -ne 1 -or $deleteRect.W -ne 74 -or $deleteRect.H -ne 30) {
        Add-Issue $Issues "${RelativePath}: DeleteButton expected 0 1 74 30 inside ActionButtons, got $($deleteButton.GetAttribute("position"))"
    }

    if ($selectRect.X -ne 82 -or $selectRect.Y -ne 1 -or $selectRect.W -ne 154 -or $selectRect.H -ne 30) {
        Add-Issue $Issues "${RelativePath}: SelectButton expected 82 1 154 30 inside ActionButtons, got $($selectButton.GetAttribute("position"))"
    }

    if ($selectRect.X - $deleteRect.R -ne 8 -or $selectRect.R -ne $actionRect.W) {
        Add-Issue $Issues "${RelativePath}: action buttons must keep an 8px gutter and align to the right edge of ActionButtons"
    }
}

function Assert-CreatorStageRail {
    param(
        [string]$RelativePath,
        [xml]$Xml,
        [string]$ActiveStage,
        [System.Collections.Generic.List[string]]$Issues
    )

    $rails = @($Xml.SelectNodes("//Widget[@name='CreatorStageRail']"))
    if ($rails.Count -ne 1) {
        Add-Issue $Issues "${RelativePath}: expected exactly one CreatorStageRail, found $($rails.Count)"
        return
    }

    $rootRect = Get-Rect ([System.Xml.XmlElement]$Xml.MyGUI.Widget)
    $expectedRail = if ($rootRect.W -eq 1320) {
        @{ X = 48; Y = 668; W = 1224; H = 30; StageWidth = 224; StageX = @(18, 252, 486, 720, 954) }
    } elseif ($rootRect.W -eq 1120) {
        @{ X = 34; Y = 604; W = 1042; H = 30; StageWidth = 192; StageX = @(14, 216, 418, 620, 822) }
    } elseif ($rootRect.W -eq 900) {
        @{ X = 32; Y = 64; W = 836; H = 30; StageWidth = 148; StageX = @(14, 172, 330, 488, 646) }
    } elseif ($rootRect.W -eq 760) {
        @{ X = 32; Y = 64; W = 696; H = 30; StageWidth = 120; StageX = @(14, 144, 274, 404, 534) }
    } else {
        Add-Issue $Issues "${RelativePath}: creator stage rail is not configured for root width $($rootRect.W)"
        return
    }

    $rail = [System.Xml.XmlElement]$rails[0]
    $railRect = Get-Rect $rail
    if ($rail.GetAttribute("skin") -ne "MW_Box" -or $railRect.X -ne $expectedRail.X -or $railRect.Y -ne $expectedRail.Y -or $railRect.W -ne $expectedRail.W -or $railRect.H -ne $expectedRail.H) {
        Add-Issue $Issues "${RelativePath}: creator stage rail geometry changed from the verified $($expectedRail.X) $($expectedRail.Y) $($expectedRail.W) $($expectedRail.H) frame"
    }

    $stageOrder = @(
        @{ Name = "CreatorStageIdentity"; Caption = "01 Identity" },
        @{ Name = "CreatorStageAppearance"; Caption = "02 Appearance" },
        @{ Name = "CreatorStageClass"; Caption = "03 Class" },
        @{ Name = "CreatorStageBirthsign"; Caption = "04 Birthsign" },
        @{ Name = "CreatorStageReview"; Caption = "05 Review" }
    )

    $activeColour = "1 0.86 0.42"
    $inactiveColour = "0.55 0.62 0.62"
    $stageWidth = $expectedRail.StageWidth
    $stageHeight = 18
    $stageTop = 6
    $minGap = 10
    $previousRect = $null

    for ($stageIndex = 0; $stageIndex -lt $stageOrder.Count; ++$stageIndex) {
        $stage = $stageOrder[$stageIndex]
        $widget = $rail.SelectSingleNode("Widget[@name='$($stage.Name)']")
        if ($null -eq $widget) {
            Add-Issue $Issues "${RelativePath}: creator stage rail is missing $($stage.Name)"
            return
        }

        $rect = Get-Rect $widget
        if ($rect.X -ne $expectedRail.StageX[$stageIndex] -or $rect.W -ne $stageWidth -or $rect.H -ne $stageHeight -or $rect.Y -ne $stageTop) {
            Add-Issue $Issues "${RelativePath}: $($stage.Name) expected x=$($expectedRail.StageX[$stageIndex]) ${stageWidth}x${stageHeight} at y=$stageTop, got $($widget.GetAttribute("position"))"
        }

        if ($null -ne $previousRect) {
            $gap = $rect.X - $previousRect.R
            if ($gap -lt $minGap) {
                Add-Issue $Issues "${RelativePath}: creator stage rail gap before $($stage.Name) is $gap, expected at least $minGap"
            }
        }
        $previousRect = $rect

        $caption = Get-PropertyValue $widget "Caption"
        if ($caption -ne $stage.Caption) {
            Add-Issue $Issues "${RelativePath}: $($stage.Name) caption expected '$($stage.Caption)', got '$caption'"
        }

        $expectedColour = if ($stage.Name -eq $ActiveStage) { $activeColour } else { $inactiveColour }
        $actualColour = Get-PropertyValue $widget "TextColour"
        if ($actualColour -ne $expectedColour) {
            Add-Issue $Issues "${RelativePath}: $($stage.Name) colour expected '$expectedColour', got '$actualColour'"
        }
    }
}

function Get-FallbackTextValues {
    param([string]$KeyPattern)

    $config = Get-SourceText "files\openmw.cfg"
    $values = @()
    foreach ($match in [regex]::Matches($config, "(?m)^fallback=($KeyPattern),(.+)$")) {
        $values += $match.Groups[2].Value
    }
    $values
}

function Get-LongestText {
    param([string[]]$Values)

    if ($Values.Count -eq 0) {
        return ""
    }

    $Values | Sort-Object Length -Descending | Select-Object -First 1
}

function Get-EstimatedWrappedLines {
    param(
        [string]$Text,
        [int]$Width,
        [double]$AverageCharacterWidth
    )

    $maxCharactersPerLine = [math]::Max(1, [math]::Floor($Width / $AverageCharacterWidth))
    [int][math]::Ceiling($Text.Length / $maxCharactersPerLine)
}

function Assert-GeneratedClassQuestionCapacity {
    param([System.Collections.Generic.List[string]]$Issues)

    $xml = Get-LayoutXml "files\data\mygui\openmw_chargen_generate_class_question.layout"
    $textWidget = Get-FirstWidgetByName $xml "Text"
    $buttonBar = Get-FirstWidgetByName $xml "ButtonBar"
    if ($null -eq $textWidget -or $null -eq $buttonBar) {
        Add-Issue $Issues "Generated class question layout is missing Text or ButtonBar"
        return
    }

    $question = Get-LongestText (Get-FallbackTextValues "Question_\d+_Question")
    $answer = Get-LongestText (Get-FallbackTextValues "Question_\d+_Answer(?:One|Two|Three)")
    if ($question -eq "" -or $answer -eq "") {
        Add-Issue $Issues "Generated class question fallback strings were not found"
        return
    }

    $averageCharacterWidth = 8.0
    $lineHeight = 18
    $questionRect = Get-Rect $textWidget
    $questionLines = Get-EstimatedWrappedLines $question $questionRect.W $averageCharacterWidth
    if ($questionLines * $lineHeight -gt $questionRect.H) {
        Add-Issue $Issues "Generated class question text may clip: longestLength=$($question.Length) estimatedLines=$questionLines rect=$($questionRect.W)x$($questionRect.H)"
    }

    $buttonBarRect = Get-Rect $buttonBar
    $buttonHeight = 58
    $buttonGap = 10
    $answerLines = Get-EstimatedWrappedLines $answer $buttonBarRect.W $averageCharacterWidth
    if ($answerLines * $lineHeight -gt $buttonHeight) {
        Add-Issue $Issues "Generated class answer text may clip: longestLength=$($answer.Length) estimatedLines=$answerLines buttonHeight=$buttonHeight"
    }

    $requiredButtonBarHeight = 3 * $buttonHeight + 2 * $buttonGap
    if ($requiredButtonBarHeight -gt $buttonBarRect.H) {
        Add-Issue $Issues "Generated class answer buttons exceed ButtonBar height: required=$requiredButtonBarHeight actual=$($buttonBarRect.H)"
    }
}

function Get-NamedFloatConstant {
    param(
        [string]$Source,
        [string]$Name
    )

    $match = [regex]::Match($Source, "constexpr\s+float\s+$Name\s*=\s*(-?[0-9]+(?:\.[0-9]*)?)f")
    if (-not $match.Success) {
        throw "Could not find float constant '$Name'"
    }

    [double]$match.Groups[1].Value
}

function Assert-AvatarPreviewCameraMath {
    param([System.Collections.Generic.List[string]]$Issues)

    $previewSource = Get-SourceText "apps\openmw\mwrender\characterpreview.cpp"
    $controllerSource = Get-SourceText "apps\openmw\mwgui\avatarpreview.hpp"

    $fullDistance = Get-NamedFloatConstant $previewSource "bodyPreviewFullDistance"
    $closeDistance = Get-NamedFloatConstant $previewSource "bodyPreviewCloseDistance"
    $fullTargetZ = Get-NamedFloatConstant $previewSource "bodyPreviewFullTargetZ"
    $closeTargetZ = Get-NamedFloatConstant $previewSource "bodyPreviewCloseTargetZ"
    $minFocusOffset = Get-NamedFloatConstant $previewSource "bodyPreviewMinFocusOffset"
    $maxFocusOffset = Get-NamedFloatConstant $previewSource "bodyPreviewMaxFocusOffset"
    $fovYDegrees = Get-NamedFloatConstant $previewSource "bodyPreviewFovYDegrees"
    $verticalPadding = Get-NamedFloatConstant $previewSource "bodyPreviewVerticalPadding"
    $dragMomentumFramesPerSecond = Get-NamedFloatConstant $controllerSource "dragMomentumFramesPerSecond"
    $maxAngularVelocity = Get-NamedFloatConstant $controllerSource "maxAngularVelocity"
    $momentumDecayPerSecond = Get-NamedFloatConstant $controllerSource "momentumDecayPerSecond"
    $stopAngularVelocity = Get-NamedFloatConstant $controllerSource "stopAngularVelocity"
    $maxFrameDuration = Get-NamedFloatConstant $controllerSource "maxFrameDuration"

    if ($fullDistance -le 0 -or $closeDistance -le 0 -or $closeDistance -ge $fullDistance) {
        Add-Issue $Issues "Avatar preview zoom distances must be positive and ordered close < full; got close=$closeDistance full=$fullDistance"
    }

    if ($fovYDegrees -lt 8 -or $fovYDegrees -gt 20) {
        Add-Issue $Issues "Avatar preview vertical FOV is outside the portrait framing range: $fovYDegrees"
    }

    if ($verticalPadding -lt 1.05 -or $verticalPadding -gt 1.30) {
        Add-Issue $Issues "Avatar preview bounds padding is outside the tight framing range: $verticalPadding"
    }

    if ($closeTargetZ -le $fullTargetZ) {
        Add-Issue $Issues "Avatar preview close target must rise above full-body target; got closeZ=$closeTargetZ fullZ=$fullTargetZ"
    }

    $targetRisePerDistance = ($closeTargetZ - $fullTargetZ) / ($fullDistance - $closeDistance)
    if ($targetRisePerDistance -lt 0.05 -or $targetRisePerDistance -gt 0.30) {
        Add-Issue $Issues "Avatar preview target rise per distance is outside inspection framing range: $([math]::Round($targetRisePerDistance, 3))"
    }

    if ($minFocusOffset -ge 0 -or $maxFocusOffset -le 0 -or [math]::Abs($minFocusOffset) -ne $maxFocusOffset) {
        Add-Issue $Issues "Avatar preview focus offset must be symmetric around zero; got min=$minFocusOffset max=$maxFocusOffset"
    }

    $focusSpan = $maxFocusOffset - $minFocusOffset
    if ($focusSpan -lt 72 -or $focusSpan -gt 120) {
        Add-Issue $Issues "Avatar preview focus span is outside controlled inspection range: $focusSpan"
    }

    if ($dragMomentumFramesPerSecond -lt 30 -or $dragMomentumFramesPerSecond -gt 120) {
        Add-Issue $Issues "Avatar preview drag momentum sampling rate is outside responsive UI bounds: $dragMomentumFramesPerSecond"
    }

    if ($maxAngularVelocity -lt 1 -or $maxAngularVelocity -gt 8) {
        Add-Issue $Issues "Avatar preview max angular velocity is outside comfortable inspection bounds: $maxAngularVelocity"
    }

    if ($momentumDecayPerSecond -le 0 -or $momentumDecayPerSecond -ge 1) {
        Add-Issue $Issues "Avatar preview momentum decay must be an exponential factor in (0, 1), got $momentumDecayPerSecond"
    }

    if ($stopAngularVelocity -le 0 -or $stopAngularVelocity -ge $maxAngularVelocity) {
        Add-Issue $Issues "Avatar preview stop angular velocity must be positive and below max angular velocity"
    }

    if ($maxFrameDuration -le 0 -or $maxFrameDuration -gt 0.25) {
        Add-Issue $Issues "Avatar preview frame duration cap must bound large frame stalls, got $maxFrameDuration"
    }

    if (-not [regex]::IsMatch($previewSource, 'mInspectionZoom\s*=\s*std::clamp\(zoom,\s*0\.f,\s*1\.f\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview zoom input is not clamped to the normalized [0, 1] interval"
    }

    if (-not [regex]::IsMatch($previewSource, 'mInspectionFocusOffset\s*=\s*std::clamp\(focusOffset,\s*bodyPreviewMinFocusOffset,\s*bodyPreviewMaxFocusOffset\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview vertical focus is not clamped to the authored body framing interval"
    }

    if (-not [regex]::IsMatch($previewSource, 'smoothZoom\s*=\s*mInspectionZoom\s*\*\s*mInspectionZoom\s*\*\s*\(3\.f\s*-\s*2\.f\s*\*\s*mInspectionZoom\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview zoom does not use the expected cubic smoothstep curve"
    }

    if (-not [regex]::IsMatch($previewSource, 'getBodyPreviewFitDistance\s*\(\s*float\s+height\s*\).*bodyPreviewVerticalPadding.*std::tan', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview camera does not derive a fit distance from the authored FOV and bounds padding"
    }

    if (-not [regex]::IsMatch($previewSource, 'osg::ComputeBoundsVisitor\s+boundsVisitor.*mNode->getNumChildren\(\).*getChild\(i\)->accept\(boundsVisitor\).*bounds\.valid\(\).*std::max\(fullDistance,\s*getBodyPreviewFitDistance\(height\)\).*fullTargetZ\s*=\s*\(bounds\.zMin\(\)\s*\+\s*bounds\.zMax\(\)\)\s*\*\s*0\.5f', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview camera does not fit and recenter to the character's rendered bounds"
    }

    if (-not [regex]::IsMatch($previewSource, 'fullDistance\s*\+\s*\(bodyPreviewCloseDistance\s*-\s*fullDistance\)\s*\*\s*smoothZoom', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview zoom does not blend from the bounds-aware full-body distance"
    }

    if (-not [regex]::IsMatch($previewSource, 'bodyPreviewCloseTargetZ\s*-\s*fullTargetZ\)\s*\*\s*smoothZoom\s*\+\s*mInspectionFocusOffset', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview camera target does not apply the bounded vertical focus offset"
    }

    if (-not [regex]::IsMatch($previewSource, 'osg::Vec3f\s+position\(0\.f,\s*distance,\s*targetZ\).*osg::Vec3f\s+lookAt\(0\.f,\s*0\.f,\s*targetZ\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview camera position and target are not locked to a shared target Z"
    }

    if (-not [regex]::IsMatch($controllerSource, 'eventMouseWheel\s*\+=\s*MyGUI::newDelegate\(this,\s*&AvatarPreviewController::onMouseWheel\).*setZoom\(mZoom\s*\+\s*\(rel\s*>\s*0\s*\?\s*wheelZoomStep\s*:\s*-wheelZoomStep\)\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview controller does not route mouse wheel input into bounded zoom"
    }

    if (-not [regex]::IsMatch($controllerSource, 'configureInspectionLimits\(float\s+maxZoom,\s*float\s+maxVerticalFocus\).*mMaxZoom\s*=\s*std::clamp\(maxZoom,\s*minZoom,\s*defaultMaxZoom\).*mMaxVerticalFocus\s*=\s*std::max\(0\.f,\s*std::min\(maxVerticalFocus,\s*defaultMaxVerticalFocus\)\).*setZoom\(mZoom\).*setVerticalFocus\(mVerticalFocus\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview controller does not expose per-dialog zoom/focus limits"
    }

    if (-not [regex]::IsMatch($controllerSource, 'mZoom\s*=\s*std::clamp\(zoom,\s*minZoom,\s*mMaxZoom\).*mVerticalFocus\s*=\s*std::clamp\(focus,\s*-mMaxVerticalFocus,\s*mMaxVerticalFocus\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview controller does not apply per-dialog inspection bounds"
    }

    if (-not [regex]::IsMatch($controllerSource, 'mAngularVelocity\s*=\s*std::clamp\(deltaAngle\s*\*\s*dragMomentumFramesPerSecond,\s*-maxAngularVelocity,\s*maxAngularVelocity\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview controller does not bound drag momentum"
    }

    if (-not [regex]::IsMatch($controllerSource, 'mAngularVelocity\s*\*=\s*std::pow\(momentumDecayPerSecond,\s*frameDuration\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview controller does not exponentially decay drag momentum"
    }

    if (-not [regex]::IsMatch($controllerSource, 'eventMouseButtonDoubleClick\s*\+=\s*MyGUI::newDelegate\(this,\s*&AvatarPreviewController::onMouseDoubleClick\).*mAngularVelocity\s*=\s*0\.f.*setAngle\(0\.f\).*setZoom\(0\.f\).*setVerticalFocus\(0\.f\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Add-Issue $Issues "Avatar preview controller does not expose a deterministic double-click reset"
    }
}

$layouts = @(
    @{ Path = "files\data\mygui\openmw_chargen_name.layout"; Width = 1320; Height = 760; Headings = @("NameStageT", "NameAvatarT", "NameNextT"); Preview = "AvatarPreviewImage"; ActiveStage = "CreatorStageIdentity" },
    @{ Path = "files\data\mygui\openmw_chargen_race.layout"; Width = 1320; Height = 760; Headings = @("RaceT", "AppearanceT", "SkillsT"); Preview = "PreviewImage"; ActiveStage = "CreatorStageAppearance" },
    @{ Path = "files\data\mygui\openmw_chargen_class_choice.layout"; Width = 1320; Height = 760; Headings = @("ClassRouteTitle", "ClassRouteAvatarT", "ClassRouteBriefT"); Preview = "AvatarPreviewImage"; ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_chargen_class.layout"; Width = 1320; Height = 760; Headings = @("ClassStageT", "ClassPreviewT", "ClassBuildT"); Preview = "AvatarPreviewImage"; ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_chargen_create_class.layout"; Width = 1120; Height = 690; Headings = @("ClassIdentityT", "ClassAvatarT", "ClassLoadoutT"); Preview = "AvatarPreviewImage"; ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_chargen_generate_class_question.layout"; Width = 900; Height = 560; Headings = @("ClassQuestionTitle"); ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_chargen_birth.layout"; Width = 1320; Height = 760; Headings = @("BirthStageT", "BirthImageT", "BirthPowerT"); Preview = "AvatarPreviewImage"; ActiveStage = "CreatorStageBirthsign" },
    @{ Path = "files\data\mygui\openmw_chargen_review.layout"; Width = 1120; Height = 690; Headings = @("ReviewIdentityT", "ReviewAvatarT", "ReviewBuildT"); Preview = "AvatarPreviewImage"; ActiveStage = "CreatorStageReview" },
    @{ Path = "files\data\mygui\openmw_chargen_generate_class_result.layout"; Width = 900; Height = 560; Headings = @("GeneratedClassT"); ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_chargen_select_specialization.layout"; Width = 900; Height = 430; Headings = @("LabelT"); ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_chargen_select_attribute.layout"; Width = 900; Height = 560; Headings = @("LabelT"); ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_chargen_select_skill.layout"; Width = 900; Height = 560; Headings = @("LabelT"); ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_chargen_class_description.layout"; Width = 900; Height = 560; Headings = @("DescriptionTitle"); ActiveStage = "CreatorStageClass" },
    @{ Path = "files\data\mygui\openmw_infobox.layout"; Width = 680; Height = 330 },
    @{ Path = "files\mygui\characterselect\communitymp_character_select.layout"; Width = 1180; Height = 650; Headings = @("RosterEyebrow", "StageEyebrow", "CharacterTitle"); Preview = "Portrait" },
    @{ Path = "files\mygui\chat\communitymp_chat.layout"; Width = 490; Height = 331 },
    @{ Path = "files\mygui\chat\examples\default\communitymp_chat.layout"; Width = 490; Height = 331 }
)

$issues = [System.Collections.Generic.List[string]]::new()

foreach ($layout in $layouts) {
    $xml = Get-LayoutXml $layout.Path
    Assert-RootSize $layout.Path $xml $layout.Width $layout.Height $issues
    Test-WidgetTree $layout.Path ([System.Xml.XmlElement]$xml.MyGUI.Widget) $issues
    Assert-MwBoxSiblingOverlap $layout.Path $xml $issues
    Assert-StaticTextFit $layout.Path $xml $issues

    if ($layout.ContainsKey("ActiveStage")) {
        Assert-CreatorStageRail $layout.Path $xml $layout.ActiveStage $issues
    }

    if ($layout.ContainsKey("Headings")) {
        Assert-HeadingGutters $layout.Path $xml $layout.Headings 20 $issues
    }

    if ($layout.ContainsKey("Preview")) {
        Assert-WidgetAspect $layout.Path $xml $layout.Preview 0.55 0.90 120000 $issues
    }

    if ($layout.Path -eq "files\mygui\characterselect\communitymp_character_select.layout") {
        Assert-CharacterSelectActionButtons $layout.Path $xml $issues
    }
}

Assert-AvatarPreviewCameraMath $issues
Assert-GeneratedClassQuestionCapacity $issues

Write-Host "CommunityMP chargen layout geometry check"
Write-Host "Source root: $SourceRoot"
Write-Host "Layouts checked: $($layouts.Count)"
Write-Host "Camera checks: 1"
Write-Host "Questionnaire capacity checks: 1"
Write-Host "Issues: $($issues.Count)"

foreach ($issue in $issues) {
    Write-Host " - $issue"
}

if ($FailOnIssue -and $issues.Count -gt 0) {
    exit 1
}
