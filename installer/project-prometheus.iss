; Project Prometheus installer (Inno Setup 6).
; Built by CI: iscc /DAppVersion=<version> /DStageDir=<folder with the app> installer\project-prometheus.iss
; Installs for the current user only, so it needs no administrator rights.

#ifndef AppVersion
  #define AppVersion "0.2.1"
#endif
#ifndef StageDir
  #define StageDir "..\stage"
#endif

[Setup]
AppId={{8C1F5D2A-6B64-4E0F-9A64-50524F4D3634}
AppName=Project Prometheus
AppVersion={#AppVersion}
AppVerName=Project Prometheus {#AppVersion}
AppPublisher=Wolfsbane Co.
AppPublisherURL=https://wolfsbanecompany.com/apps/
AppSupportURL=https://wolfsbanecompany.com/apps/prometheus/
DefaultDirName={autopf}\Project Prometheus
; The app was called Prometheus 64 at first (same AppId): install into the new folder, not that one.
UsePreviousAppDir=no
DefaultGroupName=Project Prometheus
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=ProjectPrometheus-Setup-{#AppVersion}
SetupIconFile=..\assets\branding\prometheus\prometheus.ico
UninstallDisplayIcon={app}\ProjectPrometheus.exe
UninstallDisplayName=Project Prometheus
WizardStyle=modern
; The terms of use: free only, no games included, not affiliated, no warranty. Accepted before installing.
LicenseFile=TERMS.txt
Compression=lzma2/max
SolidCompression=yes
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "associate"; Description: "Open game files (.z64, .n64, .v64, .gba) with Project Prometheus when double-clicked"; GroupDescription: "Game files:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; What the app installed while it was called Prometheus 64 (its program files and shortcuts only;
; saves and settings live elsewhere and are kept).
Type: filesandordirs; Name: "{autopf}\Prometheus 64"
Type: files; Name: "{autoprograms}\Prometheus 64.lnk"
Type: files; Name: "{autodesktop}\Prometheus 64.lnk"

[Icons]
Name: "{autoprograms}\Project Prometheus"; Filename: "{app}\ProjectPrometheus.exe"
Name: "{autodesktop}\Project Prometheus"; Filename: "{app}\ProjectPrometheus.exe"; Tasks: desktopicon

[Registry]
; Game files: Project Prometheus becomes their program (if Windows asks once, pick Project Prometheus and tick
; "Always"). Archives (.zip, .7z, .rar) only get it as a choice under "Open with": their own program
; stays the same. Everything is per user and removed on uninstall.
Root: HKA; Subkey: "Software\Classes\Prometheus64.Game"; ValueType: string; ValueName: ""; ValueData: "Game"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Prometheus64.Game\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\ProjectPrometheus.exe,0"
Root: HKA; Subkey: "Software\Classes\Prometheus64.Game\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\ProjectPrometheus.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\.z64"; ValueType: string; ValueName: ""; ValueData: "Prometheus64.Game"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.n64"; ValueType: string; ValueName: ""; ValueData: "Prometheus64.Game"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.v64"; ValueType: string; ValueName: ""; ValueData: "Prometheus64.Game"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.gba"; ValueType: string; ValueName: ""; ValueData: "Prometheus64.Game"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.z64\OpenWithProgids"; ValueType: string; ValueName: "Prometheus64.Game"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.n64\OpenWithProgids"; ValueType: string; ValueName: "Prometheus64.Game"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.v64\OpenWithProgids"; ValueType: string; ValueName: "Prometheus64.Game"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.gba\OpenWithProgids"; ValueType: string; ValueName: "Prometheus64.Game"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.zip\OpenWithProgids"; ValueType: string; ValueName: "Prometheus64.Game"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.7z\OpenWithProgids"; ValueType: string; ValueName: "Prometheus64.Game"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.rar\OpenWithProgids"; ValueType: string; ValueName: "Prometheus64.Game"; ValueData: ""; Flags: uninsdeletevalue
; Online key files (.prometheuskey, shared by every Project Prometheus app): double-clicking one
; adds the group's key, so the player can host and join online games.
Root: HKA; Subkey: "Software\Classes\.prometheuskey"; ValueType: string; ValueName: ""; ValueData: "ProjectPrometheus.OnlineKey"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\ProjectPrometheus.OnlineKey"; ValueType: string; ValueName: ""; ValueData: "Project Prometheus online key"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\ProjectPrometheus.OnlineKey\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\ProjectPrometheus.exe,0"
Root: HKA; Subkey: "Software\Classes\ProjectPrometheus.OnlineKey\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\ProjectPrometheus.exe"" ""%1"""

[Run]
Filename: "{app}\ProjectPrometheus.exe"; Description: "Start Project Prometheus"; Flags: nowait postinstall skipifsilent

; Saves (Documents\Memories, or Documents\N64 Saves from earlier versions) and settings are the player's own and are left in place on uninstall.
