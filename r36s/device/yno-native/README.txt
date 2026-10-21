YUME NIKKI ONLINE - klient natywny dla R36S (bez przegladarki)
==============================================================

To jest EasyRPG Player z kodu projektu YNO (ynoengine), przerobiony tak, zeby dzialal natywnie
na konsoli i laczyl sie z serwerami ynoproject.net przez libcurl. Zamiast Firefoksa (setki MB RAM)
caly program to jeden plik okolo 7 MB.

Start:   EmulationStation -> Ports -> "Yume Nikki Online (native)"
Lista gier: po starcie widzisz wszystkie 25 gier projektu YNO (Yume Nikki, Yume 2kki, .flow, ...)
         z liczba graczy online. Gora/dol wybiera, A uruchamia, B zamyka program. Gra bez znacznika
         "(download)" jest juz na karcie.
Powrot do listy: START (menu ustawien) -> Exit. Wyjscie z programu: SELECT + START (przytrzymaj).
Pierwsze uruchomienie gry: program sam pobiera jej dane do yno-native/games/<gra>/
         - przy wolnym internecie (ESP32) moze to chwile trwac, dalej pobiera brakujace pliki
         w trakcie gry (przy wejsciu do nowych miejsc moze sie na moment zatrzymac).
         Pobrane pliki zostaja na karcie. Zapisy tez leza w tym katalogu (SaveXX.lsd).
Uwaga: Yume 2kki jest bardzo duze (setki plikow), pierwsze pobieranie moze trwac dlugo.

CO DZIALA
---------
- lista wszystkich gier YNO z liczbami graczy; gra z serwera YNO (pliki pobierane z ynoproject.net/data/<gra>/ tak jak w przegladarce)
- zapisy lokalne: yno-native/games/yume/SaveXX.lsd (widoczne z komputera)
- polaczenie z serwerem sesji: licznik graczy, czat (odczyt): ostatnie wiadomosci pojawiaja sie na
  dole ekranu na kilka sekund (ustawienie CHAT_PINNED=1 w yno.conf trzyma je caly czas)
- automatyczne laczenie z internetem przez ESP32, ustawienie zegara

CZEGO JESZCZE NIE MA (kolejne etapy)
------------------------------------
- widzenie innych graczy na mapie: serwery YNO odrzucaja pakiety bez tajnego klucza (room.key),
  ktorego nie ma w publicznym kodzie. Bez niego gra dziala jak solo + czat. Jesli dostaniesz klucz od
  tworcow YNO, wrzuc go jako yno-native/room.key.
- lista graczy, grupy, znajomi, odznaki, rankingi, lokalizacje, logowanie, synchronizacja zapisow
- pisanie na czacie (klawiatura ekranowa)

STEROWANIE (domyslne EasyRPG, wg nazw na obudowie R36S)
--------------------------------------------------------
  D-pad / lewa galka   ruch
  A                    Z: potwierdz / rozmawiaj
  B lub X              X: anuluj / menu
  Y                    Shift
  R1 / L1              klawisz 1 (uzyj efektu) / klawisz 3 (zdejmij efekt)
  R2 / L2              klawisz 5 (odloz efekt) / klawisz 9 (obudz sie)
  START                menu ustawien silnika (glosnosc, itp.)
  SELECT + START       wyjscie

UWAGA - TEN KLIENT NIE BYL JESZCZE URUCHOMIONY NA KONSOLI
---------------------------------------------------------
Zbudowalem go i sprawdzilem na komputerze (emulacja aarch64, wirtualny ekran): pobiera dane z
serwera YNO, rysuje ekran tytulowy Yume Nikki, laczy sie z serwerem sesji i pokazuje czat.
Nie sprawdzilem na urzadzeniu: obrazu (KMSDRM), dzwieku, pada ani wydajnosci. Jesli cos nie dziala:
  - wyslij plik yno-native/last-run.log (zawiera dokladne komunikaty i zuzycie pamieci "mem: rss")
  - zmien VIDEO_DRIVER w yno.conf (np. na kmsdrm albo offscreen) i sprobuj ponownie

PLIKI
-----
  Yume Nikki Online (native).sh      launcher widoczny w EmulationStation
  yno-native/yno-r36s                program
  yno-native/run.sh                  uruchamia program: internet, zegar, ustawienia (argument: id gry)
  yno-native/yno.conf                ustawienia
  yno-native/games/<gra>/            pobrane dane gry i zapisy
  yno-native/last-run.log            log ostatniego uruchomienia
