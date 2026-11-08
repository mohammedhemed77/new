void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT); // الطرف رقم 2 هو اللمبة المدمجة
}

void loop() {
  digitalWrite(2, HIGH);
  Serial.println("LED ON");
  delay(100);
  
  digitalWrite(2, LOW);
  Serial.println("LED OFF");
  delay(100);
}