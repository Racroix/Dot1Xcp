; Inno Setup 6.0.5 compatible
; Dot1xCP installer (AppVersion 2.5.5)
;
; Source paths are configured as absolute Windows paths.
; Update SourceRoot if your repo root changes.

#define MyAppName "Dot1xCP"
#define MyAppVersion "2.5.5"
#define MyAppPublisher "Dot1x"
#define MyAppExeName "Dot1xBroker.exe"
#define CPGuid "{{7C5B9DC9-3FAE-4E3C-8B1F-2B4B530A1D77}"
#define SourceRoot "C:\Users\cskan\source\repos"

#define SourceCPDll SourceRoot + "\Dot1xCP\x64\Release\Dot1xCP.dll"
#define SourceBrokerDir SourceRoot + "\Dot1xBroker\bin\Release\net8.0-windows"
#define SourceDotNetDesktopRuntime SourceRoot + "\windowsdesktop-runtime-8.0.24-win-x64.exe"
#define SourceVCRedist SourceRoot + "\vc_redist.x64.exe"
#define DotNetDesktopRuntimeFile "windowsdesktop-runtime-8.0.24-win-x64.exe"
#define VCRedistFile "vc_redist.x64.exe"

[Setup]
AppId={{9A237E8F-6C2D-4FB4-8E6A-2A0E8F88B511}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Dot1xCP
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=Dot1xCP_Setup_{#MyAppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64os
UninstallDisplayIcon={app}\Broker\{#MyAppExeName}

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Files]
; Credential Provider
Source: "{#SourceCPDll}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceBrokerDir}\*"; DestDir: "{app}\Broker"; Flags: ignoreversion recursesubdirs createallsubdirs

; Prerequisites (bundled, silent install)
Source: "{#SourceDotNetDesktopRuntime}"; DestDir: "{tmp}"; DestName: "{#DotNetDesktopRuntimeFile}"; Flags: ignoreversion deleteafterinstall skipifsourcedoesntexist
Source: "{#SourceVCRedist}"; DestDir: "{tmp}"; DestName: "{#VCRedistFile}"; Flags: ignoreversion deleteafterinstall skipifsourcedoesntexist

[Dirs]
Name: "{app}\Broker\branding"

[Registry]
; CP COM 등록 (install_cp.cmd 대체)
Root: HKLM64; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{#CPGuid}"; ValueType: string; ValueName: ""; ValueData: "Dot1xCP"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "SOFTWARE\Classes\CLSID\{#CPGuid}"; ValueType: string; ValueName: ""; ValueData: "Dot1xCP"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "SOFTWARE\Classes\CLSID\{#CPGuid}\InprocServer32"; ValueType: string; ValueName: ""; ValueData: "{app}\Dot1xCP.dll"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "SOFTWARE\Classes\CLSID\{#CPGuid}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"

; Disable hello windows..
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System"; ValueType: dword; ValueName: "EnableFirstLogonAnimation"; ValueData: "0"
; 사용자 전환 경로를 허용해 CP 타일 선택 경로를 유지
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System"; ValueType: dword; ValueName: "HideFastUserSwitching"; ValueData: "0"
; 마지막 로그인 사용자 이름을 표시하지 않음
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System"; ValueType: dword; ValueName: "dontdisplaylastusername"; ValueData: "1"
; 사용자 목록 열거 제한 (Other user 경로 중심)
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System"; ValueType: dword; ValueName: "EnumerateLocalUsers"; ValueData: "0"
; 재부팅 후 자동 재로그온(ARSO) 비활성화
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System"; ValueType: dword; ValueName: "DisableAutomaticRestartSignOn"; ValueData: "1"
; Fast Startup 비활성화(콜드부트에서 로그온 UI 경로 고정)
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Power"; ValueType: dword; ValueName: "HiberbootEnabled"; ValueData: "0"
Root: HKLM; Subkey: "SOFTWARE\Policies\Microsoft\Windows\CloudContent"; ValueType: dword; ValueName: "DisableConsumerFeatures"; ValueData: "1"
Root: HKLM; Subkey: "SOFTWARE\Policies\Microsoft\Windows\CloudContent"; ValueType: dword; ValueName: "DisableThirdPartySuggestions"; ValueData: "1"
Root: HKLM; Subkey: "SOFTWARE\Policies\Microsoft\Windows\CloudContent"; ValueType: dword; ValueName: "DisableWindowsSpotlightFeatures"; ValueData: "1"
Root: HKLM; Subkey: "SOFTWARE\Policies\Microsoft\Edge"; ValueType: dword; ValueName: "HideFirstRunExperience"; ValueData: "1"
Root: HKLM; Subkey: "SOFTWARE\Policies\Microsoft\Edge"; ValueType: dword; ValueName: "TutorialShown"; ValueData: "1"
Root: HKLM; Subkey: "SOFTWARE\Policies\Microsoft\Windows\OOBE"; ValueType: dword; ValueName: "DisablePrivacyExperience"; ValueData: "1"

[Run]
; Silent prerequisite installation (no UI)
Filename: "{tmp}\{#DotNetDesktopRuntimeFile}"; Parameters: "/install /quiet /norestart"; Flags: runhidden waituntilterminated; Check: NeedInstallDotNetDesktopRuntime; StatusMsg: ".NET Desktop Runtime 설치 중..."
Filename: "{tmp}\{#VCRedistFile}"; Parameters: "/install /quiet /norestart"; Flags: runhidden waituntilterminated; Check: NeedInstallVCRedistX64; StatusMsg: "VC++ Redistributable 설치 중..."

[UninstallDelete]
; Remove any generated files/folders left under install root.
Type: filesandordirs; Name: "{app}"
; Remove runtime/log folder used by Broker.
Type: filesandordirs; Name: "{commonappdata}\Dot1xCP"

[Code]
const
  UninstallKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{9A237E8F-6C2D-4FB4-8E6A-2A0E8F88B511}_is1';
  PolicyBackupKey = 'SOFTWARE\Dot1xCP\Backup\PolicyValues';

var
  BrandingTextPage: TInputQueryWizardPage;
  BrandingImagePage: TInputFileWizardPage;
  AccountOptionPage: TWizardPage;
  GrantAdminCheck: TNewCheckBox;

procedure BackupPolicyRegistryValues; forward;

function TryGetExistingUninstallCommand(var UninstallCmd: string): Boolean;
begin
  UninstallCmd := '';
  Result :=
    RegQueryStringValue(HKLM64, UninstallKey, 'QuietUninstallString', UninstallCmd) or
    RegQueryStringValue(HKLM64, UninstallKey, 'UninstallString', UninstallCmd) or
    RegQueryStringValue(HKLM, UninstallKey, 'QuietUninstallString', UninstallCmd) or
    RegQueryStringValue(HKLM, UninstallKey, 'UninstallString', UninstallCmd);
  Result := Result and (Trim(UninstallCmd) <> '');
end;

function UninstallPreviousVersionIfExists: Boolean;
var
  Cmd, UpperCmd: string;
  ResultCode: Integer;
begin
  Result := True;
  if not TryGetExistingUninstallCommand(Cmd) then
    Exit;

  Log('Existing installation detected. uninstall command=' + Cmd);
  UpperCmd := UpperCase(Cmd);
  if Pos('/VERYSILENT', UpperCmd) = 0 then
    Cmd := Cmd + ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART';

  if not Exec(ExpandConstant('{cmd}'), '/C ' + Cmd, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    MsgBox('기존 버전 제거 실행에 실패했습니다. 설치를 중단합니다.', mbError, MB_OK);
    Result := False;
    Exit;
  end;

  if ResultCode <> 0 then
  begin
    MsgBox(Format('기존 버전 제거 실패 (종료 코드: %d). 설치를 중단합니다.', [ResultCode]), mbError, MB_OK);
    Result := False;
    Exit;
  end;

  Log('Previous version uninstall completed successfully.');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not UninstallPreviousVersionIfExists then
    Result := '기존 버전 제거에 실패하여 설치를 진행할 수 없습니다.';
  if Result = '' then
    BackupPolicyRegistryValues;
end;

procedure BackupPolicyDword(const SubKey, ValueName, Tag: string);
var
  ExistingValue: Cardinal;
begin
  if RegQueryDWordValue(HKLM, SubKey, ValueName, ExistingValue) then
  begin
    RegWriteDWordValue(HKLM64, PolicyBackupKey, Tag + '_Present', 1);
    RegWriteDWordValue(HKLM64, PolicyBackupKey, Tag + '_Value', ExistingValue);
  end
  else
  begin
    RegWriteDWordValue(HKLM64, PolicyBackupKey, Tag + '_Present', 0);
    RegDeleteValue(HKLM64, PolicyBackupKey, Tag + '_Value');
  end;
end;

procedure BackupPolicyRegistryValues;
begin
  BackupPolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'EnableFirstLogonAnimation', 'EnableFirstLogonAnimation');
  BackupPolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'HideFastUserSwitching', 'HideFastUserSwitching');
  BackupPolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'dontdisplaylastusername', 'dontdisplaylastusername');
  BackupPolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'EnumerateLocalUsers', 'EnumerateLocalUsers');
  BackupPolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'DisableAutomaticRestartSignOn', 'DisableAutomaticRestartSignOn');
  BackupPolicyDword('SYSTEM\CurrentControlSet\Control\Session Manager\Power', 'HiberbootEnabled', 'HiberbootEnabled');
  BackupPolicyDword('SOFTWARE\Policies\Microsoft\Windows\CloudContent', 'DisableConsumerFeatures', 'DisableConsumerFeatures');
  BackupPolicyDword('SOFTWARE\Policies\Microsoft\Windows\CloudContent', 'DisableThirdPartySuggestions', 'DisableThirdPartySuggestions');
  BackupPolicyDword('SOFTWARE\Policies\Microsoft\Windows\CloudContent', 'DisableWindowsSpotlightFeatures', 'DisableWindowsSpotlightFeatures');
  BackupPolicyDword('SOFTWARE\Policies\Microsoft\Edge', 'HideFirstRunExperience', 'HideFirstRunExperience');
  BackupPolicyDword('SOFTWARE\Policies\Microsoft\Edge', 'TutorialShown', 'TutorialShown');
  BackupPolicyDword('SOFTWARE\Policies\Microsoft\Windows\OOBE', 'DisablePrivacyExperience', 'DisablePrivacyExperience');
