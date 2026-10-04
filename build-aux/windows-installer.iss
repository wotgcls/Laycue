; Laycue Windows installer (Inno Setup 6).
; Built by CI (.github/workflows/build-project.yaml, Windows job). CI passes:
;   AppVersion      e.g. 0.1.0
; Laycue is a pack of three plugins; each is its own tick box so people install only what they want.
;   SourceDir       the CMake install folder, which contains laycue-scenes\bin\64bit\laycue-scenes.dll, laycue-scenes\data\..., and the same for laycue-titles and laycue-network
;   OutputDir       where to write the installer
;   OutputBaseName  installer file name without .exe

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; Never change AppId: Windows uses it to recognise upgrades and uninstalls.
AppId={{1FAE074C-D24A-4020-91FC-9A1228945E33}
AppName=Laycue for OBS Studio (Scenes, Titles, Network)
AppVersion={#AppVersion}
AppPublisher=Laycue
AppPublisherURL=https://github.com/wotgcls/Laycue
; OBS loads plugins from this machine-wide folder (OBS 28 and newer).
DefaultDirName={commonappdata}\obs-studio\plugins
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Laycue for OBS Studio
; Laycue brand icon (assets/brand/laycue.ico); the uninstaller shows the copy installed under data\brand.
SetupIconFile=..\assets\brand\laycue.ico
UninstallDisplayIcon={uninstallexe}
; If OBS is open, the installer offers to close it (OBS keeps laycue.dll locked while running).
CloseApplications=yes
RestartApplications=no

[Messages]
WelcomeLabel2=This installs [name/ver] into OBS Studio.%n%nPlease save your work in OBS. If OBS is open, the installer will offer to close it.
FinishedLabel=Laycue is installed.%n%nStart OBS, then open the panels from the Docks menu: Laycue Scenes, Laycue Titles and Laycue Network.

[Types]
Name: "full"; Description: "Everything (Scenes, Titles and Network)"
Name: "custom"; Description: "Choose which to install"; Flags: iscustom

[Components]
Name: "scenes"; Description: "Laycue Scenes: layouts and automatic overlay rules"; Types: full custom
Name: "titles"; Description: "Laycue Titles: 20 live lower thirds and tickers"; Types: full custom
Name: "network"; Description: "Laycue Network: bitrate advice and auto-adjust"; Types: full custom

[Files]
Source: "{#SourceDir}\laycue-scenes\*"; DestDir: "{app}\laycue-scenes"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: scenes
Source: "{#SourceDir}\laycue-titles\*"; DestDir: "{app}\laycue-titles"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: titles
Source: "{#SourceDir}\laycue-network\*"; DestDir: "{app}\laycue-network"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: network

[InstallDelete]
; Earlier versions installed a single "laycue" folder; remove it so OBS does not load two copies.
Type: filesandordirs; Name: "{app}\laycue"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\laycue-scenes"
Type: filesandordirs; Name: "{app}\laycue-titles"
Type: filesandordirs; Name: "{app}\laycue-network"
