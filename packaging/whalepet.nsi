; ============================================================================
;  WhalePet 安装程序（NSIS 3.x）
;  ---------------------------------------------------------------------------
;  由 packaging/make-package.ps1 调用：
;    makensis /INPUTCHARSET UTF8 packaging/whalepet.nsi
;  （本文件为 UTF-8 编码，中文必须配合 /INPUTCHARSET UTF8）
;
;  打包源：dist/WhalePet/ —— 免安装版内容，由 `-DWHALEPET_PACKAGE=ON` 构建 +
;  windeployqt 生成，**不含调试符号（PDB）与用户数据（data/）**。
;
;  可用 /D 覆盖：APP_VERSION / APP_SRC / APP_OUTFILE
; ============================================================================

Unicode true

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "0.1.0"
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

; ---- 安装包元信息 ----
Name "${APP_DISPLAY_NAME}"
OutFile "${APP_OUTFILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "${APP_UNINST_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

VIProductVersion "0.1.0.0"
VIAddVersionKey /LANG=2052 "ProductName" "${APP_DISPLAY_NAME}"
VIAddVersionKey /LANG=2052 "FileDescription" "${APP_DISPLAY_NAME} 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=2052 "LegalCopyright" ""

; ---- 安装向导界面 ----
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "启动 ${APP_DISPLAY_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ============================================================================
Section "安装" SecInstall
  SetOutPath "$INSTDIR"
  ; 复制程序与 Qt 运行库；排除调试符号与用户数据
  File /r /x "*.pdb" /x "*.ilk" /x "data\*.*" "${APP_SRC}\*.*"

  ; 开始菜单 / 桌面快捷方式
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_DISPLAY_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\${APP_DISPLAY_NAME}.lnk" "$INSTDIR\${APP_EXE}"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; 控制面板「程序和功能」登记
  WriteRegStr HKLM "${APP_UNINST_KEY}" "DisplayName" "${APP_DISPLAY_NAME}"
  WriteRegStr HKLM "${APP_UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${APP_UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "${APP_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${APP_UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
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
  ; 先询问是否删除存档（data/）
  MessageBox MB_YESNO|MB_ICONQUESTION \
    "是否同时删除游戏存档（data 目录）？$\r$\n$\r$\n选择「否」将保留存档，便于日后重装继续。" \
    IDNO KeepUserData
  RMDir /r "$INSTDIR\data"
KeepUserData:

  ; 程序文件
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\*.pdb"
  RMDir /r "$INSTDIR\generic"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\networkinformation"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\sqldrivers"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\tls"

  ; 快捷方式
  Delete "$DESKTOP\${APP_DISPLAY_NAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APP_NAME}"

  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR" ; 仅在为空时删除（保留 data 时不为空）

  DeleteRegKey HKLM "${APP_UNINST_KEY}"
SectionEnd