end;

procedure RestorePolicyDword(const SubKey, ValueName, Tag: string);
var
  PresentFlag: Cardinal;
  BackupValue: Cardinal;
begin
  if not RegQueryDWordValue(HKLM64, PolicyBackupKey, Tag + '_Present', PresentFlag) then
    Exit;

  if PresentFlag = 1 then
  begin
    if RegQueryDWordValue(HKLM64, PolicyBackupKey, Tag + '_Value', BackupValue) then
      RegWriteDWordValue(HKLM, SubKey, ValueName, BackupValue);
  end
  else
  begin
    RegDeleteValue(HKLM, SubKey, ValueName);
  end;

  RegDeleteValue(HKLM64, PolicyBackupKey, Tag + '_Present');
  RegDeleteValue(HKLM64, PolicyBackupKey, Tag + '_Value');
end;

procedure RestorePolicyRegistryValues;
begin
  RestorePolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'EnableFirstLogonAnimation', 'EnableFirstLogonAnimation');
  RestorePolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'HideFastUserSwitching', 'HideFastUserSwitching');
  RestorePolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'dontdisplaylastusername', 'dontdisplaylastusername');
  RestorePolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'EnumerateLocalUsers', 'EnumerateLocalUsers');
  RestorePolicyDword('SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System', 'DisableAutomaticRestartSignOn', 'DisableAutomaticRestartSignOn');
  RestorePolicyDword('SYSTEM\CurrentControlSet\Control\Session Manager\Power', 'HiberbootEnabled', 'HiberbootEnabled');
  RestorePolicyDword('SOFTWARE\Policies\Microsoft\Windows\CloudContent', 'DisableConsumerFeatures', 'DisableConsumerFeatures');
  RestorePolicyDword('SOFTWARE\Policies\Microsoft\Windows\CloudContent', 'DisableThirdPartySuggestions', 'DisableThirdPartySuggestions');
  RestorePolicyDword('SOFTWARE\Policies\Microsoft\Windows\CloudContent', 'DisableWindowsSpotlightFeatures', 'DisableWindowsSpotlightFeatures');
  RestorePolicyDword('SOFTWARE\Policies\Microsoft\Edge', 'HideFirstRunExperience', 'HideFirstRunExperience');
  RestorePolicyDword('SOFTWARE\Policies\Microsoft\Edge', 'TutorialShown', 'TutorialShown');
  RestorePolicyDword('SOFTWARE\Policies\Microsoft\Windows\OOBE', 'DisablePrivacyExperience', 'DisablePrivacyExperience');
