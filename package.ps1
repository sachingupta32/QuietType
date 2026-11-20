$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
Copy-Item .build/toolchain/llvm-mingw-20261117-ucrt-x86_64/LICENSE.TXT dist/app/LICENSE-runtime.txt
$components = foreach($file in (Get-ChildItem dist/app -File | Sort-Object Name)){
    $id='F_'+($file.Name -replace '[^a-zA-Z0-9]','_')
    $shortcut = if($file.Name -eq 'QuietType.exe'){ '<Shortcut Id="Launch" Directory="ProgramMenuFolder" Name="QuietType" WorkingDirectory="INSTALLDIR" Advertise="no" />' }
    "<Component Id='$id' Guid='*' Win64='yes'><RegistryValue Root='HKCU' Key='Software\QuietType\Installer' Name='$id' Type='integer' Value='1' KeyPath='yes'/><File Id='Bin_$id' Source='dist\app\$($file.Name)'>$shortcut</File></Component>"
}
# Fixed component GUIDs derived from filenames keep upgrades stable for HKCU components.
$components = foreach($line in $components){
    $id=[regex]::Match($line,"Id='([^']+)'").Groups[1].Value
    $hash=[Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes("QuietType/$id"))
    $guid=[Guid]::new([byte[]]$hash[0..15])
    $line.Replace("Guid='*'","Guid='$guid'")
}
$refs = foreach($line in $components){ "<ComponentRef Id='"+[regex]::Match($line,"Id='([^']+)'").Groups[1].Value+"'/>" }
@"
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
<Product Id="*" Name="QuietType" Language="1033" Version="1.3.0" Manufacturer="QuietType" UpgradeCode="C2120C1B-F259-4413-8C03-C47A38DD2443">
<Package InstallerVersion="500" Compressed="yes" InstallScope="perUser" InstallPrivileges="limited" Platform="x64"/>
<MajorUpgrade DowngradeErrorMessage="A newer version of QuietType is installed." AllowSameVersionUpgrades="yes"/>
<MediaTemplate EmbedCab="yes"/>
<CustomAction Id="CleanStartup" FileKey="Bin_F_QuietType_exe" ExeCommand="--remove-startup" Execute="immediate" Return="ignore"/>
<InstallExecuteSequence><Custom Action="CleanStartup" Before="RemoveFiles">REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE</Custom></InstallExecuteSequence>
<Directory Id="TARGETDIR" Name="SourceDir"><Directory Id="ProgramMenuFolder"/>
<Directory Id="LocalAppDataFolder"><Directory Id="ProgramsDir" Name="Programs"><Directory Id="INSTALLDIR" Name="QuietType">
$($components -join [Environment]::NewLine)
</Directory></Directory></Directory></Directory>
<Feature Id="Main" Title="QuietType" Level="1">$($refs -join [Environment]::NewLine)</Feature>
</Product></Wix>
"@ | Set-Content .build/installer.wxs -Encoding UTF8
& .build/wix/candle.exe -nologo -out .build/installer.wixobj .build/installer.wxs
if($LASTEXITCODE){throw 'Installer compilation failed'}
& .build/wix/light.exe -nologo -sval -spdb -out dist/QuietType-x64.msi .build/installer.wixobj
if($LASTEXITCODE){throw 'Installer linking failed'}
