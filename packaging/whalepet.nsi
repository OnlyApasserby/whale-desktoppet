; ============================================================================
;  WhalePet 安装程序（NSIS 3.x）
;  ---------------------------------------------------------------------------
;  由 packaging/make-package.ps1 调用：
;    makensis /INPUTCHARSET UTF8 packaging/whalepet.nsi
;  （本文件为 UTF-8 编码，中文必须配合 /INPUTCHARSET UTF8）
;
;  打包源：dist/WhalePet/ —— 免安装版内容，由 `-DWHALEPET_PACKAGE=ON` 构建 +
;  windeployqt 生成，**不含调试符号（PDB）与用户数据（data/、stomach/）**。
;
;  可用 /D 覆盖：APP_VERSION / APP_SRC / APP_OUTFILE
;
;  ★ 维护契约（完整清单见 docs/packages.md）：
;    凡是「安装期新增的文件 / 目录 / 注册表项 / 快捷方式 / 运行期可写路径」，必须
;    同时更新本文件的四处，缺任何一处都会造成「装了删不掉」或「装上不可用」：
;      1) 安装 Section（写入）  2) 卸载 Section（删除，逐条对应）
;      3) 运行期写权限授权      4) File /x 打包排除项
; ============================================================================

Unicode true

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "0.2.0"
!endif
!ifndef APP_SRC
  !define APP_SRC "${__FILEDIR__}\..\dist\WhalePet"
!endif
!ifndef APP_OUTFILE
  !define APP_OUTFILE "${__FILEDIR__}\..\dist\WhalePet-Setup-${APP_VERSION}.exe"
!endif

!define APP_NAME "WhalePet"
!define APP_DISPLAY_NAME "鲸鱼娘桌宠 WhalePet"
!define APP_PUBLISHER "WhalePet"
!define APP_EXE "WhalePet.exe"
!define APP_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\WhalePet"

; 运行期由程序（普通用户身份）写入的安装目录子目录：胃袋 stomach/。
; 见 src/viewmodel/StomachService.cpp —— 拖拽投喂落盘 <applicationDirPath>/stomach。
; 若新增其它运行期可写子目录，必须同步加到此处与卸载清理清单。
!define APP_RW_DIR "stomach"
; 内置 Users 组 SID：用 SID 形式授权，与系统显示语言无关（中文系统下 "Users" 不可靠）。
!define APP_USERS_SID "S-1-5-32-545"

; ---- 安装包元信息 ----
Name "${APP_DISPLAY_NAME}"
OutFile "${APP_OUTFILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; 本程序是 64 位、按「所有用户」安装：
;   - 注册表读写统一走 64 位视图，卸载登记项才与 $PROGRAMFILES64 对应；
;     否则会被重定向到 WOW6432Node（32 位视图）。
;   - 快捷方式落到「所有用户」上下文（公共桌面 / 公共开始菜单），否则只有
;     安装者可见，且换用户卸载时删不到，留下残留。
; 这两项都只能在 Section / Function 内设置（见 .onInit 与两个 Section）：
; 卸载程序**不执行**安装器的 .onInit，卸载 Section 内必须再设一次。

; 注意：VIProductVersion 必须是 4 段数字，APP_VERSION 只有 3 段，故此处单独维护，
; 升版本时与 packaging/make-package.ps1 -Version 一并更新。
VIProductVersion "0.2.0.0"
VIAddVersionKey /LANG=2052 "ProductName" "${APP_DISPLAY_NAME}"
VIAddVersionKey /LANG=2052 "FileDescription" "${APP_DISPLAY_NAME} 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=2052 "LegalCopyright" ""

; ---- 安装向导界面 ----
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION WhalePetLaunchAsUser
!define MUI_FINISHPAGE_RUN_TEXT "启动 ${APP_DISPLAY_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; 完成页「启动 WhalePet」：必须**以普通用户身份**启动，不能直接 Exec。
; 安装器是提权的（RequestExecutionLevel admin），直接 Exec 会让子进程继承
; 高完整性级别（High IL）；而资源管理器是中等完整性（Medium IL），Windows 的
; UIPI 会拦截跨完整性级别的窗口消息 → 桌宠窗口收不到 dragEnterEvent，
; 拖拽时显示「禁止投放」，且与投放文件所在盘符/目录无关。
; 免安装版由资源管理器直接启动（Medium IL）故一切正常——这正是「安装版拖放不可用、
; 免安装版可用」的根因。经 explorer.exe 转发即由资源管理器以 Medium IL 拉起。
; 详见 docs/traps-extend0.md。
; ---------------------------------------------------------------------------
Function WhalePetLaunchAsUser
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${APP_EXE}"'
FunctionEnd

; ---------------------------------------------------------------------------
; .onInit：安装器启动时统一设定 64 位注册表视图 + 所有用户 Shell 上下文
;   （两项都只能在 Section / Function 内设置），并预填上次安装目录。
;   不用 InstallDirRegKey：它读注册表的时机不受控，无法保证已切到 64 位视图。
;   这里显式先读 64 位视图，为空再回退读 32 位视图（兼容历史版本写在 WOW6432Node）。
; ---------------------------------------------------------------------------
Function .onInit
  SetRegView 64
  SetShellVarContext all
  ReadRegStr $0 HKLM "${APP_UNINST_KEY}" "InstallLocation"
  ${If} $0 == ""
    SetRegView 32
    ReadRegStr $0 HKLM "${APP_UNINST_KEY}" "InstallLocation"
    SetRegView 64
  ${EndIf}
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

