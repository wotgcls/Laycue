; Laycue Windows installer (Inno Setup 6).
; Built by CI (.github/workflows/build-project.yaml, Windows job). CI passes:
;   AppVersion      e.g. 0.1.0
; Laycue is a pack of four plugins; each is its own tick box so people install only what they want.
;   SourceDir       the CMake install folder, which contains laycue-scenes\bin\64bit\laycue-scenes.dll, laycue-scenes\data\..., and the same for laycue-titles, laycue-network and laycue-clock
;   OutputDir       where to write the installer
;   OutputBaseName  installer file name without .exe

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; Never change AppId: Windows uses it to recognise upgrades and uninstalls.
AppId={{1FAE074C-D24A-4020-91FC-9A1228945E33}
AppName=Laycue for OBS Studio (Scenes, Titles, Network, Clock)
AppVersion={#AppVersion}
AppPublisher=Laycue
AppPublisherURL=https://github.com/wotgcls/Laycue
; OBS loads plugins from this machine-wide folder (OBS 28 and newer).
DefaultDirName={commonappdata}\obs-studio\plugins
DisableDirPage=yes
; Always install to the plugins folder itself. Without this, an upgrade from the old single-plugin installer
; reuses its old folder (plugins\laycue) and the new plugins end up one level too deep for OBS to find.
UsePreviousAppDir=no
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
FinishedLabel=Laycue is installed.%n%nStart OBS, then open the panels from the Docks menu: Laycue Scenes, Titles, Network, Clock, Scripture and Lyrics (or the Laycue menu).

[Types]
Name: "full"; Description: "Everything (Scenes, Titles, Network, Clock, Scripture and Lyrics)"
Name: "custom"; Description: "Choose which to install"; Flags: iscustom

[Components]
Name: "scenes"; Description: "Laycue Scenes: layouts and automatic overlay rules"; Types: full custom
Name: "titles"; Description: "Laycue Titles: 68 lower thirds, tickers and logo animations"; Types: full custom
Name: "network"; Description: "Laycue Network: bitrate advice and auto-adjust"; Types: full custom
Name: "clock"; Description: "Laycue Clock: countdowns and timers, on video or on a background"; Types: full custom
Name: "scripture"; Description: "Laycue Scripture: type a verse, show it (King James Version included)"; Types: full custom
Name: "lyrics"; Description: "Laycue Lyrics: type a song name, show it two lines at a time"; Types: full custom

[Files]
Source: "{#SourceDir}\laycue-scenes\*"; DestDir: "{app}\laycue-scenes"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: scenes
Source: "{#SourceDir}\laycue-titles\*"; DestDir: "{app}\laycue-titles"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: titles
Source: "{#SourceDir}\laycue-network\*"; DestDir: "{app}\laycue-network"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: network
Source: "{#SourceDir}\laycue-clock\*"; DestDir: "{app}\laycue-clock"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: clock
Source: "{#SourceDir}\laycue-scripture\*"; DestDir: "{app}\laycue-scripture"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: scripture
Source: "{#SourceDir}\laycue-lyrics\*"; DestDir: "{app}\laycue-lyrics"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: lyrics

[InstallDelete]
; Earlier versions installed a single "laycue" folder; remove it so OBS does not load two copies.
Type: filesandordirs; Name: "{app}\laycue"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\laycue-scenes"
Type: filesandordirs; Name: "{app}\laycue-titles"
Type: filesandordirs; Name: "{app}\laycue-network"
Type: filesandordirs; Name: "{app}\laycue-clock"
Type: filesandordirs; Name: "{app}\laycue-scripture"
Type: filesandordirs; Name: "{app}\laycue-lyrics"
