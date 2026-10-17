# Generates application/include/user_interface/userManualText.h from kilavuz_tr.txt.
#
# The manual is written as ordinary UTF-8 Turkish so it can be edited like any
# text. This turns it into what the firmware needs: lines wrapped at word
# boundaries to fit the small font beside the scroll bar, and the Turkish letters
# replaced by the codes the built-in font uses for them (the same ones turkish.h
# uses). A C hex escape swallows every hex digit that follows it, so the string is
# split wherever an escape is followed by one - doing that by hand across a few
# hundred lines is how mistakes get in, which is why this exists.
#
# Source format:
#   (kilavuz_tr.txt, kilavuz_en.txt and kilavuz_es.txt)
#   @@ Name        starts a topic; Name is the list entry and page title (<= 16)
#   - text         a bullet, wrapped with a hanging indent
#   (blank)        a blank line on screen
#   anything else  a paragraph, wrapped
#
# Usage: powershell -ExecutionPolicy Bypass -File genUserManual.ps1

param(
	[string]$Out = (Join-Path $PSScriptRoot '..\..\application\include\user_interface\userManualText.h')
)

$ErrorActionPreference = 'Stop'
$Width   = 25   # characters per line: 6px font, leaving room for the scroll bar
$NameMax = 16   # list entries and titles share a 17-byte buffer

