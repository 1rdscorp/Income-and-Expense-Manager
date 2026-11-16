# Ledgerly: Income & Expense Manager

An offline-first income and expense manager for small businesses. It needs no server, no install and no account. Everything is stored in your browser on your device.

## Getting started

Open `index.html` in any modern browser (Chrome, Edge, Firefox or Safari). You can double-click the file.

To use it on several devices, or to install it as an app, host the folder on any static web server, for example:

```bash
python3 -m http.server 8080
```

Then open http://localhost:8080.

> **Tip:** Press **N** anywhere to add a new transaction.

## Features

| Area | What you can do |
|---|---|
| **Dashboard** | See today's spending, this month's income, expenses and net profit (compared with last month), and year-to-date profit. It also shows a monthly income vs. expenses chart (click a month to drill in), your top spending categories with budget progress, recent transactions and the last 7 days. |
| **Transactions** | Add income or expenses for any day, with an amount, category, description, vendor or customer, payment method and notes. Attach **receipts**: images or PDFs, uploaded, dragged and dropped, or taken with your phone camera. The list is grouped by day with daily totals. You can filter by month, year or all time, by type and by category, search the text, and export to CSV. |
| **Categories** | Comes with 28 standard business categories (7 income, 21 expense). You can add, rename, change the icon of, or delete a category, and set an optional monthly budget on expense categories. If you delete a category that is in use, you choose whether to move its transactions to another category or delete them. |
| **Reports** | **Monthly:** totals, income and expenses by category (with % share and budget), and a daily breakdown. **Annual:** a category × month table with totals and net profit, plus a chart. **Year over year:** totals, changes, margin and the largest expense category. Every report can be exported to CSV or printed / saved as PDF. |
| **Backup & Restore** | Download a full JSON backup that includes your receipt files, or a lighter backup without them. Export all transactions to CSV. You can restore by **replacing** all data or by **merging**, which adds only what's missing. A reminder appears if you haven't backed up recently. |
| **Settings** | Set your business name and currency (24 options), choose light, dark or system theme, turn photo receipt compression on or off, and choose how often the backup reminder appears. You can also check storage usage, request persistent storage, restore the default categories, load sample data or erase all data. |

## Data & privacy

- Data is kept in the browser's IndexedDB. It never leaves the device unless you export a backup or a CSV.
- Clearing your browser's site data will erase it, so **make regular backups**. The app reminds you to.
- Amounts are stored as whole cents to avoid rounding errors.

## Project structure

```
index.html            App shell
css/styles.css        Styles (light/dark themes, responsive, print)
js/db.js              IndexedDB storage layer
js/defaults.js        Default categories, currencies, settings
js/charts.js          Dependency-free SVG bar chart
js/app.js             Views, routing, forms, reports, backup/restore
```
