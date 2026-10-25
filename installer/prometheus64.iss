; Prometheus 64 installer (Inno Setup 6).
; Built by CI: iscc /DAppVersion=<version> /DStageDir=<folder with the app> installer\prometheus64.iss
; Installs for the current user only, so it needs no administrator rights.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "..\stage"
#endif

[Setup]
AppId={{8C1F5D2A-6B64-4E0F-9A64-50524F4D3634}
AppName=Prometheus 64
AppVersion={#AppVersion}
AppVerName=Prometheus 64 {#AppVersion}
AppPublisher=Project Prometheus
DefaultDirName={autopf}\Prometheus 64
DefaultGroupName=Prometheus 64
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Prometheus64-Setup-{#AppVersion}
SetupIconFile=..\assets\branding\prometheus64\prometheus64.ico
UninstallDisplayIcon={app}\Prometheus64.exe
UninstallDisplayName=Prometheus 64
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Prometheus 64"; Filename: "{app}\Prometheus64.exe"
Name: "{autodesktop}\Prometheus 64"; Filename: "{app}\Prometheus64.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Prometheus64.exe"; Description: "Start Prometheus 64"; Flags: nowait postinstall skipifsilent

; Saves (Documents\N64 Saves) and settings are the player's own and are left in place on uninstall.