function ConvertTo-CLiteral([string]$s)
{
	$sb = New-Object System.Text.StringBuilder
	$prevEsc = $false

	foreach ($ch in $s.ToCharArray())
	{
		$code = [int]$ch
		$esc = $null

		switch ($code)
		{
			0x0131 { $esc = '93' }  # i (dotless)
			0x0130 { $esc = '94' }  # I (dotted)
			0x015E { $esc = '95' }  # S cedilla
			0x015F { $esc = '96' }  # s cedilla
			0x011F { $esc = '98' }  # g breve
			0x00C7 { $esc = 'C7' }  # C cedilla
			0x00E7 { $esc = 'E7' }  # c cedilla
			0x00D6 { $esc = 'D6' }  # O umlaut
			0x00F6 { $esc = 'F6' }  # o umlaut
			0x00DC { $esc = 'DC' }  # U umlaut
			0x00FC { $esc = 'FC' }  # u umlaut
			# Spanish: these sit at their Latin-1 places in the font. The inverted
			# question and exclamation marks do not exist there (0xBF is an empty
			# slot, 0xA1 is u-ring), so the text is written without them.
			0x00E1 { $esc = 'E1' }  # a acute
			0x00E9 { $esc = 'E9' }  # e acute
			0x00ED { $esc = 'ED' }  # i acute
			0x00F3 { $esc = 'F3' }  # o acute
			0x00FA { $esc = 'FA' }  # u acute
			0x00F1 { $esc = 'F1' }  # n tilde
			0x00C1 { $esc = 'C1' }  # A acute
			0x00C9 { $esc = 'C9' }  # E acute
			0x00CD { $esc = 'CD' }  # I acute
			0x00D3 { $esc = 'D3' }  # O acute
			0x00DA { $esc = 'DA' }  # U acute
			0x00D1 { $esc = 'D1' }  # N tilde
			# Russian: Windows-1251, drawn from the Cyrillic fonts while the radio
			# is in Russian (HX8353E_charset_cyrillic.h).
			0x0401 { $esc = 'A8' }  # Yo
			0x0451 { $esc = 'B8' }  # yo
		}

		if (($code -ge 0x0410) -and ($code -le 0x044F))
		{
			$esc = '{0:X2}' -f ($code - 0x0410 + 0xC0)  # A..ya
		}

		if ($esc)
		{
			[void]$sb.Append('\x' + $esc)
			$prevEsc = $true
			continue
		}

		if (($code -lt 0x20) -or ($code -gt 0x7E))
		{
			throw ("Desteklenmeyen karakter U+{0:X4} '{1}' (buyuk G-breve fontta yok) satir: {2}" -f $code, $ch, $s)
		}

		if ($prevEsc -and ($ch -match '[0-9A-Fa-f]'))
		{
			[void]$sb.Append('""')
		}

		if ($ch -eq '"')     { [void]$sb.Append('\"') }
		elseif ($ch -eq '\') { [void]$sb.Append('\\') }
		else                 { [void]$sb.Append($ch) }

		$prevEsc = $false
	}

	return ('"' + $sb.ToString() + '"')
}

function Get-Wrapped([string]$text, [string]$p1, [string]$p2)
{
	$out = New-Object System.Collections.Generic.List[string]
	$line = ''
	$prefix = $p1

	foreach ($w in ($text -split ' '))
	{
		if ($w -eq '') { continue }

		$avail = $Width - $prefix.Length

		# A word longer than a whole line is cut, rather than left to overflow.
		while ($w.Length -gt $avail)
		{
			if ($line -ne '')
			{
				$out.Add($prefix + $line)
				$line = ''
				$prefix = $p2
				$avail = $Width - $prefix.Length
			}
			$out.Add($prefix + $w.Substring(0, $avail))
			$w = $w.Substring($avail)
			$prefix = $p2
			$avail = $Width - $prefix.Length
		}

		if ($line -eq '')
		{
			$line = $w
		}
		elseif (($line.Length + 1 + $w.Length) -le $avail)
		{
			$line = $line + ' ' + $w
		}
		else
		{
			$out.Add($prefix + $line)
			$prefix = $p2
			$line = $w
		}
	}

	if ($line -ne '')
	{
		$out.Add($prefix + $line)
	}

	return ,$out
}

function Read-Topics([string]$path)
{
	$raw = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
	$topics = New-Object System.Collections.Generic.List[object]
	$cur = $null

	foreach ($sl in ($raw -split "`r?`n"))
	{
		$t = $sl.TrimEnd()

		if ($t.StartsWith('@@ '))
		{
			$cur = [pscustomobject]@{ Name = $t.Substring(3).Trim(); Lines = (New-Object System.Collections.Generic.List[string]) }
			$topics.Add($cur)
			continue
		}

		if ($null -eq $cur) { continue }

		if ($t -eq '')
		{
			# One blank line at most, and none at the very top of a topic.
			if (($cur.Lines.Count -gt 0) -and ($cur.Lines[$cur.Lines.Count - 1] -ne ''))
			{
				$cur.Lines.Add('')
			}
			continue
		}

		if ($t.StartsWith('- '))
		{
			foreach ($l in (Get-Wrapped $t.Substring(2) '- ' '  ')) { $cur.Lines.Add($l) }
		}
		else
		{
			foreach ($l in (Get-Wrapped $t '' '')) { $cur.Lines.Add($l) }
		}
	}

	# Drop trailing blank lines, and check the limits the screen imposes.
	foreach ($tp in $topics)
	{
		while (($tp.Lines.Count -gt 0) -and ($tp.Lines[$tp.Lines.Count - 1] -eq ''))
		{
			$tp.Lines.RemoveAt($tp.Lines.Count - 1)
		}
		if ($tp.Name.Length -gt $NameMax) { throw ("Konu adi {0} karakterden uzun: {1}" -f $NameMax, $tp.Name) }
		foreach ($l in $tp.Lines)
		{
			if ($l.Length -gt $Width) { throw ("Satir {0} karakteri asiyor: {1}" -f $Width, $l) }
		}
	}

	return ,$topics
}

# One language's arrays, named with its suffix: umLinesTR / umTopicsTR /
# UM_TOPIC_COUNT_TR and so on. The viewer picks a set by the radio's language.
function Add-Language([System.Text.StringBuilder]$sb, $topics, [string]$sfx)
{
	$lineCount = 0
	foreach ($tp in $topics) { $lineCount += $tp.Lines.Count }

	[void]$sb.AppendLine(('#define UM_LINE_COUNT_{0}  {1}' -f $sfx, $lineCount))
	[void]$sb.AppendLine(('#define UM_TOPIC_COUNT_{0} {1}' -f $sfx, $topics.Count))
	[void]$sb.AppendLine('')

	# All the lines of a language as one block of NUL-terminated strings, with a
	# table of where each starts. One array rather than a string per line, so
	# that the text itself - not just a table of pointers to it - can be placed
	# in .hiconst, the flash after the codec (see STM32F405VGTX_FLASH.ld).
	# Every character, Turkish ones included, is one byte in the font's codepage,
	# so a line's length in the firmware is its length here.
	[void]$sb.AppendLine(('static const char umText{0}[] __attribute__((section(".hiconst"))) =' -f $sfx))

	$first = 0
	$offset = 0
	$offsets = New-Object System.Collections.Generic.List[int]
	$index = New-Object System.Collections.Generic.List[string]
	foreach ($tp in $topics)
	{
		[void]$sb.AppendLine(('	// {0}' -f ($tp.Name -replace '[^\x20-\x7E]', '_')))
		foreach ($l in $tp.Lines)
		{
			$lit = ConvertTo-CLiteral $l
			[void]$sb.AppendLine(('	{0}' -f ($lit.Substring(0, $lit.Length - 1) + '\0"')))
			$offsets.Add($offset)
			$offset += ($l.Length + 1)
		}
		$index.Add(('	{{ {0}, {1}, {2} }},' -f (ConvertTo-CLiteral $tp.Name), $first, $tp.Lines.Count))
		$first += $tp.Lines.Count
	}

	if ($offset -gt 65535) { throw ("{0} metni 64 KB'i asiyor, offset tablosu uint16_t" -f $sfx) }

	[void]$sb.AppendLine('	;')
	[void]$sb.AppendLine('')
	[void]$sb.AppendLine(('static const uint16_t umOffs{0}[UM_LINE_COUNT_{0}] __attribute__((section(".hiconst"))) =' -f $sfx))
	[void]$sb.AppendLine('{')
	for ($i = 0; $i -lt $offsets.Count; $i += 12)
	{
		$chunk = $offsets.GetRange($i, [Math]::Min(12, $offsets.Count - $i))
		[void]$sb.AppendLine(('	{0},' -f ($chunk -join ', ')))
	}
	[void]$sb.AppendLine('};')
	[void]$sb.AppendLine('')
	[void]$sb.AppendLine(('static const umTopic_t umTopics{0}[UM_TOPIC_COUNT_{0}] =' -f $sfx))
	[void]$sb.AppendLine('{')
	foreach ($e in $index) { [void]$sb.AppendLine($e) }
	[void]$sb.AppendLine('};')
	[void]$sb.AppendLine('')

	"{0}: {1} konu, {2} satir" -f $sfx, $topics.Count, $lineCount
}

$langs = @(
	@{ Sfx = 'TR'; Src = (Join-Path $PSScriptRoot 'kilavuz_tr.txt') },
	@{ Sfx = 'EN'; Src = (Join-Path $PSScriptRoot 'kilavuz_en.txt') },
	@{ Sfx = 'ES'; Src = (Join-Path $PSScriptRoot 'kilavuz_es.txt') },
	@{ Sfx = 'RU'; Src = (Join-Path $PSScriptRoot 'kilavuz_ru.txt') }
)

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// Auto-generated by tools/usermanual/genUserManual.ps1 from kilavuz_tr.txt,')
[void]$sb.AppendLine('// kilavuz_en.txt and kilavuz_es.txt. Do not edit by hand: change the .txt files')
[void]$sb.AppendLine('// and run the script.')
[void]$sb.AppendLine('#ifndef _USER_MANUAL_TEXT_H_')
[void]$sb.AppendLine('#define _USER_MANUAL_TEXT_H_')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#include <stdint.h>')
[void]$sb.AppendLine('')
[void]$sb.AppendLine(('#define UM_LINE_CHARS {0}' -f $Width))
[void]$sb.AppendLine('')
[void]$sb.AppendLine('typedef struct')
[void]$sb.AppendLine('{')
[void]$sb.AppendLine('	const char *name;  // list entry and page title')
[void]$sb.AppendLine('	uint16_t    first; // index of the topic''s first line in the lines array')
[void]$sb.AppendLine('	uint16_t    count; // number of lines')
[void]$sb.AppendLine('} umTopic_t;')
[void]$sb.AppendLine('')

foreach ($lg in $langs)
{
	Add-Language $sb (Read-Topics $lg.Src) $lg.Sfx
}

[void]$sb.AppendLine('#endif')

[System.IO.File]::WriteAllText((Resolve-Path (Split-Path $Out -Parent)).Path + '\' + (Split-Path $Out -Leaf), $sb.ToString(), [System.Text.Encoding]::ASCII)
