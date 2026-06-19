# Mandarin Pinyin Converter

Program to add pinyin to PDF/TXT files containing Mandarin hanzi. Can export as PDF, or as an HTML file which shows definitions and will read text using text-to-speech. 

The program uses **libmupdf** for PDF extraction, **cppjieba + limonp** to segment words for accurate boundaries, **CEDICT** as a dictionary, **wkhtmltopdf** for PDF exporting, and **piper** for a Neural TTS audio output.

## Download & Install (Windows x64)

1. Go to the [**Releases**](https://github.com/jmherrer21/Mandarin-Pinyin-Converter/releases/latest) page.
2. Download **`MandarinPinyinConverter-Setup.exe`**.
3. Run it and follow the prompts.

[[Demo Video]](MPC_Demo.mp4)

<img width="1278" height="698" alt="Screenshot 2026-05-18 082109" src="https://github.com/user-attachments/assets/c1fecf2e-6eb1-4e7f-b7a4-9710b4328988" />


<img width="1277" height="694" alt="Screenshot 2026-05-18 081632" src="https://github.com/user-attachments/assets/e75ad050-e48b-4e60-b72a-fa7baa89d1a8" />

## Building the installer from source

The released `MandarinPinyinConverter-Setup.exe` is produced with [NSIS](https://nsis.sourceforge.io/). The build is automated by [`installer/build.ps1`](installer/build.ps1), which verifies all prerequisites and then compiles the installer.

**Prerequisites** (the build script checks for each and reports any that are missing):

- **NSIS** — `winget install NSIS.NSIS`
- **The compiled app** in `build/Release/` (`mandarin-pdf-reader.exe` + MuPDF DLLs + `data/`), produced by the CMake build.
- **`installer/vc_redist.x64.exe`** — the [Visual C++ x64 redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe).
- **`build/Release/wkhtmltopdf.exe`** — from the [wkhtmltopdf](https://wkhtmltopdf.org/downloads.html) Windows x64 build.
- **`piper/`** — the [Piper](https://github.com/rhasspy/piper/releases) Windows amd64 binaries plus the [`zh_CN-huayan-medium`](https://huggingface.co/rhasspy/piper-voices/tree/main/zh/zh_CN/huayan/medium) voice model (`.onnx` and `.onnx.json`).

Then build:

```powershell
powershell -ExecutionPolicy Bypass -File installer\build.ps1
```

This produces `installer/MandarinPinyinConverter-Setup.exe`. To build a TTS-free installer, comment out the `SecPiper` section in [`installer/setup.nsi`](installer/setup.nsi) and pass `-NoPiper` to the script.
