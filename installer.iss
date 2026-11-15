#define MyAppName "AURA 天气"
#define MyAppVersion "1.4.0"
#define MyAppPublisher "AURA Weather"
#define MyAppExeName "AuraWeather.exe"

[Setup]
AppId={{8E6B2E6A-4D94-4F4E-9A57-2E4B1C7D1A40}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\AURA Weather
UsePreviousAppDir=yes
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=.
OutputBaseFilename=AuraWeather_Setup_v1.4.0
SetupIconFile=resources\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
AppMutex=Local\AuraWeather.SingleInstance
CloseApplications=yes
DirExistsWarning=no
AllowNoIcons=no

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Files]
Source: "AuraWeather.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "portable.flag"; DestDir: "{app}"; Flags: ignoreversion
Source: "resources\icon.ico"; DestDir: "{app}"; DestName: "icon.ico"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\AURA 天气"; Filename: "{app}\AuraWeather.exe"; WorkingDir: "{app}"; IconFilename: "{app}\icon.ico"
Name: "{autodesktop}\AURA 天气"; Filename: "{app}\AuraWeather.exe"; WorkingDir: "{app}"; IconFilename: "{app}\icon.ico"

[Run]
Filename: "{app}\AuraWeather.exe"; Description: "启动 AURA 天气"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data"
