#define MyAppName "MTK Browser"
#define MyAppVersion "0.3.0"
#define MyAppPublisher "MTK Browser"
#define MyAppExeName "MTKBrowser.exe"

[Setup]
AppId={{F892F759-89AB-4D51-AD09-C75892F80E1B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion} Preview
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\MTK Browser
DefaultGroupName=MTK Browser
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist\installer
OutputBaseFilename=MTK-Browser-Setup-{#MyAppVersion}-Preview
SetupIconFile=..\native\resources\mtk_browser.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
WizardSizePercent=110
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
CloseApplications=yes
CloseApplicationsFilter=MTKBrowser.exe
RestartApplications=no
SetupLogging=yes
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=MTK Browser Preview Installer
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: checkedonce

[Files]
Source: "..\dist\MTK Browser\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\MTK Browser"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\mtk_browser_globe.ico"
Name: "{autodesktop}\MTK Browser"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon; IconFilename: "{app}\mtk_browser_globe.ico"
Name: "{autoprograms}\MTK Browser\Preview notice"; Filename: "{app}\PREVIEW_NOTICE.txt"
Name: "{autoprograms}\MTK Browser\Uninstall MTK Browser"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\MTKBrowser.exe"; ValueType: string; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey

; Default-browser registration. The browser writes these itself on every start,
; because that also repairs a stale entry, but they are declared here so
; uninstalling removes them. Otherwise Windows keeps offering a browser that is
; no longer installed in its Default apps list.
Root: HKCU; Subkey: "Software\Classes\MTKBrowserHTML"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\MTK Browser"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: none; ValueName: "MTK Browser"; Flags: uninsdeletevalue

[InstallDelete]
Type: files; Name: "{autodesktop}\MTK Safe Browser.lnk"
Type: files; Name: "{autoprograms}\MTK Safe Browser.lnk"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch MTK Browser"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"
