; Laycue Windows installer (Inno Setup 6).
; Built by CI (.github/workflows/build-project.yaml, Windows job). CI passes:
;   AppVersion      e.g. 0.1.0
;   SourceDir       the CMake install folder, which contains laycue\bin\64bit\laycue.dll and laycue\data\...
;   OutputDir       where to write the installer
;   OutputBaseName  installer file name without .exe

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; Never change AppId: Windows uses it to recognise upgrades and uninstalls.
AppId={{1FAE074C-D24A-4020-91FC-9A1228945E33}
AppName=Laycue for OBS Studio
AppVersion={#AppVersion}
AppPublisher=Laycue
AppPublisherURL=https://github.com/wotgcls/Laycue
; OBS loads plugins from this machine-wide folder (OBS 28 and newer).
DefaultDirName={commonappdata}\obs-studio\plugins\laycue
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
; If OBS is open, the installer offers to close it (OBS keeps laycue.dll locked while running).
CloseApplications=yes
RestartApplications=no

[Messages]
WelcomeLabel2=This installs [name/ver] into OBS Studio.%n%nPlease save your work in OBS. If OBS is open, the installer will offer to close it.
FinishedLabel=Laycue is installed.%n%nStart OBS, then open the Laycue panel from the Docks menu: Docks > Laycue.

[Files]
Source: "{#SourceDir}\laycue\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
