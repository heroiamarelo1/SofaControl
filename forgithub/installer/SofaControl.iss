; SofaControl installer (Inno Setup 6)
; Produces a single SofaControl-Setup.exe that bundles the app, its assets,
; and the ViGEmBus + HidHide drivers.

#define MyAppName "SofaControl"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "HeroiAmarelo"
#define MyAppURL "https://www.paypal.com/paypalme/heroiamarelo"
#define MyAppExeName "SofaControl.exe"
#define MyGuardExeName "SofaControlGuard.exe"

#define ViGEmExe "ViGEmBus_1.22.0_x64_x86_arm64.exe"
#define HidHideExe "HidHide_1.5.230_x64.exe"

[Setup]
AppId={{6B8C9E2A-3F4D-4A1B-9C7E-1D2A5F6B8C90}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=Output
OutputBaseFilename=SofaControl-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\assets\SofaControl.ico
LicenseFile=..\LICENSE
InfoBeforeFile=about.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "..\build\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\{#MyGuardExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
; Drivers: extracted to a temp folder, run during install, then removed.
Source: "redist\{#ViGEmExe}"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "redist\{#HidHideExe}"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "command_list_installer.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "mark_shortcuts_admin.ps1"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "register_startup_task.ps1"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\{#ViGEmExe}"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Installing ViGEmBus driver (virtual Xbox 360 controller)..."; \
    Flags: waituntilterminated; Check: NeedViGEmBus
Filename: "{tmp}\{#HidHideExe}"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Installing HidHide driver (hides the controller from games)..."; \
    Flags: waituntilterminated; Check: NeedHidHide
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{tmp}\mark_shortcuts_admin.ps1"" ""{group}\{#MyAppName}.lnk"" ""{autodesktop}\{#MyAppName}.lnk"""; \
    StatusMsg: "Marking shortcuts to run SofaControl as administrator..."; \
    Flags: runhidden waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "Launch SofaControl now"; \
    Flags: nowait postinstall skipifsilent

[Code]
var
  GNeedViGEm: Boolean;
  GNeedHidHide: Boolean;
  CommandPage: TWizardPage;

function Quote(const S: string): string;
begin
  Result := '"' + S + '"';
end;

procedure DeleteLegacyStartupRegistry();
begin
  RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'SofaControl');
end;

procedure DeleteStartupTask();
var
  ResultCode: Integer;
begin
  Exec(
    ExpandConstant('{sys}\schtasks.exe'),
    '/Delete /TN "SofaControl" /F',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode);
end;

procedure DeleteGuardService();
var
  ResultCode: Integer;
begin
  Exec(
    ExpandConstant('{sys}\sc.exe'),
    'stop SofaControlGuard',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode);

  Exec(
    ExpandConstant('{sys}\sc.exe'),
    'delete SofaControlGuard',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode);
end;

procedure CreateGuardService();
var
  ResultCode: Integer;
  Params: string;
begin
  DeleteGuardService();

  Params :=
    'create SofaControlGuard binPath= ' +
    Quote(ExpandConstant('{app}\{#MyGuardExeName}')) +
    ' start= auto depend= HidHide DisplayName= "SofaControl HidHide Guard"';

  Exec(
    ExpandConstant('{sys}\sc.exe'),
    Params,
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode);

  Exec(
    ExpandConstant('{sys}\sc.exe'),
    'description SofaControlGuard "Applies remembered HidHide controller protection before the user session starts."',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode);

  Exec(
    ExpandConstant('{sys}\sc.exe'),
    'start SofaControlGuard',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode);
end;

procedure CreateStartupTask();
var
  ResultCode: Integer;
  Params: string;
begin
  Params :=
    '-NoProfile -ExecutionPolicy Bypass -File ' +
    Quote(ExpandConstant('{tmp}\register_startup_task.ps1')) +
    ' ' +
    Quote(ExpandConstant('{app}\{#MyAppExeName}'));

  Exec(
    ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    Params,
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode);
end;

procedure MarkShowOnNextLaunch();
var
  ConfigDir: string;
begin
  ConfigDir := ExpandConstant('{userappdata}\SofaControl');
  ForceDirectories(ConfigDir);
  SetIniString('SofaControl', 'ShowOnNextLaunch', '1', ConfigDir + '\config.ini');
end;

function ServiceInstalled(const Name: string): Boolean;
begin
  Result := RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\' + Name);
end;

function InitializeSetup(): Boolean;
begin
  GNeedViGEm := not ServiceInstalled('ViGEmBus');
  GNeedHidHide := not ServiceInstalled('HidHide');
  Result := True;
end;

function NeedViGEmBus(): Boolean;
begin
  Result := GNeedViGEm;
end;

function NeedHidHide(): Boolean;
begin
  Result := GNeedHidHide;
end;

procedure InitializeWizard();
var
  Image: TBitmapImage;
begin
  CommandPage := CreateCustomPage(wpInfoBefore, 'Command List', 'Main SofaControl controller commands');
  ExtractTemporaryFile('command_list_installer.bmp');

  Image := TBitmapImage.Create(CommandPage);
  Image.Parent := CommandPage.Surface;
  Image.Left := 0;
  Image.Top := 0;
  Image.Width := CommandPage.SurfaceWidth;
  Image.Height := CommandPage.SurfaceHeight;
  Image.Stretch := True;
  Image.Bitmap.LoadFromFile(ExpandConstant('{tmp}\command_list_installer.bmp'));
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
  begin
    if GNeedViGEm or GNeedHidHide then
      WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + Chr(13) + Chr(10) + Chr(13) + Chr(10) + 'A controller driver was installed. If game mode does not work right away, please restart Windows once.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    CreateGuardService();
    DeleteLegacyStartupRegistry();
    DeleteStartupTask();
    CreateStartupTask();
    MarkShowOnNextLaunch();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    DeleteGuardService();
    DeleteStartupTask();
    DeleteLegacyStartupRegistry();
  end;
end;
