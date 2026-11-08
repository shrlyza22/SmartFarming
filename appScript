// ===== Apps Script: terima data ESP32, simpan ke Google Sheets =====

// ID spreadsheet: bagian URL di antara /d/ dan /edit
const SPREADSHEET_ID = '13mGrQqDdBGuG8fjwNBTN6YV7p33wfxdKz2rG9RJ-Ve8';

const NAMA_SHEET = 'Data';

function doGet(e) {
  const ss = SpreadsheetApp.openById(SPREADSHEET_ID);
  let sheet = ss.getSheetByName(NAMA_SHEET);
  if (!sheet) sheet = ss.insertSheet(NAMA_SHEET);

  // Buat judul kolom kalau sheet masih kosong
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(['Tanggal', 'Suhu (C)', 'Kelembapan udara (%)', 'Kelembapan tanah (%)']);
    sheet.getRange('A:A').setNumberFormat('dd/MM/yyyy HH:mm:ss');
    sheet.getRange(1, 1, 1, 4).setFontWeight('bold');
    sheet.setFrozenRows(1);
  }

  // Kalau dijalankan langsung dari editor (tanpa data), cukup siapkan sheet-nya
  if (!e || !e.parameter || e.parameter.suhu === undefined) {
    return ContentService.createTextOutput('Sheet siap, belum ada data dikirim');
  }

  const p = e.parameter;
  sheet.appendRow([
    new Date(),
    Number(p.suhu),
    Number(p.udara),
    Number(p.tanah)
  ]);

  return ContentService.createTextOutput('OK');
}