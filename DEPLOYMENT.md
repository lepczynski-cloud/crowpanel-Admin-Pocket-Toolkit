# Cloudflare Pages deployment (Polski)

## Model wdrożenia

**GitHub `main` -> Cloudflare Pages build -> strona + firmware z tego samego commita.**

Nie korzystamy z GitHub Actions ani z gałęzi `gh-pages`. Cały firmware kompiluje Cloudflare
w czasie wdrożenia, nie Twój komputer. Jest to statyczny hosting, bez własnego serwera i backendu.

## 1. Zaktualizuj repozytorium

Zastąp zawartość repozytorium `lepczynski-cloud/crowpanel-Admin-Pocket-Toolkit`
plikami z paczki (ZIP ma pliki bez dodatkowego katalogu nadrzędnego).

**Bardzo ważne:** samo przesłanie nowych plików przez GitHub WWW nie usuwa starych plików.
W repozytorium usuń również oba stare workflow z `.github/workflows/`,
np. `build-and-publish.yml` i `platformio-build.yml`. Zrób to przez GitHub
(otwórz plik, Delete file, Commit) lub przez Git:

```bash
git rm -r .github/workflows
git add -A
git commit -m "Build firmware in Cloudflare Pages instead of GitHub Actions"
git push origin main
```

Nie kasuj `src/main.cpp` ani `platformio.ini`: paczka zawiera ich niezmienione wersje.

## 2. Zmień istniejący projekt Cloudflare Pages

Otwórz **Cloudflare -> Workers & Pages -> ESP32 Admin Pocket Toolkit -> Settings -> Builds**
(nazwy zakładek w panelu mogą się nieco zmieniać).

Ustaw dokładnie:

| Pole | Wartość |
|---|---|
| Git repository | `lepczynski-cloud/crowpanel-Admin-Pocket-Toolkit` |
| Production branch | `main` **(nie `gh-pages`)** |
| Framework preset | `None` |
| Build command | `bash build-cloudflare.sh` |
| Build output directory | `dist` |
| Root directory | puste / domyślny katalog repozytorium |
| Build system | `v3` |

W **Settings -> Environment variables** ustaw dla Production (oraz Preview, jeśli używasz):

- `NODE_VERSION` = `24`
- `PYTHON_VERSION` = `3.11`

Projekt zawiera również `.node-version` i `.python-version` jako zabezpieczenie.
`CF_PAGES_COMMIT_SHA` jest automatycznie podawany przez Cloudflare.

W **Branch control** upewnij się, że automatyczne wdrożenia produkcyjne są włączone.
Nie twórz nowego projektu Cloudflare: użyj istniejącego, aby zachować subdomenę.

Nie ustawiaj `web` ani `.` jako katalogu wynikowego. Jedynym poprawnym jest `dist`.

## 3. Wdrożenie

Cloudflare uruchomi nową kompilację po zmianie na `main`. Możesz również wybrać
**Deployments -> Retry deployment** po zapisaniu ustawień, jeśli panel udostępnia tę opcję.

Build wykonuje następujące kroki:

1. Tworzy izolowane środowisko Python i instaluje PlatformIO/esptool.
2. Wykonuje `pio run -e crowpanel_28`.
3. Scala `bootloader.bin`, tablicę partycji i aplikację do `dist/firmware/factory.bin`.
4. Generuje dwa manifesty ESP Web Tools i informacje o wydaniu z `CF_PAGES_COMMIT_SHA`.
5. Pobiera i lokalnie dołącza ESP Web Tools (paczka npm `10.4.0`).
6. Pobiera rzeczywistą miniaturę filmu z YouTube, jeśli nie dostarczono zdjęcia.
7. Sprawdza pliki, SHA-256, manifesty i spójność commita.
8. Po sukcesie Cloudflare publikuje **cały** katalog `dist`.

**Build, który zakończy się błędem, nie powinien publikować nowego niekompletnego zestawu.**
Cloudflare Pages utrzyma poprzednie udane wdrożenie, jeśli nowe nie przejdzie walidacji.

## 4. Weryfikacja po wdrożeniu

Otwórz w przeglądarce:

- `https://esp32-admin-toolkit.lepczynski.it/`
- `https://esp32-admin-toolkit.lepczynski.it/firmware/release.json`
- `https://esp32-admin-toolkit.lepczynski.it/firmware/manifest-factory.json`
- `https://esp32-admin-toolkit.lepczynski.it/firmware/factory.bin`
- `https://esp32-admin-toolkit.lepczynski.it/assets/device-photo.jpg`

`release.json` powinien być JSON-em. Pole `source_commit` powinno odpowiadać
commitowi produkcyjnego wdrożenia Cloudflare. Obie manifesty muszą wskazywać
odpowiednio `factory.bin` (offset 0) i `firmware.bin` (offset 65536).
Witryna powinna pokazywać status **Latest firmware ready** i przycisk instalatora.
Użyj Chrome/Edge na komputerze i kabla USB z obsługą transmisji danych.

Jeżeli w przeglądarce nadal masz stare JS, wykonaj twarde odświeżenie.
Strona dodaje SHA commita do adresów JS/CSS, aby ograniczyć problem cache.
Jeśli Cloudflare ma własne reguły cache lub service worker, wyczyść ich stare dane.

## 5. Dlaczego to naprawia poprzedni problem?

Poprzedni układ wyglądał tak: GitHub Actions budował do `gh-pages`, a Cloudflare
mógł publikować inną gałąź lub nieaktualny build. Dlatego zielony workflow
nie gwarantował działającego instalatora.

Nowy układ ma jeden proces: **Cloudflare pobiera `main`, kompiluje firmware i publikuje
stronę z manifestami w jednym wdrożeniu.** Nie ma potrzeby synchronizacji między
GitHub Actions i Cloudflare.

## Ograniczenia

- Pierwsza kompilacja musi mieć dostęp do PyPI, PlatformIO registry i npm.
- Cloudflare Pages ma limit **20 minut na build**; firmware wcześniej budował się
  w około minutę, ale pierwsze pobieranie bibliotek może być dłuższe.
- Nie testowano fizycznego flashowania w tym środowisku. Nie wgrywaj na inny model CrowPanel.
- Jeżeli udostępniona wersja ESP Web Tools ma problem z Web Serial w konkretnej
  przeglądarce, użyj desktopowego Chrome/Edge.
- Strona jest w pełni statyczna; wgrywanie firmware korzysta lokalnie z Web Serial.
