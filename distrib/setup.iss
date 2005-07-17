; Script generated by the Inno Setup Script Wizard.

[Setup]
OutputBaseFilename=deng-1.9.0-beta2-setup
VersionInfoVersion=1.9.0.2
AppName=Doomsday Engine
AppVerName=Doomsday Engine 1.9.0-beta2
AppPublisher=Jaakko Ker�nen
AppPublisherURL=http://sourceforge.net/projects/deng/
AppSupportURL=http://www.doomsdayhq.com/
AppUpdatesURL=http://sourceforge.net/projects/deng/
DefaultDirName={pf}\Doomsday
DefaultGroupName=Doomsday Engine
AllowNoIcons=yes
SetupIconFile=C:\Documents and Settings\jaakko\Desktop\Snowberry\graphics\snowberry.ico
SourceDir=..\doomsday
OutputDir=D:\Projects\de1.9\distrib\Out
Compression=lzma
;Compression=zip/1
SolidCompression=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Dirs]
Name: "{app}\Bin"
Name: "{app}\Data"
Name: "{app}\Data\Fonts"
Name: "{app}\Data\Graphics"
Name: "{app}\Data\KeyMaps"
Name: "{app}\Data\jDoom"
Name: "{app}\Data\jHeretic"
Name: "{app}\Data\jHexen"
Name: "{app}\Data\jDoom\Auto"
Name: "{app}\Data\jHeretic\Auto"
Name: "{app}\Data\jHexen\Auto"
Name: "{app}\Defs"
Name: "{app}\Defs\jDoom"
Name: "{app}\Defs\jHeretic"
Name: "{app}\Defs\jHexen"
Name: "{app}\Defs\jDoom\Auto"
Name: "{app}\Defs\jHeretic\Auto"
Name: "{app}\Defs\jHexen\Auto"
Name: "{app}\Doc"
Name: "{app}\Doc\jDoom"
Name: "{app}\Doc\jHeretic"
Name: "{app}\Doc\jHexen"
Name: "{app}\Snowberry"
Name: "{app}\Snowberry\profiles"
Name: "{app}\Snowberry\addons"
Name: "{app}\Snowberry\graphics"
Name: "{app}\Snowberry\conf"
Name: "{app}\Snowberry\lang"
Name: "{app}\Snowberry\plugins"

