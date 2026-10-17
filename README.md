# OpenGD77 – TA8BMO Special Edition

My own build of the OpenGD77 firmware for the **TYT MD-UV390 Plus 10W**.

*Türkçe · Español · Русский: see below.*

> The radio speaks English, Turkish and Spanish (Spanish since v0.2.0).
> Russian is planned for a later version.

It started with one question: can this radio decode a 5-Tone (CCIR) call by
itself? Everyone, me included, thought the received FM audio never reaches the
processor. It turned out it does: the HR-C6000's codec can hand it over I2S, and
on FM that path just sits unused. Once I had that audio, one decoder became
several, and then a whole menu of things I wanted on my own radio.

There is no extra hardware. Everything below is software.

## What it adds

**Decoding FM audio inside the radio** (analogue FM channels only)
- 5-Tone (CCIR) receive and send, per-channel ANI, selective call, automatic reply
- DTMF receive, including the very fast PTT-ID / ANI bursts that sound like a single beep
- Call log (5-Tone and DTMF, with time and frequency, kept through power-off)
- Morse (CW) receive, a Morse sender, and a practice screen that uses the PTT as a key without transmitting
- APRS receive: the last 16 stations, symbol, distance, direction, a compass, which digi a packet came through, Mic-E positions, Turkish letters in comments

**APRS**
- APRS messages, plus WhatsApp (WTSAPP) and e-mail (EMAIL-2) over APRS
- APRS settings made, edited and deleted on the radio, without the CPS
- Transmit fixes: the whole frame goes out (the closing flags were being cut short),
  a longer preamble so slow digipeaters catch the start, and no more 0,0 position
  beacons when there is a better position to send (aprs.fi drops 0,0)

**RX recorder**
- Records every FM transmission you hear into the top 2MB of the external flash
  (about an hour), filed into folders by channel name, with the caller's 5-Tone
  or DTMF code when one was heard. Play back, delete one, a folder, or everything.

**Everyday things**
- Tone finder: shows which CTCSS / DCS a station is using, without changing anything
- Fox hunt screen, spectrum sweep that stops on speech
- Clock set from GPS, very quiet volume range for night listening, brightness on SK1 + volume knob
- New channels, new zones, zone editing, channel delete and a power-on password, all on the radio
- A built-in user manual (Turkish, English and Spanish) and a few games for the long nets :)

## Before you flash it

- **I have only tested this on the MD-UV390 Plus 10W** (`MDUV380_10W_PLUS_FW`). The other
  build targets are still in the tree, but I have not tried them.
- I did not touch TX power, calibration, the PA or the RF tables. Even so, this is a
  hobby build: use it at your own risk and keep a backup of your codeplug.
- Never transmit with the programming cable connected.
- The RX recorder uses the top 2MB of the 16MB flash. That space was the GPS track
  log, which is switched off in this build (`GPS_TRACK_LOG_TO_FLASH` in `settings.h`
  brings it back). The recorder checks the flash size and the DMR ID database at
  every power-on and stays off if anything does not fit.

## Building

You need STM32CubeIDE (it brings the ARM toolchain and `make`).

1. Run `./prepare` (or `prepare.bat`) in the `opengd77` folder once. It writes an
   empty, 0xFF filled codec section.
2. Either open `opengd77/MDUV380_firmware` in STM32CubeIDE and build the
   `MDUV380_10W_PLUS_FW` configuration, or from a shell with the toolchain on the PATH:

   ```
   cd opengd77/MDUV380_firmware/MDUV380_10W_PLUS_FW
   make -j8 all
   ```

   Note the `all`: a bare `make` there quietly builds nothing.

The result is `OpenMDUV380.bin`.

**The AMBE codec is not in this source**, and never will be. As with every
OpenGD77 build, the firmware loader adds it from the official TYT firmware when
the radio is flashed. Without it DMR voice does not work.

Ready-made binaries, when I publish them, are on the Releases page.

## Credits

