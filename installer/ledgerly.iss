; Inno Setup script for Ledgerly (Windows installer).
; Built by .github/workflows/windows.yml:
;   iscc /DAppVersion=1.0.0 /DSourceDir=<deploy folder> /DOutputDir=<out> installer\ledgerly.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\deploy"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName "Ledgerly"
#define AppExe "Ledgerly.exe"

[Setup]
AppId={{7C1B6F2E-4E7A-4D2B-9C55-1E0B7B3A9F41}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Ledgerly
AppComments=Income & Expense Manager for small businesses
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Lets people without admin rights install just for themselves.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Ledgerly-Setup-{#AppVersion}
SetupIconFile=..\resources\windows\ledgerly.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; Note: your data (in %APPDATA%\Ledgerly) is intentionally kept when uninstalling.