end;

function IsDotNetDesktopRuntime8Installed: Boolean;
var
  FindRec: TFindRec;
begin
  Result := FindFirst(
    ExpandConstant('{pf}\dotnet\shared\Microsoft.WindowsDesktop.App\8.*'),
    FindRec);
  if Result then
    FindClose(FindRec);
end;

function IsVCRedistX64Installed: Boolean;
var
  Installed: Cardinal;
begin
  Result :=
    RegQueryDWordValue(
      HKLM64,
      'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
      'Installed',
      Installed
    ) and (Installed = 1);
end;

function NeedInstallDotNetDesktopRuntime: Boolean;
begin
  Result :=
    FileExists(ExpandConstant('{tmp}\{#DotNetDesktopRuntimeFile}')) and
    (not IsDotNetDesktopRuntime8Installed);
end;

function NeedInstallVCRedistX64: Boolean;
begin
  Result :=
    FileExists(ExpandConstant('{tmp}\{#VCRedistFile}')) and
    (not IsVCRedistX64Installed);
end;

function NibbleToHex(const N: Integer): Char;
begin
  if N < 10 then
    Result := Chr(Ord('0') + N)
  else
    Result := Chr(Ord('A') + (N - 10));
end;

function CodeToHex4(const Code: Integer): string;
var
  V: Integer;
