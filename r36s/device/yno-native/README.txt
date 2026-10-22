YUME NIKKI ONLINE - klient natywny dla R36S (bez przegladarki)
==============================================================

To jest EasyRPG Player z kodu projektu YNO (ynoengine), przerobiony tak, zeby dzialal natywnie
na konsoli i laczyl sie z serwerami ynoproject.net przez libcurl. Zamiast Firefoksa (setki MB RAM)
caly program to jeden plik okolo 7 MB.

Start:   EmulationStation -> Ports -> "Yume Nikki Online (native)"
Lista gier: po starcie widzisz tekstowe menu z wszystkimi 25 grami projektu YNO (Yume Nikki,
         Yume 2kki, .flow, ...) i liczba graczy online. Gora/dol wybiera (lewo/prawo: cala strona),
         A uruchamia, Y otwiera opcje gry, B zamyka program. "(download)" = gry nie ma jeszcze na karcie.
Opcje gry (Y):
         Download all files  pobiera od razu WSZYSTKIE pliki gry, zeby gra nigdy nie czekala na pobieranie
                             (zalecane; pliki juz obecne na karcie sa pomijane, mozna przerwac przez B i
                             wznowic kiedy indziej). Rozmiar: ok. 14-250 MB, Yume 2kki ok. 1,2 GB.
                             Po zakonczeniu gra dostaje znak kompletnosci: bez niego na liscie widac
                             "(partial)" albo "(download)".
         Verify files        porownuje rozmiar kazdego pliku gry z karty z serwerem i pobiera ponownie
                             te, ktore sie roznia (serwer nie publikuje sum kontrolnych, wiec sprawdzany
                             jest rozmiar). Sprawdza tylko pliki, ktore juz sa na karcie.
         Re-download files   pobiera od nowa wszystkie pliki gry, ktore sa na karcie (zapisy zostaja)
         W trakcie widac pasek postepu; B przerywa.
Pasek postepu pojawia sie tez w grze, gdy program pobiera brakujace pliki (kawalek ponizej srodka ekranu).
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
- automatyczne laczenie z internetem: telefon z "Tethering USB" (Android) albo ESP32, ustawienie zegara

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
  USB Tethering.sh, usb-tether/      internet z telefonu przez USB (wlacz Tethering USB w telefonie, potem uruchom)
