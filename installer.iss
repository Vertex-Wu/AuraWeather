#define MyAppName "AURA 天气"
#define MyAppVersion "2.0.5"
#define MyAppPublisher "AURA Weather"
#define MyAppExeName "AuraWeather.exe"

[Setup]
AppId={{8E6B2E6A-4D94-4F4E-9A57-2E4B1C7D1A40}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion=2.0.5.0
VersionInfoProductVersion=2.0.5.0
DefaultDirName={autopf}\AURA Weather
UsePreviousAppDir=yes
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=.
OutputBaseFilename=AuraWeather_Setup_v2.0.5
SetupIconFile=resources\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
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

[Code]
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  Exec('cmd.exe', '/c tasklist /FI "IMAGENAME eq AuraWeather.exe" | find /I "AuraWeather.exe"',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if ResultCode = 0 then
  begin
    if MsgBox('检测到 AuraWeather 正在运行。点击“确定”将强制关闭它并继续安装，点击“取消”请先手动关闭程序。',
              mbConfirmation, MB_OKCANCEL) = IDOK then
      Exec('taskkill.exe', '/f /im AuraWeather.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
    else
    begin
      Result := False;
      Exit;
    end;
  end;
  Result := True;
end;