begin
  V := Code and $FFFF;
  Result :=
    NibbleToHex((V div 4096) mod 16) +
    NibbleToHex((V div 256) mod 16) +
    NibbleToHex((V div 16) mod 16) +
    NibbleToHex(V mod 16);
end;

function JsonEscape(const S: string): string;
var
  I: Integer;
  Code: Integer;
begin
  Result := '';
  for I := 1 to Length(S) do
  begin
    Code := Ord(S[I]);
    if Code = 34 then
      Result := Result + '\"'
    else if Code = 92 then
      Result := Result + '\\'
    else if Code = 8 then
      Result := Result + '\b'
    else if Code = 9 then
      Result := Result + '\t'
    else if Code = 10 then
      Result := Result + '\n'
    else if Code = 12 then
      Result := Result + '\f'
    else if Code = 13 then
      Result := Result + '\r'
    else if (Code < 32) or (Code > 126) then
      Result := Result + '\u' + CodeToHex4(Code)
    else
      Result := Result + S[I];
  end;
end;

function SafeTrim(const S: string): string;
begin
  Result := Trim(S);
end;

function IsSupportedImageExt(const Path: string): Boolean;
var
  Ext: string;
begin
  Ext := LowerCase(ExtractFileExt(Path));
  Result := (Ext = '.png') or (Ext = '.jpg') or (Ext = '.jpeg');
end;

