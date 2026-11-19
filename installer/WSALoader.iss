; WSALoader setup (Inno Setup 7)
; Crafted by Will0wn - https://github.com/Will0wn
#define AppVer "0.4.1"
#define Src "..\Custom"

[Setup]
AppId={{6F1D2B7A-3C4E-4F8A-9B2D-7E5C1A9F0D34}
AppName=WSALoader
AppVersion={#AppVer}
AppPublisher=Will0wn
AppPublisherURL=https://github.com/Will0wn
AppSupportURL=https://github.com/Will0wn
AppCopyright=Crafted by Will0wn
VersionInfoCompany=Will0wn
VersionInfoCopyright=Crafted by Will0wn
VersionInfoDescription=WSALoader setup
SetupArchitecture=x64
PrivilegesRequired=admin
; the "application folder" is the WSA folder: files go into its Custom subfolder
DefaultDirName={commonpf}\Windows Subsystem for Android
AppendDefaultDirName=no
DirExistsWarning=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
UninstallFilesDir={app}\Custom\WSALoader
UninstallDisplayName=WSALoader
UninstallDisplayIcon={app}\Custom\WSALoader\WSALoader.ico
CloseApplications=no
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir=Output
OutputBaseFilename=WSALoader-Setup-{#AppVer}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nWSALoader makes Windows Subsystem for Android load mods. WSA will be closed during the installation.%n%nCrafted by Will0wn - github.com/Will0wn
SelectDirDesc=Where is Windows Subsystem for Android installed?
SelectDirLabel3=Select the folder that contains WsaClient. WSALoader and its mods are installed into its Custom subfolder.

[Types]
Name: "full"; Description: "WSALoader + WSASmartTV"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "loader"; Description: "WSALoader (required)"; Types: full custom; Flags: fixed
Name: "smarttv"; Description: "WSASmartTV - Android scale, fullscreen apps, phone remote"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut that starts WSA (opens the Home launcher)"; GroupDescription: "Shortcuts:"

[Icons]
; the WindowsApps alias starts WsaClient with its package identity, like the Start menu shortcuts
Name: "{userdesktop}\Android TV"; Filename: "{localappdata}\Microsoft\WindowsApps\MicrosoftCorporationII.WindowsSubsystemForAndroid_8wekyb3d8bbwe\WsaClient.exe"; Parameters: "/launch wsa://{code:HomePackage}"; IconFilename: "{app}\Custom\WSALoader\AndroidTV.ico"; Comment: "Start Windows Subsystem for Android"; Tasks: desktopicon

[InstallDelete]
; files of the old layout (everything directly in Custom)
; desktop shortcut of 0.3.0, renamed "Android TV"
Type: files; Name: "{userdesktop}\WSA.lnk"
Type: files; Name: "{app}\Custom\WsaAddon.dll"
Type: files; Name: "{app}\Custom\gfxstream_proxy.dll"
Type: files; Name: "{app}\Custom\install.ps1"
Type: files; Name: "{app}\Custom\uninstall.ps1"
Type: files; Name: "{app}\Custom\Installa.cmd"
Type: files; Name: "{app}\Custom\Disinstalla.cmd"
Type: files; Name: "{app}\Custom\README.md"
Type: files; Name: "{app}\Custom\telecomando.html"
Type: files; Name: "{app}\Custom\config.ini"
Type: files; Name: "{app}\Custom\WSALoader\Installa.cmd"
Type: files; Name: "{app}\Custom\WSALoader\Disinstalla.cmd"
Type: files; Name: "{app}\Custom\WSASmartTV\telecomando.html"
; WSASmartTV deselected on an update: remove it
Type: filesandordirs; Name: "{app}\Custom\WSASmartTV"; Components: not smarttv

[Files]
Source: "{#Src}\WSALoader\WSALoader.dll"; DestDir: "{app}\Custom\WSALoader"; Components: loader; Flags: ignoreversion
Source: "{#Src}\WSALoader\install.ps1"; DestDir: "{app}\Custom\WSALoader"; Components: loader; Flags: ignoreversion
Source: "{#Src}\WSALoader\uninstall.ps1"; DestDir: "{app}\Custom\WSALoader"; Components: loader; Flags: ignoreversion
Source: "{#Src}\WSALoader\README.md"; DestDir: "{app}\Custom\WSALoader"; Components: loader; Flags: ignoreversion
Source: "{#Src}\WSALoader\WSALoader.ico"; DestDir: "{app}\Custom\WSALoader"; Components: loader; Flags: ignoreversion
Source: "{#Src}\WSALoader\AndroidTV.ico"; DestDir: "{app}\Custom\WSALoader"; Components: loader; Flags: ignoreversion
Source: "{#Src}\WSASmartTV\WSASmartTV.dll"; DestDir: "{app}\Custom\WSASmartTV"; Components: smarttv; Flags: ignoreversion
Source: "{#Src}\WSASmartTV\remote.html"; DestDir: "{app}\Custom\WSASmartTV"; Components: smarttv; Flags: ignoreversion
Source: "{#Src}\WSASmartTV\README.md"; DestDir: "{app}\Custom\WSASmartTV"; Components: smarttv; Flags: ignoreversion
; the user's settings are kept on update; the setup page writes the chosen values
Source: "{#Src}\WSASmartTV\config.ini"; DestDir: "{app}\Custom\WSASmartTV"; Components: smarttv; Flags: onlyifdoesntexist

[UninstallRun]
; puts the original gfxstream_backend.dll back before the files are removed
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\Custom\WSALoader\uninstall.ps1"""; Flags: runhidden waituntilterminated; RunOnceId: "RestoreGfxstream"

[UninstallDelete]
Type: files; Name: "{app}\Custom\WSASmartTV\config.ini"
Type: dirifempty; Name: "{app}\Custom\WSASmartTV"
Type: dirifempty; Name: "{app}\Custom\WSALoader"
Type: dirifempty; Name: "{app}\Custom"

[Code]
var
  TvPage, RemotePage: TWizardPage;
  DensityBox: TNewComboBox;
  StandaloneRadio, ExternalRadio, OffRadio: TNewRadioButton;
  PinLabel, ServerLabel, TokenLabel: TNewStaticText;
  PinEdit, ServerEdit, TokenEdit: TNewEdit;
  DensityValues: TArrayOfString;
  RemotePort, PcIp: String;
  SettingsLoaded, LoaderFailed: Boolean;
  // maintenance of an existing installation
  InstalledVersion, InstalledDir: String;
  MaintenanceButton: TNewButton;
  UpdateMode, ExitQuietly: Boolean;

const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{6F1D2B7A-3C4E-4F8A-9B2D-7E5C1A9F0D34}_is1';

procedure AddDensity(const Value, Caption: String);
var
  N: Integer;
begin
  N := GetArrayLength(DensityValues);
  SetArrayLength(DensityValues, N + 1);
  DensityValues[N] := Value;
  DensityBox.Items.Add(Caption);
end;

procedure RemoteClick(Sender: TObject);
begin
  PinLabel.Enabled := StandaloneRadio.Checked;
  PinEdit.Enabled := StandaloneRadio.Checked;
  ServerLabel.Enabled := ExternalRadio.Checked;
  ServerEdit.Enabled := ExternalRadio.Checked;
  TokenLabel.Enabled := ExternalRadio.Checked;
  TokenEdit.Enabled := ExternalRadio.Checked;
end;

function NewLabelOn(Page: TWizardPage; const Caption: String; Left, Top: Integer): TNewStaticText;
begin
  Result := TNewStaticText.Create(Page);
  Result.Parent := Page.Surface;
  Result.Caption := Caption;
  Result.Left := Left;
  Result.Top := Top;
  Result.Width := Page.SurfaceWidth - Left;
  Result.WordWrap := True;
end;

function NewLabel(const Caption: String; Top: Integer): TNewStaticText;
begin
  Result := NewLabelOn(TvPage, Caption, 0, Top);
end;

function NewEdit(Left, Top, Width, MaxLength: Integer): TNewEdit;
begin
  Result := TNewEdit.Create(RemotePage);
  Result.Parent := RemotePage.Surface;
  Result.Left := Left;
  Result.Top := Top;
  Result.Width := Width;
  Result.MaxLength := MaxLength;
end;

function NewRadio(const Caption: String; Top: Integer): TNewRadioButton;
begin
  Result := TNewRadioButton.Create(RemotePage);
  Result.Parent := RemotePage.Surface;
  Result.Caption := Caption;
  Result.Top := Top;
  Result.Width := RemotePage.SurfaceWidth;
  Result.Height := ScaleY(17);
  Result.OnClick := @RemoteClick;
end;

procedure InitializeWizard;
var
  L: TNewStaticText;
begin
  TvPage := CreateCustomPage(wpSelectComponents, 'WSASmartTV settings',
    'Android scale. You can change it later in Custom\WSASmartTV\config.ini.');

  L := NewLabel('Android UI scale (density):', 0);
  DensityBox := TNewComboBox.Create(TvPage);
  DensityBox.Parent := TvPage.Surface;
  DensityBox.Style := csDropDownList;
  DensityBox.Top := L.Top + L.Height + ScaleY(6);
  DensityBox.Width := ScaleX(300);
  AddDensity('240', '240 - small');
  AddDensity('320', '320');
  AddDensity('400', '400');
  AddDensity('480', '480 - medium');
  AddDensity('560', '560 - large (recommended for a TV)');
  AddDensity('640', '640 - extra large');
  AddDensity('0', 'Same as Windows');
  NewLabel('Higher values make everything bigger.', DensityBox.Top + DensityBox.Height + ScaleY(4));

  RemotePage := CreateCustomPage(TvPage.ID, 'Phone remote',
    'How the remote reaches WSA. You can change it later in Custom\WSASmartTV\config.ini.');

  StandaloneRadio := NewRadio('Standalone: this PC serves the remote page on your home network', 0);
  PinLabel := NewLabelOn(RemotePage, 'PIN (4 to 12 digits):', ScaleX(18), StandaloneRadio.Top + StandaloneRadio.Height + ScaleY(6));
  PinEdit := NewEdit(ScaleX(18), PinLabel.Top + PinLabel.Height + ScaleY(4), ScaleX(120), 12);

  ExternalRadio := NewRadio('External: WSA connects to your remote server, no port is opened on this PC',
    PinEdit.Top + PinEdit.Height + ScaleY(16));
  ServerLabel := NewLabelOn(RemotePage, 'Server address (ws:// or wss://):', ScaleX(18), ExternalRadio.Top + ExternalRadio.Height + ScaleY(6));
  ServerEdit := NewEdit(ScaleX(18), ServerLabel.Top + ServerLabel.Height + ScaleY(4), RemotePage.SurfaceWidth - ScaleX(18), 500);
  TokenLabel := NewLabelOn(RemotePage, 'Token (the same one set on the server, at least 8 characters):', ScaleX(18), ServerEdit.Top + ServerEdit.Height + ScaleY(8));
  TokenEdit := NewEdit(ScaleX(18), TokenLabel.Top + TokenLabel.Height + ScaleY(4), ScaleX(240), 120);

  OffRadio := NewRadio('Off', TokenEdit.Top + TokenEdit.Height + ScaleY(16));

  NewLabelOn(RemotePage, 'The remote works only while WSA is running.', 0, OffRadio.Top + OffRadio.Height + ScaleY(12));
end;

function ConfigFile: String;
begin
  Result := AddBackslash(WizardDirValue) + 'Custom\WSASmartTV\config.ini';
end;

// fills the page from the existing config.ini (update) or with the defaults
procedure LoadSettings;
var
  Density, Port, Mode: String;
  I: Integer;
begin
  Density := '560';
  RemotePort := '8580';
  Mode := 'standalone';
  PinEdit.Text := '';
  if FileExists(ConfigFile) then begin
    Density := GetIniString('WSASmartTV', 'Density', '560', ConfigFile);
    Port := GetIniString('WSASmartTV', 'RemotePort', '8580', ConfigFile);
    if Port <> '0' then RemotePort := Port;
    // config of 0.3.x has no RemoteMode: RemotePort=0 meant off
    Mode := Lowercase(GetIniString('WSASmartTV', 'RemoteMode', '', ConfigFile));
    if Mode = '' then
      if Port = '0' then Mode := 'off' else Mode := 'standalone';
    PinEdit.Text := GetIniString('WSASmartTV', 'RemotePin', '', ConfigFile);
    ServerEdit.Text := GetIniString('WSASmartTV', 'RemoteServer', '', ConfigFile);
    TokenEdit.Text := GetIniString('WSASmartTV', 'RemoteToken', '', ConfigFile);
  end;
  StandaloneRadio.Checked := (Mode <> 'external') and (Mode <> 'off');
  ExternalRadio.Checked := Mode = 'external';
  OffRadio.Checked := Mode = 'off';
  DensityBox.ItemIndex := -1;
  for I := 0 to GetArrayLength(DensityValues) - 1 do
    if DensityValues[I] = Density then DensityBox.ItemIndex := I;
  if DensityBox.ItemIndex < 0 then begin
    AddDensity(Density, Density + ' (current)');
    DensityBox.ItemIndex := GetArrayLength(DensityValues) - 1;
  end;
  RemoteClick(nil);
end;

function ValidPin(const Pin: String): Boolean;
var
  I: Integer;
begin
  Result := (Length(Pin) >= 4) and (Length(Pin) <= 12);
  for I := 1 to Length(Pin) do
    if (Pin[I] < '0') or (Pin[I] > '9') then Result := False;
end;

function ValidServer(const Url: String): Boolean;
var
  U: String;
begin
  U := Lowercase(Url);
  Result := ((Pos('ws://', U) = 1) and (Length(U) > 5)) or ((Pos('wss://', U) = 1) and (Length(U) > 6));
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := ((PageID = TvPage.ID) or (PageID = RemotePage.ID)) and not WizardIsComponentSelected('smarttv');
  // Update goes straight to the installation with the previous folder, components and tasks
  if UpdateMode and ((PageID = wpSelectDir) or (PageID = wpSelectComponents) or (PageID = TvPage.ID) or
     (PageID = RemotePage.ID) or (PageID = wpSelectTasks) or (PageID = wpReady)) then Result := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and not FileExists(AddBackslash(WizardDirValue) + 'WsaClient\WsaClient.exe') then begin
    MsgBox('WsaClient.exe was not found in this folder.' + #13#10 +
      'Select the folder where Windows Subsystem for Android is installed.', mbError, MB_OK);
    Result := False;
  end;
  if (CurPageID = RemotePage.ID) and StandaloneRadio.Checked and not ValidPin(Trim(PinEdit.Text)) then begin
    MsgBox('Choose a remote PIN of 4 to 12 digits.', mbError, MB_OK);
    Result := False;
  end;
  if (CurPageID = RemotePage.ID) and ExternalRadio.Checked then begin
    if not ValidServer(Trim(ServerEdit.Text)) then begin
      MsgBox('Enter the server address, starting with ws:// or wss://' + #13#10 +
        '(for example ws://192.168.1.10:8580/wsa).', mbError, MB_OK);
      Result := False;
    end else if (Length(Trim(TokenEdit.Text)) < 8) or (Pos(' ', Trim(TokenEdit.Text)) > 0) then begin
      MsgBox('Enter the token set on the server: at least 8 characters, no spaces.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

// WsaClient must be closed before its DLLs are replaced (it is the only process that loads them;
// running WsaClient.exe /shutdown directly, outside its package, fails with error 0xc00004bc)
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM WsaClient.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(1000);
end;

// launcher opened by the desktop shortcut: RemoteHome of WSASmartTV, Projectivy otherwise
function HomePackage(Param: String): String;
begin
  Result := GetIniString('WSASmartTV', 'RemoteHome', 'com.spocky.projengmenu',
    ExpandConstant('{app}\Custom\WSASmartTV\config.ini'));
end;

procedure SaveSettings;
var
  Cfg: String;
begin
  Cfg := ExpandConstant('{app}\Custom\WSASmartTV\config.ini');
  SetIniString('WSASmartTV', 'Density', DensityValues[DensityBox.ItemIndex], Cfg);
  // RemotePort stays a real port: the mode decides whether the remote is on
  SetIniString('WSASmartTV', 'RemotePort', RemotePort, Cfg);
  if StandaloneRadio.Checked then begin
    SetIniString('WSASmartTV', 'RemoteMode', 'standalone', Cfg);
    SetIniString('WSASmartTV', 'RemotePin', Trim(PinEdit.Text), Cfg);
  end else if ExternalRadio.Checked then begin
    SetIniString('WSASmartTV', 'RemoteMode', 'external', Cfg);
    SetIniString('WSASmartTV', 'RemoteServer', Trim(ServerEdit.Text), Cfg);
    SetIniString('WSASmartTV', 'RemoteToken', Trim(TokenEdit.Text), Cfg);
  end else
    SetIniString('WSASmartTV', 'RemoteMode', 'off', Cfg);
end;

// install.ps1 tests the loader inside the WSA folder and only then puts it in place
function RunInstallScript(const Dir: String; var Output: AnsiString): Integer;
var
  Log, Params: String;
begin
  Log := ExpandConstant('{tmp}\wsaloader-install.log');
  DeleteFile(Log);
  Params := '-NoProfile -ExecutionPolicy Bypass -File "' + AddBackslash(Dir) + 'Custom\WSALoader\install.ps1' +
    '" -Log "' + Log + '"';
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Params, '', SW_HIDE,
              ewWaitUntilTerminated, Result) then Result := -1;
  Output := '';
  LoadStringFromFile(Log, Output);
end;

procedure ActivateLoader;
var
  Output: AnsiString;
  Code, P: Integer;
begin
  Code := RunInstallScript(ExpandConstant('{app}'), Output);
  if Code <> 0 then begin
    LoaderFailed := True;
    MsgBox('WSALoader could not be activated. WSA was not modified.' + #13#10#13#10 + String(Output), mbError, MB_OK);
    Exit;
  end;
  P := Pos('PC IP: ', String(Output));
  if P > 0 then begin
    PcIp := Copy(String(Output), P + 7, 40);
    P := Pos(#13, PcIp);
    if P > 0 then PcIp := Copy(PcIp, 1, P - 1);
    PcIp := Trim(PcIp);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    // Update keeps config.ini as it is
    if WizardIsComponentSelected('smarttv') and not UpdateMode then SaveSettings;
    ActivateLoader;
  end;
end;

// ---- maintenance of an existing installation: Update, Repair, Uninstall ----

procedure DetectInstallation;
begin
  InstalledVersion := '';
  InstalledDir := '';
  if RegQueryStringValue(HKLM, UninstallKey, 'Inno Setup: App Path', InstalledDir) and
     FileExists(AddBackslash(InstalledDir) + 'Custom\WSALoader\install.ps1') then
    RegQueryStringValue(HKLM, UninstallKey, 'DisplayVersion', InstalledVersion);
end;

procedure CloseSetup;
begin
  ExitQuietly := True;
  WizardForm.Close;
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if ExitQuietly then Confirm := False;
end;

procedure DoUpdate;
begin
  // the finish page describes the remote from the current settings
  if not SettingsLoaded then begin
    LoadSettings;
    SettingsLoaded := True;
  end;
  UpdateMode := True;
  MaintenanceButton.Visible := False;
  WizardForm.NextButton.OnClick(WizardForm.NextButton);
end;

procedure DoRepair;
var
  Dll: AnsiString;
  Output: AnsiString;
begin
  if LoadStringFromFile(AddBackslash(InstalledDir) + 'gfxstream_backend.dll', Dll) and
     (Pos('gfxstream_real.', String(Dll)) > 0) then begin
    MsgBox('WSALoader is already active: nothing to repair.', mbInformation, MB_OK);
    Exit;
  end;
  if RunInstallScript(InstalledDir, Output) <> 0 then
    MsgBox('Repair failed: WSA was not modified.' + #13#10#13#10 + String(Output), mbError, MB_OK)
  else begin
    MsgBox('WSALoader repaired. Start an Android app: the mods load with WSA.', mbInformation, MB_OK);
    CloseSetup;
  end;
end;

procedure DoUninstall(SmartTVOnly: Boolean);
var
  Uninstaller: String;
  Code: Integer;
begin
  if SmartTVOnly then begin
    // only WsaClient loads the mods: close it before removing the DLL
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM WsaClient.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
    Sleep(1000);
    if not DelTree(AddBackslash(InstalledDir) + 'Custom\WSASmartTV', True, True, True) then begin
      MsgBox('WSASmartTV could not be removed.', mbError, MB_OK);
      Exit;
    end;
    // the next setup run must not select WSASmartTV again
    RegWriteStringValue(HKLM, UninstallKey, 'Inno Setup: Setup Type', 'custom');
    RegWriteStringValue(HKLM, UninstallKey, 'Inno Setup: Selected Components', 'loader');
    RegWriteStringValue(HKLM, UninstallKey, 'Inno Setup: Deselected Components', 'smarttv');
    MsgBox('WSASmartTV removed. WSALoader stays installed.', mbInformation, MB_OK);
    CloseSetup;
  end else begin
    if not RegQueryStringValue(HKLM, UninstallKey, 'UninstallString', Uninstaller) then Exit;
    StringChangeEx(Uninstaller, '"', '', True);
    if Exec(Uninstaller, '', '', SW_SHOW, ewNoWait, Code) then CloseSetup
    else MsgBox('The uninstaller could not be started.', mbError, MB_OK);
  end;
end;

function MaintenanceRow(Form: TSetupForm; const Caption, Text: String; Top, Res: Integer): TNewButton;
var
  L: TNewStaticText;
begin
  Result := TNewButton.Create(Form);
  Result.Parent := Form;
  Result.Caption := Caption;
  Result.Left := ScaleX(16);
  Result.Top := Top;
  Result.Width := ScaleX(96);
  Result.Height := ScaleY(26);
  Result.ModalResult := Res;
  L := TNewStaticText.Create(Form);
  L.Parent := Form;
  L.Caption := Text;
  L.Left := ScaleX(124);
  L.Top := Top + ScaleY(4);
  L.Width := Form.ClientWidth - ScaleX(140);
  L.WordWrap := True;
end;

procedure MaintenanceClick(Sender: TObject);
var
  Form: TSetupForm;
  B: TNewButton;
  SmartTVRadio, AllRadio: TNewRadioButton;
  UpdateCaption, UpdateText: String;
  HasSmartTV, SmartTVOnly: Boolean;
  Top, Choice: Integer;
begin
  HasSmartTV := FileExists(AddBackslash(InstalledDir) + 'Custom\WSASmartTV\WSASmartTV.dll');
  if InstalledVersion = '{#AppVer}' then begin
    UpdateCaption := 'Reinstall';
    UpdateText := 'Installs {#AppVer} again. Keeps the WSA folder, the mods and their settings.';
  end else begin
    UpdateCaption := 'Update';
    UpdateText := 'Installs {#AppVer} over ' + InstalledVersion + '. Keeps the WSA folder, the mods and their settings.';
  end;

  Form := CreateCustomForm(ScaleX(500), ScaleY(250), False, True);
  try
    Form.Caption := 'WSALoader maintenance';
    Top := ScaleY(16);
    MaintenanceRow(Form, UpdateCaption, UpdateText, Top, mrYes);
    Top := Top + ScaleY(48);
    MaintenanceRow(Form, 'Repair', 'Puts WSALoader back after a WSA update replaced gfxstream_backend.dll.', Top, mrRetry);
    Top := Top + ScaleY(48);
    MaintenanceRow(Form, 'Uninstall', '', Top, mrAbort);

    SmartTVRadio := TNewRadioButton.Create(Form);
    SmartTVRadio.Parent := Form;
    SmartTVRadio.Caption := 'WSASmartTV only';
    SmartTVRadio.Left := ScaleX(124);
    SmartTVRadio.Top := Top + ScaleY(4);
    SmartTVRadio.Width := Form.ClientWidth - ScaleX(140);
    SmartTVRadio.Enabled := HasSmartTV;
    AllRadio := TNewRadioButton.Create(Form);
    AllRadio.Parent := Form;
    AllRadio.Caption := 'Everything: WSALoader and all its mods';
    AllRadio.Left := ScaleX(124);
    AllRadio.Top := SmartTVRadio.Top + ScaleY(22);
    AllRadio.Width := Form.ClientWidth - ScaleX(140);
    AllRadio.Checked := True;

    B := TNewButton.Create(Form);
    B.Parent := Form;
    B.Caption := 'Close';
    B.Width := ScaleX(80);
    B.Height := ScaleY(26);
    B.Left := Form.ClientWidth - B.Width - ScaleX(16);
    B.Top := Form.ClientHeight - B.Height - ScaleY(12);
    B.ModalResult := mrCancel;
    B.Cancel := True;

    Choice := Form.ShowModal;
    SmartTVOnly := HasSmartTV and SmartTVRadio.Checked;
  finally
    Form.Free;
  end;

  case Choice of
    mrYes: DoUpdate;
    mrRetry: DoRepair;
    mrAbort: DoUninstall(SmartTVOnly);
  end;
end;

// bottom left of the wizard, only when WSALoader is already installed
procedure CreateMaintenanceButton;
begin
  DetectInstallation;
  if InstalledVersion = '' then Exit;
  MaintenanceButton := TNewButton.Create(WizardForm);
  MaintenanceButton.Parent := WizardForm;
  MaintenanceButton.Caption := '&Maintenance...';
  MaintenanceButton.Width := ScaleX(110);
  MaintenanceButton.Height := WizardForm.CancelButton.Height;
  MaintenanceButton.Top := WizardForm.CancelButton.Top;
  MaintenanceButton.Left := WizardForm.ClientWidth - WizardForm.CancelButton.Left - WizardForm.CancelButton.Width;
  MaintenanceButton.OnClick := @MaintenanceClick;
  WizardForm.SelectComponentsLabel.Caption := 'WSALoader ' + InstalledVersion +
    ' is already installed. Click Next to change the installation, or Maintenance to update, repair or uninstall it.';
end;

procedure CurPageChanged(CurPageID: Integer);
var
  Text: String;
begin
  if MaintenanceButton = nil then CreateMaintenanceButton;
  if MaintenanceButton <> nil then
    MaintenanceButton.Visible := not UpdateMode and ((CurPageID <= wpReady) or (CurPageID = TvPage.ID) or
      (CurPageID = RemotePage.ID));
  if (CurPageID = TvPage.ID) and not SettingsLoaded then begin
    LoadSettings;
    SettingsLoaded := True;
  end;
  if CurPageID = wpFinished then begin
    if LoaderFailed then
      Text := 'The files were copied, but WSALoader could not be activated: WSA is unchanged.'
    else begin
      Text := 'WSALoader is installed. Start an Android app: the mods load with WSA.';
      if WizardIsComponentSelected('smarttv') and StandaloneRadio.Checked then begin
        if PcIp = '' then PcIp := '<PC IP>';
        Text := Text + #13#10#13#10 + 'Phone remote: open http://' + PcIp + ':' + RemotePort +
          ' in the phone browser while WSA is running, then enter your PIN.';
      end;
      if WizardIsComponentSelected('smarttv') and ExternalRadio.Checked then
        Text := Text + #13#10#13#10 + 'Phone remote: while WSA is running it connects to ' +
          Trim(ServerEdit.Text) + '. Open the remote from your server.';
    end;
    WizardForm.FinishedLabel.Caption := Text + #13#10#13#10 + 'Crafted by Will0wn - github.com/Will0wn';
    WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
  end;
end;
