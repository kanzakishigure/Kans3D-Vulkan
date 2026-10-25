# Kans3D Engine

<p align="center">
  <a href="https://github.com/kanzakishigure/Kans3D">
    <img src="./KansEditor/Resources/Icon/Kans3D_logo.png" width="400" alt="Kans3D Engine logo">
  </a>
</p>

<p align="center"><a href="README.md">English</a> · 繁體中文</p>

## 跨平台 3D 遊戲引擎

**Kans3D 是一款以 C++ 編寫、內建編輯器的 3D 遊戲引擎。** 這是一個個人專案，涵蓋從匯入資產到渲染、動畫、播放聲音與編寫場景腳本的完整流程。引擎可在 Windows 與 Linux 上執行；目前其渲染硬體介面（RHI）提供 Vulkan 後端。

- **渲染：** 由渲染圖（render graph）驅動的前向 PBR 管線，支援 HDR 與 ACES／Standard 色調映射、物理式泛光（bloom）、串聯陰影貼圖與點光源陰影（PCSS）、MSAA 與 FXAA、多 pass 材質著色器、後處理材質，以及逐物件的自訂深度／模板。
- **資產：** 源資產以 `.kmeta` 保存匯入設定，並烘焙為產物（product）；支援 glTF、GLB、FBX 與 OBJ 模型，可提取與重新映射材質，支援材質實例與平滑法線串流。
- **動畫：** 骨骼動畫，含狀態機、混合樹、動畫層、根運動與事件；形狀鍵（shape key）；雙骨骼 IK 與注視（look-at）；建置時的動畫片段壓縮。
- **音訊：** 基於 miniaudio 的播放，支援串流與 3D 音效；提供 AudioSource 與 AudioListener 元件。
- **腳本：** 以 .NET 10 CoreCLR 執行 C#，支援熱重載，並可建置 NativeAOT。
- **編輯器：** 可停靠面板、內容瀏覽器、階層、屬性面板、復原／重做、Play 模式、可編輯網格、即時預覽的材質編輯器、Animator 圖，以及影格統計與 Frame Graph 檢視。

## 自由且開源

Kans3D 依 [MIT 授權](LICENSE) 免費開源，由 [kanzakishigure](https://github.com/kanzakishigure) 開發，自 2021 年起持續開發至今。

引擎仍在積極開發中，檔案格式與 API 會隨版本變動。`master` 分支包含最新合併的功能；其他分支為開發中的工作。

## 取得引擎

### 預先建置的版本

目前尚未提供預先建置的執行檔，請依下列說明從原始碼建置。

### 從原始碼編譯

**需求**

- Git、CMake 3.21 以上，以及 Python 3.10 以上（設定選單需要；純文字模式只需 3.7 以上）。
- **Windows 10/11：** Visual Studio 2022（含「使用 C++ 的桌面開發」工作負載）與 Vulkan SDK。
- **Linux：** GCC、Ninja，以及 Vulkan、shaderc、glslang、SPIRV-Cross、SPIRV-Tools、Assimp 與 X11/Wayland 的開發套件。
- **.NET SDK 10**（選用）：啟用 C# 腳本。未安裝時，引擎會在停用腳本的狀態下建置。

**建置步驟**

```
git clone https://github.com/kanzakishigure/Kans3D.git
cd Kans3D
git submodule update --init --recursive Kans3D/vendor
python script/Setup.py          # Windows: script\Setup.bat   Linux: ./script/Setup.sh
```

設定腳本會列出所有相依項目並顯示選單。你可以在選單中安裝缺少的項目（Windows 使用 winget；Linux 使用 pacman、apt 或 dnf）、切換 CMake preset 或測試與建置選項，然後設定 CMake。以 Tab、Shift+Tab 或方向鍵移動，空白鍵切換勾選，Enter 確認，按數字直接選擇，也可以用滑鼠點選。選單使用 `script/third_party` 中純 Python wheel 形式的 [prompt_toolkit](https://github.com/prompt-toolkit/python-prompt-toolkit)，不需要先用 pip 安裝任何套件。

| 選項 | 作用 |
| :----- | :----- |
| `--non-interactive` | 不顯示選單：檢查、詢問一次、安裝所有缺少的項目並設定 |
| `--check` | 只回報狀態；缺少必要相依項目時結束碼為 1 |
| `--tests` | 一併檢查測試相依項目（GoogleTest、軟體 Vulkan） |
| `--build` | 設定完成後建置 KansEditor（Debug） |
| `--yes` | 不詢問直接安裝（純文字模式，用於自動化） |
| `--preset <name>` | CMake 設定 preset（預設為 `windows-msvc`／`linux-gcc`） |

設定完成後手動建置：

```
cmake --build build/windows-msvc --config Debug --target KansEditor   # 或 build/linux-gcc
```

在 Windows 上，Debug 建置會鏈接 Vulkan SDK 的除錯版著色器函式庫，而 SDK 安裝程式預設不會安裝它。請在安裝程式的「**Select Components**（選擇元件）」頁面勾選 **Debuggable Shader API Libraries - 64-bit**（設定腳本透過 winget 安裝時使用預設元件，需要自行補裝，例如重新執行安裝程式勾選）。安裝後請開啟新的終端機，讓 `VULKAN_SDK` 環境變數生效。

**測試**

測試位於 `test/` 下的 [Kans3D-tests](https://github.com/kanzakishigure/Kans3D-tests) 子模組，該倉庫為私有。有存取權限時，以 `git submodule update --init test` 取得，並以 `--tests` 執行設定腳本。沒有該子模組時，CMake 會略過測試，引擎與編輯器照常建置。

## 社群與貢獻

歡迎在 [issue tracker](https://github.com/kanzakishigure/Kans3D/issues) 回報錯誤或提出建議。Pull request 請以 `master` 為目標分支。

## 文件與範例

文件網站正在準備中。在此之前，編輯器啟動時會開啟 Showcase 場景，展示渲染器、材質與編輯工具。