procedure InitializeWizard;
begin
  BrandingTextPage := CreateInputQueryPage(
    wpSelectDir,
    '브랜딩 설정',
    '브로커 UI 텍스트를 입력하세요.',
    ''
  );

  BrandingTextPage.Add('타이틀 텍스트', False);
  BrandingTextPage.Add('서브타이틀 텍스트', False);
  BrandingTextPage.Values[0] := 'Campus Network';
  BrandingTextPage.Values[1] := '실습실 단말용 네트워크 인증 프로그램';

  BrandingImagePage := CreateInputFilePage(
    BrandingTextPage.ID,
    '브랜딩 이미지',
    '배경/로고 파일을 선택하세요.',
    '찾아보기 버튼으로 PNG/JPG/JPEG 파일을 선택할 수 있습니다. 비워두면 기본 이미지를 사용합니다.'
  );
  BrandingImagePage.Add('배경 이미지 파일 (선택)', '이미지 파일|*.png;*.jpg;*.jpeg|모든 파일|*.*', '.png');
  BrandingImagePage.Add('로고 이미지 파일 (선택)', '이미지 파일|*.png;*.jpg;*.jpeg|모든 파일|*.*', '.png');
  BrandingImagePage.Values[0] := '';
  BrandingImagePage.Values[1] := '';

  AccountOptionPage := CreateCustomPage(
    BrandingImagePage.ID,
    '계정 권한 옵션',
    'Dot1x 인증으로 생성/동기화되는 로컬 계정 권한을 선택하세요.'
  );
  GrantAdminCheck := TNewCheckBox.Create(AccountOptionPage.Surface);
  GrantAdminCheck.Parent := AccountOptionPage.Surface;
  GrantAdminCheck.Left := ScaleX(0);
  GrantAdminCheck.Top := ScaleY(8);
  GrantAdminCheck.Width := AccountOptionPage.SurfaceWidth;
  GrantAdminCheck.Caption := '인증 계정을 로컬 Administrators 그룹에 추가';
  GrantAdminCheck.Checked := False;
end;

