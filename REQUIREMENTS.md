# Build requirements / Требования для сборки

[English](#english) · [Русский](#русский)

## English

### Common to all platforms

| What | Minimum | Used for |
| --- | --- | --- |
| CMake | 3.21 | build configuration |
| C++20 compiler | GCC 11, Clang 16 / Xcode 15, MSVC 2022 (17.x) | all applications |
| GNU Bison | 3.5 | game data parsers (L2NPC, CacheD, L2Server) |
| re2c | 3.0 | game data scanners (L2NPC, CacheD, L2Server) |
| PostgreSQL client (libpq) | 18 | L2NPC, CacheD, L2Server, AuthD |
| libxml2 | any current | L2Server |
| OpenSSL (libcrypto) | 3.0 | AuthGateD |

To **run** the server you also need a PostgreSQL 18 server with ICU support.
Plan for about 10 GB of free disk space: the sources with game data take
5.5 GB and a release build about 1 GB (a debug build needs up to 6 GB). Use at
least 8 GB of RAM (16 GB is more comfortable, L2Server has very large source
files).

### Windows

- Visual Studio 2022 with the "Desktop development with C++" workload
  (it includes CMake).
- PostgreSQL 18 for Windows x64. Its installation also provides libpq,
  libxml2 and OpenSSL, and CMake finds it in `C:\Program Files\PostgreSQL`.
- winflexbison (`win_bison`) and re2c on `PATH`.
- Configure with the `windows-msvc` preset and build with
  `windows-msvc-release`.

A MinGW cross build (`mingw-release` preset) builds only L2NPC, CacheD and
NASC.

### macOS

- Xcode 15 or newer (or its Command Line Tools).
- Homebrew: `brew install cmake bison re2c openssl@3`.
  The system bison 2.3 is too old; CMake finds the Homebrew one.
- PostgreSQL 18 from the official installer. CMake looks in
  `/Library/PostgreSQL/18`; set `PG_PREFIX` for another location.

### Linux (Debian / Ubuntu)

```sh
sudo apt install build-essential cmake bison re2c libpq-dev libxml2-dev \
    libssl-dev
```

On older distributions check that `gcc`, `bison`, `re2c` and the PostgreSQL
client meet the minimum versions above. For PostgreSQL 18 the official
PostgreSQL apt repository may be needed.

### Build

```sh
cmake --preset native-release
cmake --build --preset native-release
```

On Windows:

```sh
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

More details are in [README.md](README.md).

## Русский

### Общее для всех платформ

| Что | Минимум | Для чего |
| --- | --- | --- |
| CMake | 3.21 | конфигурация сборки |
| Компилятор C++20 | GCC 11, Clang 16 / Xcode 15, MSVC 2022 (17.x) | все приложения |
| GNU Bison | 3.5 | парсеры игровых данных (L2NPC, CacheD, L2Server) |
| re2c | 3.0 | сканеры игровых данных (L2NPC, CacheD, L2Server) |
| Клиент PostgreSQL (libpq) | 18 | L2NPC, CacheD, L2Server, AuthD |
| libxml2 | любая актуальная | L2Server |
| OpenSSL (libcrypto) | 3.0 | AuthGateD |

Для **запуска** сервера нужен ещё сервер PostgreSQL 18 с поддержкой ICU.
Закладывайте около 10 ГБ свободного места: исходники с игровыми данными
занимают 5,5 ГБ, release-сборка около 1 ГБ (debug-сборке нужно до 6 ГБ).
Оперативной памяти нужно минимум 8 ГБ (лучше 16 ГБ: у L2Server очень большие
исходные файлы).

### Windows

- Visual Studio 2022 с нагрузкой «Разработка классических приложений на C++»
  (CMake входит в состав).
- PostgreSQL 18 для Windows x64. Его установка заодно даёт libpq, libxml2
  и OpenSSL, и CMake находит её в `C:\Program Files\PostgreSQL`.
- winflexbison (`win_bison`) и re2c в `PATH`.
- Конфигурация через пресет `windows-msvc`, сборка через
  `windows-msvc-release`.

Кросс-сборка MinGW (пресет `mingw-release`) собирает только L2NPC, CacheD
и NASC.

### macOS

- Xcode 15 или новее (или его Command Line Tools).
- Homebrew: `brew install cmake bison re2c openssl@3`.
  Системный bison 2.3 слишком старый, CMake сам найдёт версию из Homebrew.
- PostgreSQL 18 из официального установщика. CMake ищет его в
  `/Library/PostgreSQL/18`; для другого пути задайте `PG_PREFIX`.

### Linux (Debian / Ubuntu)

```sh
sudo apt install build-essential cmake bison re2c libpq-dev libxml2-dev \
    libssl-dev
```

На старых дистрибутивах проверьте, что `gcc`, `bison`, `re2c` и клиент
PostgreSQL не старше указанных минимумов. Для PostgreSQL 18 может
понадобиться официальный apt-репозиторий PostgreSQL.

### Сборка

```sh
cmake --preset native-release
cmake --build --preset native-release
```

На Windows:

```sh
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

Подробности в [README.md](README.md).