; ============================================================================
Section "安装" SecInstall
  SetShellVarContext all
  SetRegView 64

  SetOutPath "$INSTDIR"
  ; 复制程序与 Qt 运行库；排除调试符号与运行期数据。
  ; data/、stomach/ 都是运行期生成的数据（存档 / 胃袋），绝不随包分发；
  ; 显式排除目录本身，避免打包机上残留的空目录被一并装到用户机器。
  File /r /x "*.pdb" /x "*.ilk" /x "data" /x "data\*.*" /x "stomach" /x "stomach\*.*" "${APP_SRC}\*.*"

  ; ---- 运行期写权限授权（拖拽投喂可用性的关键，见 docs/packages.md §3）----
  ; 程序以普通用户（非提权）身份运行，需在 <安装目录>/stomach 内落盘；而 $INSTDIR
  ; 由提权安装程序创建，默认只继承上级目录 ACL——装到 C:\Program Files 时 Users 组
  ; 仅有「读取和执行」，部分数据盘的继承 ACL 也可能不给写权限。此时
  ; StomachService::ensureStomachDir() 返回 false，拖拽文件静默不落盘（投喂动画照常
  ; 出现，功能实际不可用）。
  ; 这里先建目录，再用系统自带 icacls 显式授予内置 Users 组「修改」权限（含继承）；
  ; 用 SID 形式与系统语言无关，失败只告警、不阻断安装。
  CreateDirectory "$INSTDIR\${APP_RW_DIR}"
  nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR\${APP_RW_DIR}" /grant *${APP_USERS_SID}:(OI)(CI)M'
  Pop $0
  ${If} $0 != 0
    DetailPrint "警告：为 $INSTDIR\${APP_RW_DIR} 授予写权限失败（icacls 退出码 $0），拖拽投喂可能不可用"
  ${EndIf}

  ; ---- 开始菜单 / 桌面快捷方式（所有用户上下文）----
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_DISPLAY_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\${APP_DISPLAY_NAME}.lnk" "$INSTDIR\${APP_EXE}"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; ---- 控制面板「程序和功能」登记（64 位视图）----
  WriteRegStr HKLM "${APP_UNINST_KEY}" "DisplayName" "${APP_DISPLAY_NAME}"
  WriteRegStr HKLM "${APP_UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${APP_UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "${APP_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${APP_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  ; 静默卸载入口：/S 见卸载 Section 的 IfSilent 分支（默认保留用户数据）
  WriteRegStr HKLM "${APP_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKLM "${APP_UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegDWORD HKLM "${APP_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${APP_UNINST_KEY}" "NoRepair" 1

  ; 估算占用（KB）
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${APP_UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

; ============================================================================
Section "Uninstall"
  ; 卸载程序独立执行，上方全局设置不生效——必须重设，否则会去 32 位视图删注册表、
  ; 去「当前用户」上下文删快捷方式，导致注册表项与公共快捷方式残留。
  SetRegView 64
  SetShellVarContext all

  ; 结束正在运行的程序：否则 WhalePet.exe / *.dll 被占用无法删除，连带
  ; RMDir "$INSTDIR" 失败，整个安装目录都会留下。taskkill 未命中会返回非零，忽略即可。
  nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM "${APP_EXE}" /F'
  Pop $0

  ; 先询问是否删除运行期数据（存档 data/ 与胃袋 stomach/）。
  ; 静默卸载（Uninstall.exe /S）下 MessageBox 无法正常返回，直接走默认「保留」分支，
  ; 避免自动卸载误删用户数据。
  IfSilent KeepUserData
  MessageBox MB_YESNO|MB_ICONQUESTION \
    "是否同时删除游戏存档与投喂暂存（data、stomach 目录）？$\r$\n$\r$\n选择「否」将保留，便于日后重装继续。" \
    IDNO KeepUserData
  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\stomach"
KeepUserData:

  ; ---- 程序文件（与安装 Section 逐条对应，勿遗漏）----
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\*.pdb"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\README.md"

  ; ---- Qt 部署的插件目录 ----
  RMDir /r "$INSTDIR\generic"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\networkinformation"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\sqldrivers"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\tls"

  ; ---- 非递归兜底：清掉仍为空的运行期目录 ----
  ; 走「保留用户数据」分支时数据非空，这两个 RMDir 会自动失败，目录按预期留下。
  RMDir "$INSTDIR\stomach"
  RMDir "$INSTDIR\data"

  ; ---- 快捷方式（所有用户上下文）----
  Delete "$DESKTOP\${APP_DISPLAY_NAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APP_NAME}"

  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR" ; 仅在为空时删除（保留 data 时不为空）

  DeleteRegKey HKLM "${APP_UNINST_KEY}"
SectionEnd
