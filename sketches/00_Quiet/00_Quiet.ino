// 00_Quiet — disable the speaker amplifier and idle.
// Used to silence the board between audio tests.
#define AMP_ENABLE 1   // active LOW: HIGH = amp OFF

void setup() {
  Serial.begin(115200);
  pinMode(AMP_ENABLE, OUTPUT);
  digitalWrite(AMP_ENABLE, HIGH);   // amp OFF (silent)
  Serial.println("Audio amplifier disabled. Board idle.");
}

void loop() {
  delay(1000);
}