[Files]
; NOTE: Don't use "Flags: ignoreversion" on any shared system files
; Binaries
Source: "Bin\Release\Doomsday.exe"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "Bin\Release\jDoom.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "Bin\Release\jHeretic.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "Bin\Release\jHexen.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "Bin\Release\dr*.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "Bin\Release\dsA3D.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "Bin\Release\dsCompat.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "Bin\Release\dp*.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
; Libraries
Source: "DLLs\LZSS.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "DLLs\EAX.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "DLLs\zlib.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "DLLs\SDL_net.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "DLLs\SDL.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "D:\sdk\fmod\api\fmod.dll"; DestDir: "{app}\Bin"; Flags: ignoreversion
Source: "D:\Projects\SiDGaR\Release\*.exe"; DestDir: "{app}"; Flags: ignoreversion
; Snowberry
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\dist\*"; DestDir: "{app}\Snowberry"; Flags: ignoreversion
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\graphics\*.bmp"; DestDir: "{app}\Snowberry\graphics"
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\graphics\*.png"; DestDir: "{app}\Snowberry\graphics"
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\graphics\*.jpg"; DestDir: "{app}\Snowberry\graphics"
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\graphics\*.ico"; DestDir: "{app}\Snowberry\graphics"
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\lang\english.lang"; DestDir: "{app}\Snowberry\lang"
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\plugins\*.py"; DestDir: "{app}\Snowberry\plugins"; Excludes: "observer.py"
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\profiles\*.prof"; DestDir: "{app}\Snowberry\profiles"
Source: "C:\Documents and Settings\jaakko\Desktop\Snowberry\conf\*.conf"; DestDir: "{app}\Snowberry\conf"
; Documentation
Source: "Doc\ChangeLog.txt"; DestDir: "{app}\Doc"
Source: "Doc\Ame\TXT\Readme.txt"; DestDir: "{app}\Doc"; Flags: isreadme
Source: "Doc\Ame\TXT\Beginner.txt"; DestDir: "{app}\Doc"
Source: "Doc\Ame\TXT\CmdLine.txt"; DestDir: "{app}\Doc"
Source: "Doc\Ame\TXT\InFine.txt"; DestDir: "{app}\Doc"
Source: "Doc\DEDDoc.txt"; DestDir: "{app}\Doc"
Source: "Doc\DSS.txt"; DestDir: "{app}\Doc"
Source: "Doc\Network.txt"; DestDir: "{app}\Doc"
Source: "Doc\Example.bat"; DestDir: "{app}\Doc"
Source: "Doc\CVars.txt"; DestDir: "{app}\Doc"
Source: "Doc\jDoom\Doomlic.txt"; DestDir: "{app}\Doc\jDoom"
Source: "Doc\jDoom\jDoom.txt"; DestDir: "{app}\Doc\jDoom"
Source: "Doc\jHeretic\jHeretic.txt"; DestDir: "{app}\Doc\jHeretic"
Source: "Doc\Ravenlic.txt"; DestDir: "{app}\Doc\jHeretic"
Source: "Doc\jHexen\jHexen.txt"; DestDir: "{app}\Doc\jHexen"
Source: "Doc\Ravenlic.txt"; DestDir: "{app}\Doc\jHexen"
; Definitions
Source: "Defs\*.ded"; DestDir: "{app}\Defs"
Source: "Defs\jDoom\*.ded"; DestDir: "{app}\Defs\jDoom"
Source: "Defs\jHeretic\*.ded"; DestDir: "{app}\Defs\jHeretic"
Source: "Defs\jHexen\*.ded"; DestDir: "{app}\Defs\jHexen"
; Data
Source: "Data\Doomsday.wad"; DestDir: "{app}\Data"
Source: "Data\CPHelp.txt"; DestDir: "{app}\Data"
Source: "Data\Fonts\*.dfn"; DestDir: "{app}\Data\Fonts"
Source: "Data\Fonts\Readme.txt"; DestDir: "{app}\Data\Fonts"
Source: "Data\KeyMaps\*.dkm"; DestDir: "{app}\Data\KeyMaps"
Source: "Data\Graphics\*.*"; DestDir: "{app}\Data\Graphics"
Source: "Data\jDoom\jDoom.wad"; DestDir: "{app}\Data\jDoom"
Source: "Data\jHeretic\jHeretic.wad"; DestDir: "{app}\Data\jHeretic"
Source: "Data\jHexen\jHexen.wad"; DestDir: "{app}\Data\jHexen"

[INI]
Filename: "{app}\Doomsday.url"; Section: "InternetShortcut"; Key: "URL"; String: "http://sourceforge.net/projects/deng/"

[Icons]
Name: "{group}\Doomsday Engine"; Filename: "{app}\Snowberry\Snowberry.exe"; WorkingDir: "{app}\Snowberry"
Name: "{group}\{cm:ProgramOnTheWeb,Doomsday Engine}"; Filename: "{app}\Doomsday.url"
Name: "{group}\{cm:UninstallProgram,Doomsday Engine}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\Doomsday Engine"; Filename: "{app}\Snowberry\Snowberry.exe"; WorkingDir: "{app}\Snowberry"; Tasks: desktopicon

[Run]
Filename: "{app}\Snowberry\Snowberry.exe"; Description: "{cm:LaunchProgram,Doomsday Engine}"; WorkingDir: "{app}\Snowberry"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\Doomsday.url"
Type: files; Name: "{app}\Snowberry\plugins\*.pyc"
Type: files; Name: "{app}\Snowberry\*.log"
Type: dirifempty; Name: "{app}\Snowberry\plugins"
Type: dirifempty; Name: "{app}\Snowberry"
Type: dirifempty; Name: "{app}"

