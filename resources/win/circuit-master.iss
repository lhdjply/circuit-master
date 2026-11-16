; circuit-master — Windows 安装包脚本（Inno Setup 6.5+）
;
; 本地编译（在仓库根目录，先跑过 windeployqt 那套打包流程，暂存目录里要有
; 主程序 + Qt 运行时）：
;   set /p MyVer=<VERSION
;   iscc /DMyAppVersion=%MyVer% /DMyArch=x64 /DMyArchAllowed=x64compatible ^
;        /DMySourceDir=%CD%\dist\circuit-master-%MyVer%-windows-x64 ^
;        /DMyOutputDir=%CD%\dist /DMyRepoRoot=%CD% ^
;        /DMyChineseIsl=%CD%\resources\win\ChineseSimplified.isl ^
;        /DMyIconFile=%CD%\resources\win\circuit-master.ico ^
;        resources\win\circuit-master.iss
;
; CI 的调用方式见 .github/workflows/release.yaml 的「Build the installer」步骤。
;
; 说明：源目录（MySourceDir）就是便携包 zip 打包的那棵暂存树——两者内容完全一致，
; 安装包只是把「解压到某个目录」换成「装到 Program Files 并建快捷方式」。
;
; 显示名一律用中文「电路仿真大师」（桌面/开始菜单快捷方式、控制面板里的程序名），
; 安装目录与可执行文件名保持 ASCII，避免中文路径带来的兼容问题。
;
; 本目录里的 ChineseSimplified.isl 是社区维护的简体中文语言包
; （jrsoftware 的 istrans 收集，维护者 Zhenghan Yang <847320916@qq.com>，
;  https://github.com/kira-96/Inno-Setup-Chinese-Simplified-Translation），
; 按原样随仓库分发，要求 Inno Setup 6.5.0+。
;
; circuit-master.ico 由 resources/assets/icons/icon.svg 生成
; （16/24/32/48/64/128/256 全 PNG 帧）。exe 自身的 PE 图标来自
; resources/win/circuit-master.rc，与本脚本是同一份 ico；
; 这里仍随包装进 {app}，供安装程序、快捷方式与卸载项使用。

#ifndef MyAppVersion
  #define MyAppVersion "0.2.0"
#endif

; MyArch 只用于产物文件名；MyArchAllowed 是 Inno 的目标架构约束。
; 两者分开是因为文件名想写 x64，而 Inno 的约束值要写 x64compatible（6.3+）。
#ifndef MyArch
  #define MyArch "x64"
#endif
#ifndef MyArchAllowed
  #define MyArchAllowed "x64compatible"
#endif

#ifndef MySourceDir
  #define MySourceDir "..\..\dist\circuit-master-" + MyAppVersion + "-windows-x64"
#endif
#ifndef MyOutputDir
  #define MyOutputDir "..\..\dist"
#endif
; 仓库根目录，用来取 README / CHANGELOG（相对本脚本所在目录）。
#ifndef MyRepoRoot
  #define MyRepoRoot "..\.."
#endif
; 中文翻译文件路径。ISPP 的 SourcePath 本身就是**脚本所在目录**（不是含文件名的
; 完整路径），这里补一个反斜杠再拼文件名。脚本用相对路径调用 ISCC 时 SourcePath
; 也可能是相对路径，那种情况请按上面命令行显式传绝对路径。
#ifndef MyChineseIsl
  #define MyChineseIsl AddBackslash(SourcePath) + "ChineseSimplified.isl"
#endif
; 安装程序图标；默认取本脚本同目录下的 ico。
#ifndef MyIconFile
  #define MyIconFile AddBackslash(SourcePath) + "circuit-master.ico"
#endif

; 显示名用中文（桌面/开始菜单快捷方式、控制面板里的程序名），
; 安装目录与可执行文件名保持 ASCII，避免路径里出现中文引发兼容问题。
#define MyAppName "电路仿真大师"
; 安装目录沿用 0.1.0 的名字（英文），升级不会装出两份；中文只用于显示。
#define MyAppDirName "Circuit Master"
#define MyAppExeName "CircuitMaster.exe"
#define MyAppPublisher "Circuit Master Maintainers"
#define MyAppUrl "https://github.com/lhdjply/circuit-master"

[Setup]
; AppId 一旦发布就不要再改：升级安装与卸载识别都靠它。
; 这个 GUID 是 circuit-master 自己的，不能与其他项目复用（否则两个软件会互相覆盖安装）。
AppId={{4E9B2C71-8A3D-4F62-B5C8-1D7E9A0F3C42}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppUrl}
AppSupportURL={#MyAppUrl}
AppUpdatesURL={#MyAppUrl}
DefaultDirName={autopf}\{#MyAppDirName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
; 默认按用户安装（装到 %LOCALAPPDATA%\Programs，不弹 UAC）。程序的配置都走
; QSettings 的用户目录，不写安装目录，所以装到哪都不影响使用；
; 需要全机器安装时可在向导里切换。
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed={#MyArchAllowed}
ArchitecturesInstallIn64BitMode={#MyArchAllowed}
OutputDir={#MyOutputDir}
OutputBaseFilename=circuit-master-{#MyAppVersion}-windows-{#MyArch}-setup
SetupIconFile={#MyIconFile}
UninstallDisplayIcon={app}\circuit-master.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; 升级安装时程序可能正在跑：让 Inno 提示关闭并代为结束进程，
; 避免「装完了但跑的还是旧版本」。
CloseApplications=yes
RestartApplications=no
MinVersion=10.0
; 让 Windows 立刻刷新文件关联（否则资源管理器可能还按旧缓存打开）
ChangesAssociations=yes

[Languages]
; 中文放首位 = 默认语言。ChineseSimplified.isl 需要 Inno Setup 6.5.0+。
Name: "chinese"; MessagesFile: "compiler:Default.isl,{#MyChineseIsl}"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; 暂存树是平的：主程序、Qt DLL 在根上，插件在 platforms/、imageformats/、
; iconengines/ 这类子目录里——必须递归，否则装完起不来。
Source: "{#MySourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
; 图标单独装一份：快捷方式与卸载项直接指向它（exe 的 PE 图标是同一份，但
; 显式给路径不依赖 shell 去 exe 里取）
Source: "{#MyIconFile}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\circuit-master.ico"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"; IconFilename: "{app}\circuit-master.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\circuit-master.ico"; Tasks: desktopicon

[Registry]
; 文件关联：双击 .csm 用本程序打开。
; 用 HKA（有管理员权限时装到 HKLM，否则 HKCU），免安装权限下也能生效。
; 注意：Inno 的 [Registry] 每条必须写在一行内，不支持续行。
Root: HKA; Subkey: "Software\Classes\.csm"; ValueType: string; ValueName: ""; ValueData: "CircuitMaster.csm"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\CircuitMaster.csm"; ValueType: string; ValueName: ""; ValueData: "电路仿真大师 电路文件"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\CircuitMaster.csm\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\circuit-master.ico"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\CircuitMaster.csm\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekey
; 「打开方式」列表里也能看到本程序
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#MyAppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\circuit-master.ico"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

; 不写 [UninstallDelete] 清理用户配置（QSettings 所在目录）：
; 文档与偏好属于用户数据，卸载程序不该替用户决定删不删。
