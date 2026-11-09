; Buyer installer for Windows (Inno Setup 6): Welcome -> Licence (the terms of sale) -> Components
; (Infinite app / VST3) -> Ready -> Install -> Finished (next steps).
;
;   iscc /DSrc=<staged dist\Infinite> /DArt=<make_art.py out>\windows /DTerms=<terms.txt>
;        /DVersion=1.1.0 /DArch=x64|ARM64 /DOut=<dir> tools\installer\windows\Infinite.iss
;
; Per user, no admin: the app goes to %LOCALAPPDATA%\Programs\Infinite and is always installed,
; since it is the plugin's engine. "Infinite app" adds the Start menu and desktop entries. "VST3"
; runs `Infinite.com --install-plugins vst3` (src/core/DawPluginInstall.h): the same copier as the
; app's Settings > DAW plugin, into %LOCALAPPDATA%\Programs\Common\VST3, with the engine recorded.
; Not code-signed yet: SmartScreen asks once (More info > Run anyway); the download page says so.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif

[Setup]
AppId={{6E1F3C2A-9B4D-4F7E-A1C5-3D8B2F6E9A01}
AppName=Infinite
AppVersion={#Version}
AppVerName=Infinite {#Version}
AppPublisher=Infinite Devices
AppPublisherURL=https://infinitedevices.org
AppSupportURL=https://infinitedevices.org/download.html
DefaultDirName={localappdata}\Programs\Infinite
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
#if Arch == "ARM64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
WizardStyle=modern
WizardImageFile={#Art}\wizard.bmp
WizardSmallImageFile={#Art}\wizard-small.bmp
SetupIconFile=..\..\..\art\brand\logo\windows\Infinite.ico
UninstallDisplayIcon={app}\Infinite.exe
LicenseFile={#Terms}
OutputDir={#Out}
OutputBaseFilename=Infinite-Setup-windows-{#Arch}
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
RestartApplications=no

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "app"; Description: "Infinite app"; Types: full custom
Name: "vst3"; Description: "VST3 (Infinite Instrument, Infinite FX)"; Types: full custom

[Files]
Source: "{#Src}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Infinite"; Filename: "{app}\Infinite.exe"; Components: app
Name: "{userdesktop}\Infinite"; Filename: "{app}\Infinite.exe"; Components: app

[Run]
Filename: "{app}\Infinite.com"; Parameters: "--install-plugins vst3"; Components: vst3; \
  StatusMsg: "Adding the plugins to your VST3 folder..."; Flags: runhidden waituntilterminated
Filename: "{app}\Infinite.exe"; Description: "Open Infinite"; Components: app; \
  Flags: postinstall nowait skipifsilent unchecked

[UninstallRun]
Filename: "{app}\Infinite.com"; Parameters: "--uninstall-plugins"; RunOnceId: "RemovePlugins"; Flags: runhidden waituntilterminated

[Messages]
WelcomeLabel1=Infinite
WelcomeLabel2=A visual instrument and effect for your DAW, and the standalone app.%n%nNext you choose what to install. Everything installs for your user account, so no admin rights are needed.
SelectComponentsLabel2=Infinite app is the standalone app. VST3 adds Infinite Instrument and Infinite FX for your DAW.
FinishedHeadingLabel=Infinite is installed
; steps:begin (tools/installer/sync_steps.py)
FinishedLabelNoIcons=Next:%n%n1. Open your DAW and rescan plugins.%n%n2. Add Infinite Instrument to an instrument track, or Infinite FX to an audio track, and open it.%n%n3. The licence screen opens. Copy the key from your email and click Paste key.%n%n4. Play a few notes.%n%nIf your DAW does not find Infinite, add %LOCALAPPDATA%\Programs\Common\VST3 to its VST3 folders.
FinishedLabel=Next:%n%n1. Open your DAW and rescan plugins.%n%n2. Add Infinite Instrument to an instrument track, or Infinite FX to an audio track, and open it.%n%n3. The licence screen opens. Copy the key from your email and click Paste key.%n%n4. Play a few notes.%n%nIf your DAW does not find Infinite, add %LOCALAPPDATA%\Programs\Common\VST3 to its VST3 folders.
; steps:end