This is a fork of [OpenGD77](https://www.opengd77.com), originally
conceived by Kai DG4KLU and developed by Roger VK3KYY with Daniel F1RMB, Alex
DL4LEX, Colin G4EML and many others. None of this would exist without their
work. See [the original README](opengd77/README.md) for the full credits.

My starting point was [jamarju's fork](https://github.com/jamarju/opengd77),
based on OpenGD77 20240908.

## License

The OpenGD77 license applies to the whole tree, including my additions: see
[license.txt](opengd77/license.txt) and the header of every source file.
**Commercial use is not allowed.** If you pass on a binary, pass on the license
text with it.

## Supporting the project

The firmware is free and always will be - you never have to pay for it. If it
has been useful to you and you would like to buy me a tea, a donation is very
welcome and goes into more hours on this. It is a gift, not a purchase: it buys
no license, support or warranty.

[☕ buymeacoffee.com/ta8bmo](https://buymeacoffee.com/ta8bmo)

73 de TA8BMO

---

# Türkçe

**TYT MD-UV390 Plus 10W** için kendi OpenGD77 sürümüm.

Her şey tek bir soruyla başladı: Bu telsiz 5-Ton (CCIR) çağrıyı kendi kendine
çözebilir mi? Herkes, ben de dahil, alınan FM sesinin işlemciye hiç ulaşmadığını
sanıyordu. Ulaşıyormuş: HR-C6000'in codec'i sesi I2S üzerinden verebiliyor, FM'de
bu yol sadece boşta duruyor. O sese ulaşınca bir çözücü birkaç çözücü oldu, sonra
da kendi telsizimde görmek istediğim bir sürü özellik.

Ek donanım yok, hepsi yazılım.

**Neler var**
- 5-Ton alma/gönderme, kanal başına ANI, seçmeli çağrı, otomatik cevap
- DTMF alma (tek "bip" gibi duyulan hızlı PTT-ID'ler dahil), çağrı kaydı
- Mors alma, Mors gönderme ve PTT ile (yayın yapmadan) Mors pratiği
- APRS alma (son 16 istasyon, mesafe, yön, pusula, hangi digiden geldiği), APRS
  mesajı, APRS üzerinden WhatsApp ve e-posta, APRS ayarlarını telsizden ekleme/silme
- APRS gönderim düzeltmeleri: çerçevenin tamamı gidiyor, ön sinyal uzun, 0,0 konum gitmiyor
- Dinleme kaydı: duyduğun her FM konuşmasını kaydeder (yaklaşık 1 saat), kanal adına göre klasörler
- Ton bulucu, tilki avı ekranı, konuşmada duran spektrum taraması
- GPS saati, gece için çok kısık ses, SK1 + ses düğmesi ile parlaklık
- Telsizden kanal/zone oluşturma, zone düzenleme, kanal silme, açılış şifresi
- Telsiz içinde Türkçe/İngilizce/İspanyolca kullanım kılavuzu ve birkaç oyun

**Önemli**
- Sadece MD-UV390 Plus 10W'da denedim. Diğer modellerde denemedim.
- TX gücüne, kalibrasyona, PA'ya ve RF tablolarına dokunmadım. Yine de hobi
  amaçlı bir sürüm: risk sende, kodplug yedeğini al.
- Programlama kablosu takılıyken asla yayın yapma.
- AMBE codec bu kaynakta yok; telsize yüklerken firmware loader orijinal TYT
  yazılımından ekler. Codec olmadan DMR ses çalışmaz.
- OpenGD77 lisansı geçerli. Ticari kullanım yasak.

**Destek olmak istersen**

Yazılım ücretsiz ve hep ücretsiz kalacak, kimse bunun için para ödemek zorunda
değil. İşine yaradıysa ve bir çay ısmarlamak istersen bağışın çok makbule geçer,
bu işe daha çok vakit ayırmamı sağlar. Bağış bir hediyedir, satın alma değildir:
karşılığında lisans, destek ya da garanti verilmez.

[☕ buymeacoffee.com/ta8bmo](https://buymeacoffee.com/ta8bmo)

73 de TA8BMO

---

# Español

Mi propia versión del firmware OpenGD77 para el **TYT MD-UV390 Plus 10W**.

Todo empezó con una pregunta: ¿puede esta radio decodificar sola una llamada
5-Tonos (CCIR)? Todos, yo incluido, creíamos que el audio FM recibido nunca llega
al procesador. Sí llega: el códec del HR-C6000 puede entregarlo por I2S, y en FM
ese camino simplemente está sin usar. Con ese audio, un decodificador se
convirtió en varios. No hace falta ningún hardware adicional, todo es software.

**Qué añade**
- 5-Tonos (CCIR): recepción y envío, ANI por canal, llamada selectiva, respuesta automática
- Recepción DTMF (también los PTT-ID muy rápidos que suenan como un solo "bip") y registro de llamadas
- Morse (CW): recepción, envío y práctica con el PTT como manipulador (sin transmitir)
- APRS: recepción (últimas 16 estaciones, distancia, rumbo, brújula, por qué digi llegó),
  mensajes, WhatsApp y e-mail por APRS, ajustes APRS editables en la radio
- Correcciones de TX APRS: se envía la trama completa, preámbulo más largo, sin balizas en 0,0
- Grabador de RX: graba cada transmisión FM que oyes (aprox. 1 hora), en carpetas por canal
- Buscador de tonos CTCSS/DCS, caza del zorro, barrido de espectro que se detiene con la voz
- Reloj por GPS, volumen muy bajo para la noche, brillo con SK1 + mando de volumen
- Canales, zonas y contraseña de encendido gestionados desde la radio
- Menús y manual de usuario en español (desde v0.2.0), además de turco e inglés, y algunos juegos

**Importante**
- Solo lo he probado en el MD-UV390 Plus 10W. No he probado otros modelos.
- No he tocado la potencia de TX, la calibración, el PA ni las tablas de RF. Aun así es un
  proyecto de aficionado: úsalo bajo tu propia responsabilidad y guarda copia de tu codeplug.
- Nunca transmitas con el cable de programación conectado.
- El códec AMBE no está en este código; el firmware loader lo añade desde el firmware
  oficial de TYT al cargarlo en la radio. Sin él no funciona la voz DMR.
- Se aplica la licencia de OpenGD77. **Prohibido el uso comercial.**

**Apoyar el proyecto:** el firmware es y será siempre gratuito. Si te ha sido útil y
quieres invitarme a un té, un donativo es muy bienvenido. Es un regalo, no una
compra: no incluye licencia, soporte ni garantía.
[☕ buymeacoffee.com/ta8bmo](https://buymeacoffee.com/ta8bmo)

73 de TA8BMO

---

# Русский

Моя собственная версия прошивки OpenGD77 для **TYT MD-UV390 Plus 10W**.

Всё началось с одного вопроса: может ли эта радиостанция сама декодировать
вызов 5-Tone (CCIR)? Все, включая меня, считали, что принятый FM-звук не доходит
до процессора. Оказалось, доходит: кодек HR-C6000 может передать его по I2S, а в
режиме FM этот путь просто не используется. Из одного декодера получилось
несколько. Никакого дополнительного железа, только программа.

**Что добавлено**
- 5-Tone (CCIR): приём и передача, ANI для каждого канала, селективный вызов, автоответ
- Приём DTMF (включая очень быстрые PTT-ID, которые звучат как один «бип») и журнал вызовов
- Морзе (CW): приём, передача и тренажёр с PTT в роли ключа (без передачи в эфир)
- APRS: приём (последние 16 станций, расстояние, азимут, компас, через какой digi пришёл пакет),
  сообщения, WhatsApp и e-mail через APRS, настройки APRS прямо на радиостанции
- Исправления передачи APRS: кадр уходит полностью, длиннее преамбула, нет маяков с позицией 0,0
- Запись приёма: записывает каждую услышанную FM-передачу (около 1 часа), по папкам каналов
- Поиск тона CTCSS/DCS, «охота на лис», сканирование спектра с остановкой на речи
- Часы по GPS, очень тихая громкость для ночи, яркость через SK1 + ручку громкости
- Каналы, зоны и пароль включения настраиваются на самой радиостанции
- Встроенное руководство (турецкий, английский и испанский) и несколько игр

**Важно**
- Проверено только на MD-UV390 Plus 10W. Другие модели я не проверял.
- Мощность передатчика, калибровку, PA и RF-таблицы я не трогал. И всё же это
  любительский проект: используйте на свой риск и сохраните копию кодплага.
- Никогда не передавайте с подключённым кабелем программирования.
- Кодека AMBE в этом исходном коде нет; firmware loader добавляет его из официальной
  прошивки TYT при загрузке. Без него голос DMR не работает.
- Действует лицензия OpenGD77. **Коммерческое использование запрещено.**

**Поддержать проект:** прошивка бесплатна и всегда будет бесплатной. Если она вам
пригодилась и вы хотите угостить меня чаем, буду очень рад. Это подарок, а не
покупка: лицензия, поддержка или гарантия не предоставляются.
[☕ buymeacoffee.com/ta8bmo](https://buymeacoffee.com/ta8bmo)

73 de TA8BMO
