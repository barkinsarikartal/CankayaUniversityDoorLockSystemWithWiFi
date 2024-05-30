#include <SPIFFS.h>

String fileName = "";

void setup() {
  Serial.begin(115200);
  delay(2000);
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  File file = SPIFFS.open(fileName, "w");
  if (!file) {
    Serial.println("Dosya oluşturulamadı.");
    return;
  }
  file.print("");
  file.close();
  Serial.println("Dosya başarıyla oluşturuldu.");
}

void loop() {
}