; CoolSynth 808's Windows installer (CoolSynth's D363, as CoolSynth 303's and CoolSynth's own, its D209), built with
; Inno Setup 6.5 or newer:
;   ISCC /DAppVersion=0.1.0 "/DBuildDir=..\build\ci-release\CoolSynth808_artefacts\Release" installer\CoolSynth808.iss
; scripts\ci\PackageRelease.ps1 -Installer passes the version from CMakeLists.txt. It installs the standalone app with
; a Start menu entry, the VST3 and the CLAP where hosts look (Common Files\VST3 and Common Files\CLAP; the CLAP,
; CoolSynth's D367, with its own tick box). Uninstalling removes what it installed and
; nothing else: the player's projects, patterns (%APPDATA%\CoolSynth808\Patterns), controller setup and the app's
; settings in %APPDATA%\CoolSynth808 stay.
; No firewall rule: Windows asks once, on the app's first start, for the beat sync (the user's pick, D363).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\ci-release\CoolSynth808_artefacts\Release"
#endif

[Setup]
; Fixed for every version, so a new installer updates the one already there.
AppId={{8CDDFC94-8BD3-4D04-96CF-66A320A2CFC5}
AppName=CoolSynth 808
AppVersion={#AppVersion}
AppVerName=CoolSynth 808 {#AppVersion}
AppPublisher=tomcoolpxl
AppPublisherURL=https://github.com/tomcoolpxl/CoolSynth808
AppSupportURL=https://github.com/tomcoolpxl/CoolSynth808/issues
AppUpdatesURL=https://github.com/tomcoolpxl/CoolSynth808/releases
DefaultDirName={autopf}\CoolSynth 808
DisableProgramGroupPage=yes
; The licence page shows the app's own MIT licence (the user's pick, D363); it ships beside the app as LICENSE.txt.
LicenseFile=..\LICENSE
OutputDir=..\build\installer
OutputBaseFilename=CoolSynth808-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayName=CoolSynth 808
; The 808's icon (the user's pick, D363), the one the app carries: for the installer and the Installed apps list.
SetupIconFile=..\resources\icon\CoolSynth808.ico
UninstallDisplayIcon={app}\CoolSynth808.ico
VersionInfoVersion={#AppVersion}
VersionInfoProductName=CoolSynth 808

[Types]
Name: "full"; Description: "Everything: the app and the VST3 and CLAP plugins"
Name: "app"; Description: "The app only"
Name: "vst3"; Description: "The VST3 plugin only"
; Each part its own tick box (CoolSynth's D367, as CoolSynth.iss)
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "app"; Description: "Standalone app"; Types: full app custom
Name: "vst3"; Description: "VST3 plugin, to play CoolSynth 808 in your music software (DAW)"; Types: full vst3 custom
Name: "clap"; Description: "CLAP plugin, for DAWs that support it: Bitwig, Reaper, FL Studio, Studio One"; Types: full custom

[Files]
Source: "{#BuildDir}\Standalone\CoolSynth 808.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#BuildDir}\VST3\CoolSynth 808.vst3\*"; DestDir: "{commoncf64}\VST3\CoolSynth 808.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
; The CLAP (CoolSynth's D367): one file, in Common Files\CLAP, where CLAP hosts look.
Source: "{#BuildDir}\CLAP\CoolSynth 808.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\icon\CoolSynth808.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\CoolSynth 808"; Filename: "{app}\CoolSynth 808.exe"; Components: app

[Run]
Filename: "{app}\CoolSynth 808.exe"; Description: "Start CoolSynth 808"; Components: app; Flags: nowait postinstall skipifsilent
; The guide on the web, where it shows as a page.
Filename: "https://github.com/tomcoolpxl/CoolSynth808#readme"; Description: "Open CoolSynth 808's guide on the web"; Flags: shellexec nowait postinstall skipifsilent unchecked
