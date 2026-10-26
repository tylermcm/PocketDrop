#define AppVersion GetVersionNumbersString("..\build\PocketDrop.exe")

[Setup]
AppId={{60430251-2449-43F2-A72D-10B8E2243A2A}
AppName=PocketDrop
AppVersion={#AppVersion}
AppPublisher=PocketDrop
AppPublisherURL=https://github.com/tylermcm/PocketDrop
AppUpdatesURL=https://github.com/tylermcm/PocketDrop/releases/latest
DefaultDirName={localappdata}\Programs\PocketDrop
DefaultGroupName=PocketDrop
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\build
OutputBaseFilename=PocketDrop-Setup
SetupIconFile=..\src\win\app.ico
UninstallDisplayIcon={app}\PocketDrop.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
AppMutex=Local\PocketDrop.SingleInstance
CloseApplications=yes
RestartApplications=no
DisableProgramGroupPage=yes

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked
Name: sendto; Description: "Add PocketDrop to the Send to menu"; Flags: unchecked

[Files]
Source: "..\build\PocketDrop.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\PocketDrop"; Filename: "{app}\PocketDrop.exe"
Name: "{autodesktop}\PocketDrop"; Filename: "{app}\PocketDrop.exe"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Windows\SendTo\PocketDrop"; Filename: "{app}\PocketDrop.exe"; Tasks: sendto

[Run]
Filename: "{app}\PocketDrop.exe"; Description: "Launch PocketDrop"; Flags: nowait postinstall skipifsilent

; Settings and Downloads/PocketDrop are deliberately preserved on uninstall.
