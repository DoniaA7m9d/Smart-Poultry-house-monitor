function doPost(e) {
  var d = JSON.parse(e.postData.contents);
  SpreadsheetApp.getActiveSheet().appendRow(
    [new Date(), d.temp, d.hum, d.thi, d.nh3, d.co2, d.fan, d.pump, d.mains]);
  return ContentService.createTextOutput("ok");
}
