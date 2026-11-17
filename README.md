# Ledgerly: Income & Expense Manager

A Windows desktop app for small businesses to record daily income and expenses, attach receipts, and see monthly and yearly totals. It is written in C++17 with Qt 6. All data stays on your computer in a local SQLite database.

## Install on Windows

1. Open the repository's **Actions** tab on GitHub, then open the latest **Windows installer** run that finished successfully.
2. Under **Artifacts**, download **Ledgerly-Windows-…**. Unzip it and run **`Ledgerly-Setup-x.y.z.exe`**.
   - You don't need administrator rights. Setup offers to install just for you.
   - It adds a Start-menu entry, an optional desktop shortcut and an uninstaller (*Settings → Apps*).
   - Windows SmartScreen may warn that the app is from an unknown publisher, because the installer isn't code-signed. Click **More info → Run anyway**.
3. If you'd rather not install anything, use the **portable `.zip`** from the same download. Unzip it anywhere and run `Ledgerly.exe`.

**Making a permanent download link.** To publish a release, push a version tag, for example `git tag v1.0.0 && git push origin v1.0.0`. The workflow then attaches the installer to a GitHub Release.

Requires Windows 10 or 11 (64-bit).

## Features

| Area | What you can do |
|---|---|
| **Dashboard** | See today's spending, this month's income, expenses and net profit (compared with last month), and year-to-date profit. It also shows an interactive income-vs-expenses chart (click a month to drill in), your top spending categories with budget progress, recent transactions, upcoming recurring payments and the last 7 days. |
| **Transactions** | Record income or expenses for any day: amount, category, description, vendor or customer (auto-completed), payment method and notes. Attach receipts with a file picker or drag and drop. Large phone photos are shrunk automatically. Transactions are grouped by day with daily totals. You can filter by month, year or all time, by type and by category, and search the text. Right-click to edit, duplicate for today, view receipts, make recurring / stop repeating, or delete (several at once is supported). You can also export the list to **Excel** or CSV. |
| **Recurring** | Set up regular bills and income once — rent, internet, insurance, salaries, subscriptions — weekly, monthly, every 3 months or yearly, with an optional end date. Each payment is **recorded automatically on its due date**; any that fell due while the app was closed are added the next time you open it. Tick **Repeat this transaction** when adding *or editing* a transaction to turn it into a recurring one (you choose whether to back-fill payments since then); a transaction that's part of a series shows **Edit series…** and **Stop repeating**. Pause, resume, edit (future payments only) or delete rules; see your fixed costs per month and bills due in the next 30 days. The dashboard shows upcoming payments and tells you what was recorded automatically. |
| **Bank & Cards** | **Automatic bank connection** through [SimpleFIN Bridge](https://bridge.simplefin.org) (about $15/year, works with U.S. Bank, Wells Fargo, American Express and most U.S. banks and cards): paste a setup token once and new transactions are downloaded every 6 hours. Or add your bank accounts and credit cards — with ready-made presets and download instructions for **U.S. Bank, Wells Fargo and American Express** (Amex CSV charges are flipped to expenses automatically, and credit-card payments between your own accounts are skipped) — and **import statements** (OFX/QFX/QBO, QIF or CSV — columns, delimiters and date formats are detected; your choices are remembered per account). Categories are filled in automatically from your **rules**, from **what you chose before for the same payee**, and from built-in keywords. Lines already imported are skipped, and lines that look like something entered by hand or by a recurring rule are flagged so nothing is counted twice. Set an **automatic import folder** per account and statements saved there are imported within a minute. |
| **Categories** | Comes with 28 standard business categories (7 income, 21 expense). You can add, edit or delete categories, choose an icon, and set an optional monthly budget with over-budget warnings. If you delete a category that is in use, you choose whether to move its transactions to another category or delete them. |
| **Reports** | **Monthly:** income and expenses by category with % share and budget, plus a daily breakdown. **Annual:** a category × month table with totals and net profit. **Year over year:** totals and the change from the previous year. Every report can be exported to **Excel** (.xlsx — real numbers with currency formatting, one sheet per table plus a summary and monthly/yearly totals ready for charts), CSV, PDF, or printed. |
| **Shared folder** | Use Ledgerly on **several computers**: choose a folder in Google Drive, OneDrive or Dropbox on each computer (Backup & Sync → Set up shared folder). Each computer keeps its own database; changes travel through the folder as small journal files that are never edited by two computers, so it's safe with cloud-sync tools. When the same record is changed on two computers, the latest change wins; recurring payments are never recorded twice. |
| **Backup & Restore** | Save a full backup as one `.json` file that includes your receipts, or a lighter backup without them. You can also turn on **automatic backups** when the app closes, saved to a folder you choose (a OneDrive or Dropbox folder works well) and keeping the last *N* copies. Restore by replacing all data, or by merging, which adds only what's missing. A reminder appears if you haven't backed up recently. |
| **Settings** | Set your business name and currency (24 options), choose light, dark or system theme, turn photo compression on or off, and choose how often the backup reminder appears. You can also open the data folder, restore the default categories, load sample data or erase all data. |

**Keyboard shortcuts:**
- **Ctrl+N:** new transaction
- **Ctrl+B:** back up now
- **Ctrl+1 … Ctrl+8:** switch pages
- **Del:** delete the selected transactions
- **Ctrl+Enter:** save in the transaction dialog

### Where your data lives

Your data is stored in `%APPDATA%\Ledgerly\ledgerly.db`. Use **Help → Open data folder** to open it. Uninstalling the app does **not** delete your data. Backups use the same format as the earlier Ledgerly web version, so you can import a backup made in the browser with **Backup & Restore → Restore → Merge**.

## Building from source

Requirements: CMake 3.21 or newer, a C++17 compiler, and Qt 6.4 or newer with the *Widgets, Sql (SQLite), Svg* and *PrintSupport* modules.

**Windows** (Visual Studio 2022 + Qt 6 for MSVC 64-bit):

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64
```

```bash
cmake --build build --config Release
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

To make the build runnable on another PC, run `windeployqt build\Release\Ledgerly.exe`. To build the installer, compile `installer\ledgerly.iss` with [Inno Setup 6](https://jrsoftware.org/isinfo.php). The GitHub Actions workflow in `.github/workflows/windows.yml` does all of this automatically.

**Linux** (e.g. Ubuntu 24.04):

```bash
sudo apt install cmake ninja-build qt6-base-dev qt6-svg-dev libqt6sql6-sqlite
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/Ledgerly
```

## Project layout

```
src/core/      Data model, SQLite store, backup (JSON/CSV), report builders (no UI; unit-tested)
src/ui/        Qt Widgets: main window, pages, dialogs, chart, theme
src/main.cpp   Entry point (--data-dir <dir> to use a different data folder)
resources/     Icons, Windows .ico/.rc, Linux .desktop
installer/     Inno Setup script for the Windows installer
tests/         Qt Test suites: core logic (tst_core) and UI flows (tst_ui)
```
