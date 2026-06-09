; ============================================================================
;  VoltageSeq2 — Windows VST3 installer (Inno Setup)
;  Build the VST3 first (see installer/windows/README.md), then open this
;  script in Inno Setup and click Build (F9). Output lands in .\Output\.
; ============================================================================

#define MyAppName     "VoltageSeq2"
; Version can be overridden from the command line (CI): ISCC /DMyAppVersion=4.5.1
#ifndef MyAppVersion
  #define MyAppVersion "4.5"
#endif
#define MyAppPublisher "Murgatroyd Instruments"
#define MyAppURL      "https://github.com/Ryanmurg-22/VoltageSeq2"

; Path to the built VST3 bundle, relative to THIS .iss file.
; CMake places it at: <repo>\build\VoltageSeq2_artefacts\Release\VST3\VoltageSeq2.vst3
#define MyVST3Source  "..\..\build\VoltageSeq2_artefacts\Release\VST3\VoltageSeq2.vst3"

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
; Install straight into the shared VST3 folder (no dir-choosing needed).
DefaultDirName={commoncf}\VST3\{#MyAppName}.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=VoltageSeq2_v{#MyAppVersion}_Windows_Installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; A 64-bit-only plugin: force the 64-bit Common Files location and require admin
; (writing under Program Files needs elevation).
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin

[Files]
Source: "{#MyVST3Source}\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; Remove the whole .vst3 bundle folder on uninstall.
Type: filesandordirs; Name: "{app}"

[Messages]
WelcomeLabel2=This will install {#MyAppName} {#MyAppVersion} (VST3) into your shared VST3 plug-in folder.%n%nClose your DAW before continuing.
