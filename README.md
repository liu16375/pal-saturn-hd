# 仙劍奇俠傳 Saturn HD（PC 移植版）

把 1999 年 Sega Saturn 版《仙劍奇俠傳》移植到 Windows 的非官方愛好者專案：以開源引擎 SDLPAL 為基礎，改成讀取並還原土星版的資料與演出，再加上高清畫面、四種語言與若干便利功能。

> **禁止商用。** 本專案僅供個人學習與研究，不得以任何形式販售、收費散布或用於任何商業用途。
> 《仙劍奇俠傳》的遊戲內容、美術、音樂與商標屬於大宇資訊（Softstar），本專案與大宇資訊無關，也未獲其授權。

## 下載（發行文件）

| 檔案 | 說明 |
|---|---|
| **[PAL_Saturn_HD_Setup.exe](https://github.com/liu16375/pal-saturn-hd/releases/download/v1.0/PAL_Saturn_HD_Setup.exe)** | 安裝程式 |
| **[PAL_Saturn_HD_Setup-data.bin](https://github.com/liu16375/pal-saturn-hd/releases/download/v1.0/PAL_Saturn_HD_Setup-data.bin)** | 安裝程式的資料包，必須和上面的 exe 放在同一個資料夾 |
| **PAL_Saturn_HD_portable.zip**：[分卷 1](https://github.com/liu16375/pal-saturn-hd/releases/download/v1.0/PAL_Saturn_HD_portable.zip.001)、[分卷 2](https://github.com/liu16375/pal-saturn-hd/releases/download/v1.0/PAL_Saturn_HD_portable.zip.002) | 免安裝版（GitHub 單檔上限 2 GB，所以拆成兩個分卷） |

所有檔案都在 [Releases v1.0](https://github.com/liu16375/pal-saturn-hd/releases/tag/v1.0)。

**安裝版**：兩個檔案下載到同一個資料夾，執行 `PAL_Saturn_HD_Setup.exe`。預設安裝到目前使用者的目錄，不需要系統管理員權限，約需 21 GB 空間。

**免安裝版**：兩個分卷下載到同一個資料夾，在該資料夾開啟命令提示字元合併後解壓，再執行其中的 `sdlpal.exe`：

```
copy /b PAL_Saturn_HD_portable.zip.001 + PAL_Saturn_HD_portable.zip.002 PAL_Saturn_HD_portable.zip
```

## 操作

| 按鍵 | 功能 |
|---|---|
| 方向鍵 | 移動 |
| Enter / 空白鍵 | 確認、對話 |
| Esc | 選單、取消 |
| Alt + Enter | 全螢幕切換 |
| F1（戰鬥中） | 自動戰鬥開關 |

語言：標題畫面 → 系統 → 第一行，每按一次 Enter 依序切換 **國語 / 中文 / 日本語 / English**。

## 改動與優化內容

### 土星版還原
- 讀取由土星版光碟轉換而來的資料（地圖、角色、戰鬥、劇本、文字）。
- 對話框、選單、狀態與戰鬥介面依土星版的版面與流程重做，多處細節以模擬器對照過原版。
- 六段過場動畫由光碟的影片轉換而來，並修掉了轉換後畫面出現彩色色塊的問題。
- 音樂以原版音源驅動程式從光碟資料直接算繪，逐曲對應原曲號。

### 高清化
- 場景地圖、角色、敵人、法術特效、戰鬥背景與頭像提供 6 倍解析度（1920×1200）的高清素材；部分素材經 AI 輔助放大後再修整。
- 介面文字改用向量字型在高清層繪製。
- 修正了高清地磚接縫、角色換裝時貼圖錯位、畫面邊緣露出鄰近房間等問題。

### 多語言
- 新增簡體中文、日文、英文，可在標題畫面隨時切換；國語為原版文字。
- 全部 13534 行對白、道具與仙術名稱及說明、選單、地圖名稱皆已翻譯；人名、地名在全篇使用統一譯名。
- 日文、英文的對白會依對話框寬度自動換行與分頁，道具與裝備名稱過長時自動縮窄顯示。
- 仍為中文的部分：標題圖、裝備與狀態畫面的欄位標題、戰鬥指令圖示、過場動畫內的文字（這些是圖片）。

### 便利功能
- 進入場景時顯示地圖名稱；小地圖。
- 對話記錄，可回看先前的對話。
- 可互動物品的提示標記。
- 自動存檔（切換場景時）。
- 戰鬥自動攻擊；進入戰鬥的黑畫面等待時間縮短。
- 選單內可直接開關音樂與音效。
- 兩處劇情提示（神殿祭壇、濃霧樹林），避免卡關。
- 通關播完片尾後回到標題畫面。

## 倉庫內容

| 目錄 | 內容 |
|---|---|
| `engine/` | 修改後的 SDLPAL 原始碼（基於上游提交 `d4bf69b`）。`3rd/` 下的第三方函式庫為上游的 git 子模組，未包含在此，需從上游取得。 |
| `i18n/` | 翻譯表（對白、名稱、字串）與產生語言檔的腳本 `build.py`。 |
| `tools/` | 把土星版光碟資料轉換成引擎可讀格式的腳本。 |

本倉庫不包含遊戲資料、高清素材與音樂；這些只在發行文件中。

### 編譯（Windows）

```
MSBuild engine\win32\sdlpal.vcxproj -p:Configuration=Release -p:Platform=x64 -p:PlatformToolset=v143
```

## 已知限制

- 敵人站位、部分法術特效位置與召喚動畫為近似還原。
- 日文、英文介面中，戰鬥結算、升級與習得仙術的提示框尚未逐一實機確認。
- 日文按文節換行，行尾長短不齊。

## 授權與致謝

- 引擎原始碼基於 [SDLPAL](https://github.com/sdlpal/sdlpal)，依 **GNU GPL v3** 授權（見 `engine/LICENSE`）；對引擎的修改同樣以 GPL v3 提供。
- 遊戲本體的一切內容版權屬於大宇資訊（Softstar）。請支持正版。
- **再次聲明：本專案及其發行文件禁止用於任何商業用途。**