function CopyBrandingImage(const SrcPath, DestPath: string): Boolean;
begin
  Result := True;
  if SafeTrim(SrcPath) = '' then
    Exit;

  if not FileExists(SrcPath) then
  begin
    MsgBox('브랜딩 이미지 파일을 찾을 수 없습니다:' + #13#10 + SrcPath, mbError, MB_OK);
    Result := False;
    Exit;
  end;

  if not IsSupportedImageExt(SrcPath) then
  begin
    MsgBox('지원되지 않는 이미지 확장자입니다. PNG/JPG/JPEG만 가능합니다.' + #13#10 + SrcPath, mbError, MB_OK);
    Result := False;
    Exit;
  end;

  if not CopyFile(SrcPath, DestPath, False) then
  begin
    MsgBox('브랜딩 이미지 복사 실패:' + #13#10 + SrcPath, mbError, MB_OK);
    Result := False;
  end;
end;

procedure WriteBrokerAppSettings;
var
  BrokerDir, BrandingDir, ConfigPath: string;
  TitleText, SubtitleText, BgSrc, LogoSrc: string;
  BgDestRel, LogoDestRel, BgDestAbs, LogoDestAbs: string;
  GrantLocalAdminJson: string;
  JsonText: string;
begin
  BrokerDir := ExpandConstant('{app}\Broker');
  BrandingDir := BrokerDir + '\branding';
  ForceDirectories(BrandingDir);

  TitleText := SafeTrim(BrandingTextPage.Values[0]);
  SubtitleText := SafeTrim(BrandingTextPage.Values[1]);
  BgSrc := SafeTrim(BrandingImagePage.Values[0]);
  LogoSrc := SafeTrim(BrandingImagePage.Values[1]);

  BgDestRel := '';
  LogoDestRel := '';
  GrantLocalAdminJson := 'false';
  if Assigned(GrantAdminCheck) and GrantAdminCheck.Checked then
    GrantLocalAdminJson := 'true';

  if BgSrc <> '' then
  begin
    BgDestRel := 'branding\lab_bg' + ExtractFileExt(BgSrc);
    BgDestAbs := BrokerDir + '\' + BgDestRel;
    if not CopyBrandingImage(BgSrc, BgDestAbs) then
      RaiseException('배경 이미지 처리 실패');
  end;

  if LogoSrc <> '' then
  begin
    LogoDestRel := 'branding\logo' + ExtractFileExt(LogoSrc);
    LogoDestAbs := BrokerDir + '\' + LogoDestRel;
    if not CopyBrandingImage(LogoSrc, LogoDestAbs) then
      RaiseException('로고 이미지 처리 실패');
  end;

  ConfigPath := BrokerDir + '\appsettings.json';

  JsonText :=
    '{' + #13#10 +
    '  "UseDirectDot3": true,' + #13#10 +
    '  "UseStdIn": true,' + #13#10 +
    '  "GrantLocalAdminOnAuth": ' + GrantLocalAdminJson + ',' + #13#10 +
    '  "Dot3ServerName": "ipmentor6.crt",' + #13#10 +
    '  "Dot3TrustedRootCAHash": "65:4A:91:F3:CC:12:75:EF:A4:0E:89:C6:09:7A:1B:7C:FA:56:2D:2A",' + #13#10 +
    '  "Dot3SkipProfileIfSame": true,' + #13#10 +
    '  "SecurityEnabled": true,' + #13#10 +
    '  "SecurityStrict": false,' + #13#10 +
    '  "SecurityRuntimeRecheckIntervalMs": 5000,' + #13#10 +
    '  "EnableAntiDebugCheck": true,' + #13#10 +
    '  "EnableExecutionEnvironmentCheck": true,' + #13#10 +
    '  "EnableInstallPathCheck": true,' + #13#10 +
    '  "EnableParentProcessCheck": true,' + #13#10 +
    '  "EnableModuleScanCheck": true,' + #13#10 +
    '  "EnableSelfHashCheck": false,' + #13#10 +
    '  "RequireCodeSignature": false,' + #13#10 +
    '  "ExpectedInstallDir": "C:\\Program Files\\Dot1xCP\\Broker",' + #13#10 +
    '  "AllowedParentProcessNames": "LogonUI.exe,CredentialUIBroker.exe,explorer.exe,devenv.exe,dotnet.exe",' + #13#10 +
    '  "BlockedModuleKeywords": "frida,detours,easyhook,x64dbg,ollydbg,scylla,ida64,idaq,idaq64,cheatengine,processhacker",' + #13#10 +
    '  "AllowedModuleNames": "",' + #13#10 +
    '  "AllowedModulePathKeywords": "\\dot1xcp\\broker\\",' + #13#10 +
    '  "TitleText": "' + JsonEscape(TitleText) + '",' + #13#10 +
    '  "SubtitleText": "' + JsonEscape(SubtitleText) + '",' + #13#10 +
    '  "BackgroundImagePath": "' + JsonEscape(BgDestRel) + '",' + #13#10 +
    '  "LogoImagePath": "' + JsonEscape(LogoDestRel) + '"' + #13#10 +
    '}' + #13#10;

  if not SaveStringToFile(ConfigPath, JsonText, False) then
    RaiseException('appsettings.json 생성 실패: ' + ConfigPath);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    WriteBrokerAppSettings;
  end;
end;

procedure ExecUninstallCommandNoFail(const FileName, Params: string);
var
  ResultCode: Integer;
begin
  try
    if not Exec(FileName, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      Log(Format('Uninstall command launch failed: %s %s', [FileName, Params]))
    else
      Log(Format('Uninstall command finished: %s %s (rc=%d)', [FileName, Params, ResultCode]));
  except
    Log(Format('Uninstall command exception: %s %s', [FileName, Params]));
  end;
end;

function ReadFileTextNoFail(const FilePath: string; var Content: string): Boolean;
var
  Raw: AnsiString;
begin
  Result := False;
  Content := '';
  Raw := '';
  try
    if FileExists(FilePath) then
    begin
      Result := LoadStringFromFile(FilePath, Raw);
      if Result then
        Content := Raw;
    end;
  except
    Result := False;
  end;
end;

function JsonExtractStringValue(const JsonText, Key: string): string;
var
  Token: string;
  P, I, L, StartPos: Integer;
begin
  Result := '';
  Token := '"' + Key + '"';
  P := Pos(Token, JsonText);
  if P = 0 then
    Exit;

  L := Length(JsonText);
  I := P + Length(Token);

  while (I <= L) and (JsonText[I] <> ':') do
    I := I + 1;
  if I > L then
    Exit;

  I := I + 1;
  while (I <= L) and ((JsonText[I] = ' ') or (JsonText[I] = #9) or (JsonText[I] = #13) or (JsonText[I] = #10)) do
    I := I + 1;
  if (I > L) or (JsonText[I] <> '"') then
    Exit;

  StartPos := I + 1;
  I := StartPos;
  while (I <= L) and (JsonText[I] <> '"') do
    I := I + 1;
  if I > L then
    Exit;

  Result := Copy(JsonText, StartPos, I - StartPos);
end;

function NormalizeGuidBraced(GuidText: string): string;
begin
  Result := Trim(GuidText);
  StringChangeEx(Result, '{', '', True);
  StringChangeEx(Result, '}', '', True);
  Result := UpperCase(Result);
  if Result <> '' then
    Result := '{' + Result + '}';
end;

function TryResolveInterfaceNameFromGuid(const GuidBraced: string; var InterfaceName: string): Boolean;
var
  KeyPath: string;
begin
  InterfaceName := '';
  if GuidBraced = '' then
  begin
    Result := False;
    Exit;
  end;

  KeyPath :=
    'SYSTEM\CurrentControlSet\Control\Network\{4D36E972-E325-11CE-BFC1-08002BE10318}\' +
    GuidBraced + '\Connection';
  Result := RegQueryStringValue(HKLM64, KeyPath, 'Name', InterfaceName);
  if not Result then
    Result := RegQueryStringValue(HKLM, KeyPath, 'Name', InterfaceName);
  if Result then
    InterfaceName := Trim(InterfaceName);
  Result := Result and (InterfaceName <> '');
end;

procedure RemoveLanProfilesOnUninstall;
var
  RuntimeJsonPath, JsonText: string;
  GuidRaw, GuidBraced, InterfaceName: string;
begin
  RuntimeJsonPath := ExpandConstant('{commonappdata}\Dot1xCP\broker-runtime.json');
  if not ReadFileTextNoFail(RuntimeJsonPath, JsonText) then
  begin
    Log('LAN profile delete skipped: runtime json not found/readable.');
    Exit;
  end;

  GuidRaw := JsonExtractStringValue(JsonText, 'WiredInterfaceGuid');
  GuidBraced := NormalizeGuidBraced(GuidRaw);
  if GuidBraced = '' then
  begin
    Log('LAN profile delete skipped: WiredInterfaceGuid missing in runtime json.');
    Exit;
  end;

  if not TryResolveInterfaceNameFromGuid(GuidBraced, InterfaceName) then
  begin
    Log(Format('LAN profile delete skipped: interface name not found for guid=%s', [GuidBraced]));
    Exit;
  end;

  StringChangeEx(InterfaceName, '"', '', True);
  ExecUninstallCommandNoFail(
    ExpandConstant('{sys}\netsh.exe'),
    Format('lan delete profile interface="%s"', [InterfaceName])
  );

  // Fallback path through cmd wrapper for environments with argument parsing quirks.
  ExecUninstallCommandNoFail(
    ExpandConstant('{cmd}'),
    Format('/C netsh lan delete profile interface="%s"', [InterfaceName])
  );
end;

procedure StopDot3SvcOnUninstall;
begin
  ExecUninstallCommandNoFail(
    ExpandConstant('{sys}\sc.exe'),
    'stop dot3svc'
  );
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemoveLanProfilesOnUninstall;
    StopDot3SvcOnUninstall;
    RestorePolicyRegistryValues;
  end;
end;
