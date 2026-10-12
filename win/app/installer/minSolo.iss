; minSolo — Windows installer (Inno Setup 6), the same shape as QCBridgeN's.
;
;   installer\build-setup.cmd    (stages build\minSolo.exe, then this)
;
; One statically linked exe under Program Files\minSolo; no runtime to install.
; Unsigned for the first releases: SmartScreen shows "More info → Run anyway".

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#define MyAppName "minSolo"
#define MyAppPublisher "cbkow"
#define MyAppExeName "minSolo.exe"
#define StageDir "..\build\stage"

[Setup]
AppId={{B7E2C4A1-5D63-4F0B-9A2E-6C1F0D3E8A47}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/cbkow/minSolo
AppSupportURL=https://github.com/cbkow/minSolo/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\..\..\dist
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Setup-x64
SetupIconFile=..\res\minSolo.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
WizardStyle=modern
LicenseFile=..\..\..\LICENSE
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes

[Tasks]
Name: "startup"; Description: "Start {#MyAppName} when you sign in"; GroupDescription: "Startup:"

[Files]
Source: "{#StageDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\..\PRIVACY_POLICY.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Registry]
; Same value the app's own "Launch at login" setting writes, so the two agree.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Ask a running copy to quit (it releases every mute) before the files go.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "quit"

[Code]
// A tray app has no window for CloseApplications to close: ask it to quit ourselves.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Exe: String;
  Code: Integer;
begin
  Exe := ExpandConstant('{app}\{#MyAppExeName}');
  if FileExists(Exe) then
  begin
    Exec(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
    Sleep(1500);
  end;
  Result := '';
end;
