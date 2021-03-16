# Roomba
Software per ESP-01 montato su Roomba 606.

Utilizzo di due feed Adafruit, RoombaCtrl e RoombaData, per il controllo ed i dati
forniti dall'ESP.

Aggiornato al 14/03/2021  

Part list:

 1 x Roomba 606

 1 x MP1584EN Buck converter set to 3.3V

 1 x 3906 PNP transistor

 1 x ESP-01

Cablaggio:
 DEVICE  DIR N NAME  ->  DEVICE  DIR N NAME

 Roomba  OUT 1 VPWR  ->  Buck    IN  1 VPWR
 
 Roomba  OUT 6 VGND  ->  Buck    IN  3 VGND
 
 Buck    OUT 4 BPWR  ->  ESP-01  IN  8 VCC
 
 Buck    OUT 4 BPWR  ->  ESP-01  IN  4 Ch_EN
 
 Buck    OUT 6 BGND  ->  ESP-01  IN  1 GND
 
 Buck    OUT 6 BGND  ->  PNP     IN  2 BASE
 
 Roomba  OUT 4 TXD   ->  PNP     IN  2 TXD
 
 PNP     OUT 1 EMIT  ->  ESP-01  IN  7 RXD
 
 ESP-01  OUT 3 COLL  ->  Roomba  IN  3 RXD
 
 ESP-01  OUT 5 GPIO0 ->  Roomba  IN  5 BRC
