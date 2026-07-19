#include "HomeSpan.h"

struct My_AC; 
My_AC *my_ac_device;

struct My_AC : Service::HeaterCooler {     
  SpanCharacteristic *active;
  SpanCharacteristic *curTemp;
  SpanCharacteristic *targCoolTemp; // Температура для охлаждения
  SpanCharacteristic *targHeatTemp; // Температура для нагрева
  SpanCharacteristic *curState;
  SpanCharacteristic *targState;
  SpanCharacteristic *fanSpeed; // НОВАЯ ХАРАКТЕРИСТИКА
  SpanCharacteristic *swing;
  

  My_AC() : Service::HeaterCooler() {
    active = new Characteristic::Active(0);                     
    curTemp = new Characteristic::CurrentTemperature(25);       
    
    // Порог охлаждения (нужен для режима Cool и Auto)
    targCoolTemp = new Characteristic::CoolingThresholdTemperature(25); 
    targCoolTemp->setRange(16, 32, 1);

    // Порог нагрева (НУЖЕН ЧТОБЫ НЕ БЫЛО NULL В РЕЖИМЕ HEAT)
    targHeatTemp = new Characteristic::HeatingThresholdTemperature(25);
    targHeatTemp->setRange(16, 32, 1);

    // 0=Inactive, 1=Idle, 2=Heating, 3=Cooling
    curState = new Characteristic::CurrentHeaterCoolerState(0);
    // 0=Auto, 1=Heat, 2=Cool
    targState = new Characteristic::TargetHeaterCoolerState(2); 
    targState->setValidValues(2, 1, 2);
    fanSpeed = new Characteristic::RotationSpeed(1); // Старт с 1 (Low)
    fanSpeed->setRange(0, 3, 1);                    // Всего 3 деления: 1, 2, 3

        // 0 = Swing Disabled, 1 = Swing Enabled
    swing = new Characteristic::SwingMode(0); 

    Serial1.begin(9600, SERIAL_8N1, 5, 6); 
  } 


void syncStates(int p, int sw, int m, int f, int t, int roomT) {
    // 1. Питание
    if(active->getVal() != p) active->setVal(p);
    
    // 2. Иконка (0-Off, 2-Heat, 3-Cool). Используем 'm' (Mode)
    int state = 0;
    if(p == 1) state = (m == 0x03) ? 2 : 3; 
    if(curState->getVal() != state) curState->setVal(state);

    // 3. Целевая температура
    if(targCoolTemp->getVal() != t) targCoolTemp->setVal(t);
    if(targHeatTemp->getVal() != t) targHeatTemp->setVal(t);

    // 4. Текущая температура (-2 градуса)
    float correctedRoomTemp = (float)roomT;
    if(curTemp->getVal() != correctedRoomTemp) curTemp->setVal(correctedRoomTemp);

    // 5. Скорость (f приходит как 01, 02 или 03). Синхронизируем с ползунком 1-3
      // Скорость (f приходит как 01, 02 или 03)
    int fHome = (f == 2) ? 2 : (f == 3 ? 3 : 1);
    if(fanSpeed->getVal() != fHome) fanSpeed->setVal(fHome);

    // 6. Swing (инверсия)
    int swHome = (sw == 0x06) ? 0 : 1;
    if(swing->getVal() != swHome) swing->setVal(swHome);
}

   boolean update() override {                              
    
    
    int p = active->getNewVal();
    int s = targState->getNewVal();

    // ЖЕСТКИЙ ИНВЕРТ: если нажали на кнопку питания (updated)
    // и при этом НЕ трогали ползунок режима (!targState->updated())
    if(active->updated() && !targState->updated() && !fanSpeed->updated()) {
        // Заставляем p быть 1, если пришла команда включения (плитка)
        p = active->getNewVal(); 
        
        // ФОРСИРУЕМ: если это включение, то p ТОЧНО должно быть 1
        if(p == 1) active->setVal(1); 
        
        if(p == 1 && s == 0) s = 2; 
    }

    // 2. ЛОГИКА СКОРОСТИ (0, 1, 2, 3)
    int fVal = (int)fanSpeed->getNewVal();
    
    // Если ползунок ушел в 0, мы принудительно ставим питание p = 0
    if(fanSpeed->updated() && fVal == 0) {
        p = 0;
        active->setVal(0);
    }

    byte fCode = 0x01; // Байт скорости для кондея (минимум по умолчанию)
    if(fVal == 2) fCode = 0x02;      // Средняя
    else if(fVal >= 3) fCode = 0x03; // Максимальная
    else fCode = 0x01;               // Если fVal 1 или 0, в пакете шлем 01


    // 3. ПОДГОТОВКА ПАКЕТА
    int t = (s == 1) ? (int)targHeatTemp->getNewVal() : (int)targCoolTemp->getNewVal();
    byte mCode = (s == 1) ? 0x03 : 0x01; 
    byte swCode = (swing->getNewVal() == 1) ? 0x00 : 0x06; 







    byte packet[22] = {
      0xAA, 0x14, 0x02, (byte)(p ? 0x01 : 0x00), 
      0x00, swCode, 0x0B, mCode, 0x10, fCode, 
      (byte)t, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00, 
      0x00, 0x05, 0x00, 0x00, 0x00 
    };

    int sum = 0;
    for(int i = 0; i < 21; i++) sum += packet[i];
    packet[21] = (byte)(sum & 0xFF); 

    Serial1.write(packet, 22);
          // --- ВСТАВИТЬ ЭТОТ БЛОК ---
      Serial.print("\n[ОТПРАВКА В КОНДЕЙ]: ");
      for(int i = 0; i < 22; i++) {
        Serial.printf("%02X ", packet[i]);
      }
      Serial.println();
      // -------------------------
    
    // Статус иконки в приложении
    if(p == 0) curState->setVal(0);
    else curState->setVal(s == 1 ? 2 : 3); 

    return true;
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000); 


//homeSpan.setLogLevel(1); // Ставим 1, чтобы не засорять эфир при шифровании

  homeSpan.setWifiCredentials("Apple Homekit", "27021994");

  // ВАЖНО: Сменил Setup ID на DX15, чтобы iPhone перечитал структуру сервисов и убрал NULL
homeSpan.begin(Category::AirConditioners, "AC1", "46637726", "P134");

  new SpanAccessory();  
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Manufacturer("DNS-m");
      new Characteristic::SerialNumber("ZeroSuper");
     my_ac_device = new My_AC();
}

void loop() {
  homeSpan.poll();

  // Пинг раз в 30 сек для обновления данных
  static uint32_t lastPing = 0;
  if (millis() - lastPing > 30000) { 
    lastPing = millis();
    byte ping[] = {0xAA, 0x02, 0x01, 0xAD};
    Serial1.write(ping, 4);
  }

  if (Serial1.available() >= 22) {
    if (Serial1.peek() == 0xAA) {
      byte r[22];
      Serial1.readBytes(r, 22);

      if (r[1] == 0x14 && (r[2] == 0x01 || r[2] == 0x02)) {
        if(my_ac_device != NULL) {
          // Вызываем одну функцию и передаем ей все байты по порядку из твоих логов
          // Питание(r[3]), Swing(r[5]), Режим(r[7]), Скорость(r[9]), Уставка(r[10]), Комнатная(r[11])
          my_ac_device->syncStates(r[3], r[5], r[7], r[9], r[10], r[11]);
        }
      }
    } else {
      Serial1.read();
    }
  }
}